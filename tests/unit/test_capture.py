import json
import os
from pathlib import Path
from types import SimpleNamespace

import pytest

import driftglass.capture as capture
from driftglass.capture import CaptureError, capture_run
from driftglass.events import load_jsonl
from driftglass.manifests import load_manifest


ROOT = Path(__file__).resolve().parents[2]
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"


def event(event_id, action, *, quality="complete", attributes=None):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-26T10:00:00Z",
        "category": "coverage",
        "action": action,
        "source": {"collector": "synthetic-agent/1", "event_ref": f"capture:{event_id}"},
        "collection_quality": quality,
        "attributes": attributes or {},
    }


def write_jsonl(path, records):
    path.write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )


def complete_events():
    return [
        event("start", "capture_started", attributes={"target_pid": 123}),
        event("finish", "capture_finished", attributes={"capture_status": "completed"}),
    ]


def setup_capture(tmp_path, monkeypatch, *, return_code=0, records=None):
    observer = tmp_path / "driftglass-agent"
    observer.write_text("synthetic executable for subprocess mocking", encoding="utf-8")
    observer.chmod(0o700)
    event_path = tmp_path / "events.jsonl"
    manifest_path = tmp_path / "manifest.json"
    calls = []

    def fake_run(command, **kwargs):
        calls.append((command, kwargs))
        output_path = Path(command[command.index("--output") + 1])
        if records is not None:
            write_jsonl(output_path, records)
        return SimpleNamespace(returncode=return_code, stderr="", stdout="")

    monkeypatch.setattr(capture.platform, "system", lambda: "Linux")
    monkeypatch.setattr(capture.subprocess, "run", fake_run)
    args = {
        "observer_path": observer,
        "pid": 123,
        "duration_seconds": 1,
        "interval_ms": 100,
        "experiment_id": "synthetic-experiment",
        "role": "baseline",
        "application_name": "Synthetic App; text only",
        "application_version": "1.0",
        "event_output_path": event_path,
        "manifest_output_path": manifest_path,
        "event_schema_path": EVENT_SCHEMA,
        "manifest_schema_path": MANIFEST_SCHEMA,
        "scenario": "synthetic, no application is launched by this test",
    }
    return args, calls, event_path, manifest_path


def test_capture_writes_valid_manifest_and_invokes_only_observer(tmp_path, monkeypatch):
    args, calls, event_path, manifest_path = setup_capture(
        tmp_path, monkeypatch, records=complete_events()
    )
    result = capture_run(**args)

    checked = load_manifest(result.manifest_path, MANIFEST_SCHEMA)
    records = load_jsonl(result.event_path, EVENT_SCHEMA)
    command, options = calls[0]

    assert checked["capture"]["status"] == "completed"
    assert checked["role"] == "baseline"
    assert checked["coverage"]["unavailable_categories"] == ["file", "system", "persistence"]
    assert result.event_count == len(records) == 2
    assert event_path.is_file() and manifest_path.is_file()
    assert command[0] == str(args["observer_path"].resolve())
    assert command[command.index("--pid") + 1] == "123"
    assert "--application-name" not in command
    assert "Synthetic App; text only" not in command
    assert "shell" not in options
    assert options["stdin"] is capture.subprocess.DEVNULL
    if os.name == "posix":
        assert manifest_path.stat().st_mode & 0o777 == 0o600


def test_completed_capture_keeps_coverage_gaps_visible(tmp_path, monkeypatch):
    records = [
        event("start", "capture_started"),
        event("gap", "coverage_gap", quality="partial", attributes={"category": "network"}),
        event("finish", "capture_finished", quality="partial"),
    ]
    args, _, _, manifest_path = setup_capture(tmp_path, monkeypatch, records=records)

    result = capture_run(**args)
    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)

    assert manifest["capture"]["status"] == "completed"
    assert any("coverage gap for network" in message for message in manifest["coverage"]["errors"])
    assert any("partial-quality" in message for message in manifest["coverage"]["errors"])
    assert result.event_count == 3


def test_failed_observer_writes_failed_manifest_and_returns_error(tmp_path, monkeypatch):
    args, _, event_path, manifest_path = setup_capture(tmp_path, monkeypatch, return_code=2)

    with pytest.raises(CaptureError, match="capture status is failed"):
        capture_run(**args)

    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)
    assert manifest["capture"]["status"] == "failed"
    assert manifest["capture"]["status_reason"]
    assert not event_path.exists()
    assert set(manifest["coverage"]["unavailable_categories"]) == {
        "process", "network", "file", "system", "persistence"
    }
    assert any("status 2" in message for message in manifest["coverage"]["errors"])


def test_missing_capture_finished_is_incomplete_not_success(tmp_path, monkeypatch):
    args, _, _, manifest_path = setup_capture(
        tmp_path, monkeypatch, records=[event("start", "capture_started")]
    )

    with pytest.raises(CaptureError, match="capture status is incomplete"):
        capture_run(**args)

    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)
    assert manifest["capture"]["status"] == "incomplete"
    assert any("capture_finished" in message for message in manifest["coverage"]["errors"])


def test_rejects_output_path_collision_before_running_observer(tmp_path, monkeypatch):
    args, calls, _, _ = setup_capture(tmp_path, monkeypatch, records=complete_events())
    args["manifest_output_path"] = args["event_output_path"]

    with pytest.raises(CaptureError, match="must be different"):
        capture_run(**args)

    assert calls == []


def test_rejects_invalid_pid_before_running_observer(tmp_path, monkeypatch):
    args, calls, _, _ = setup_capture(tmp_path, monkeypatch, records=complete_events())
    args["pid"] = 0

    with pytest.raises(CaptureError, match="positive integer"):
        capture_run(**args)

    assert calls == []



def test_capture_incomplete_signal_overrides_zero_exit_and_boundary_events(tmp_path, monkeypatch):
    records = [
        event("start", "capture_started"),
        event(
            "incomplete",
            "capture_incomplete",
            quality="partial",
            attributes={"reason_code": "process_scan_failed"},
        ),
        event(
            "finish",
            "capture_finished",
            quality="partial",
            attributes={"capture_status": "completed"},
        ),
    ]
    args, _, _, manifest_path = setup_capture(tmp_path, monkeypatch, records=records)

    with pytest.raises(CaptureError, match="capture status is incomplete"):
        capture_run(**args)

    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)
    assert manifest["capture"]["status"] == "incomplete"
    assert any("observer reported an incomplete capture" in note for note in manifest["coverage"]["errors"])
    assert manifest["capture"]["status_reason"]


def test_timeout_with_partial_stream_writes_incomplete_manifest(tmp_path, monkeypatch):
    args, _, event_path, manifest_path = setup_capture(tmp_path, monkeypatch)

    def timeout_run(command, **options):
        output_path = Path(command[command.index("--output") + 1])
        write_jsonl(output_path, [event("start", "capture_started")])
        raise capture.subprocess.TimeoutExpired(command, options["timeout"])

    monkeypatch.setattr(capture.subprocess, "run", timeout_run)

    with pytest.raises(CaptureError, match="capture status is incomplete"):
        capture_run(**args)

    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)
    assert event_path.is_file()
    assert manifest["capture"]["status"] == "incomplete"
    assert any("exceeded the coordinator time limit" in note for note in manifest["coverage"]["errors"])


def test_malformed_observer_stream_writes_failed_manifest(tmp_path, monkeypatch):
    args, _, event_path, manifest_path = setup_capture(tmp_path, monkeypatch)

    def malformed_run(command, **options):
        output_path = Path(command[command.index("--output") + 1])
        output_path.write_text('{"schema_version": 1\\n', encoding="utf-8")
        return SimpleNamespace(returncode=0, stderr="", stdout="")

    monkeypatch.setattr(capture.subprocess, "run", malformed_run)

    with pytest.raises(CaptureError, match="capture status is failed"):
        capture_run(**args)

    manifest = load_manifest(manifest_path, MANIFEST_SCHEMA)
    assert event_path.is_file()
    assert manifest["capture"]["status"] == "failed"
    assert any("did not pass JSONL validation" in note for note in manifest["coverage"]["errors"])
    assert any("capture_started" in note for note in manifest["coverage"]["errors"])


def test_manifest_write_failure_does_not_delete_preexisting_path(tmp_path):
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text("keep this file", encoding="utf-8")

    with pytest.raises(CaptureError, match="could not write run manifest"):
        capture._write_new_json(manifest_path, {"status": "completed"})

    assert manifest_path.read_text(encoding="utf-8") == "keep this file"


def test_manifest_write_failure_preserves_replacement_path(tmp_path, monkeypatch):
    manifest_path = tmp_path / "manifest.json"

    def replace_then_fail(value, stream, **kwargs):
        manifest_path.unlink()
        manifest_path.write_text("replacement file", encoding="utf-8")
        raise ValueError("synthetic serialization failure")

    monkeypatch.setattr(capture.json, "dump", replace_then_fail)

    with pytest.raises(CaptureError, match="could not write run manifest"):
        capture._write_new_json(manifest_path, {"status": "completed"})

    assert manifest_path.read_text(encoding="utf-8") == "replacement file"

