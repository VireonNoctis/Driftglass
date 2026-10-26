import json
from pathlib import Path

import pytest

from driftglass.manifests import ManifestValidationError, load_manifest


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"


def manifest(**overrides):
    result = {
        "schema_version": 1,
        "run_id": "run-001",
        "experiment_id": "experiment-001",
        "role": "baseline",
        "application": {
            "name": "Synthetic App",
            "version": "1.0.0",
            "artifact_sha256": "a" * 64,
            "package_id": "org.example.synthetic",
        },
        "environment": {
            "os": "Linux",
            "os_version": "synthetic-kernel",
            "architecture": "x86_64",
            "snapshot_id": "snapshot-001",
        },
        "capture": {
            "status": "completed",
            "started_at": "2026-10-25T10:00:00Z",
            "ended_at": "2026-10-25T10:05:00Z",
            "scenario": "launch and idle",
            "profile": "minimal-v1",
        },
        "coverage": {
            "event_sources": [
                {
                    "name": "synthetic-process-source",
                    "state": "active",
                    "categories": ["process"],
                },
                {
                    "name": "synthetic-network-source",
                    "state": "unavailable",
                    "categories": ["network"],
                },
            ],
            "unavailable_categories": ["network"],
            "dropped_events": 0,
            "errors": ["network source not present in synthetic fixture"],
        },
    }
    result.update(overrides)
    return result


def write_manifest(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data) + "\n", encoding="utf-8")


def test_loads_valid_manifest_and_preserves_coverage_gaps(tmp_path):
    path = tmp_path / "manifest.json"
    write_manifest(path, manifest())

    result = load_manifest(path, SCHEMA)

    assert result["role"] == "baseline"
    assert result["coverage"]["unavailable_categories"] == ["network"]
    assert result["coverage"]["event_sources"][1]["state"] == "unavailable"


def test_allows_missing_artifact_hash_but_keeps_it_explicit(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["application"]["artifact_sha256"] = None
    write_manifest(path, data)

    assert load_manifest(path, SCHEMA)["application"]["artifact_sha256"] is None


def test_rejects_completed_capture_without_end_time(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["capture"]["ended_at"] = None
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match="completed capture requires"):
        load_manifest(path, SCHEMA)


def test_rejects_end_time_before_start_time(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["capture"]["ended_at"] = "2026-10-25T09:59:59Z"
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match="must not be earlier"):
        load_manifest(path, SCHEMA)


@pytest.mark.parametrize("status", ["incomplete", "failed"])
def test_requires_reason_for_non_successful_terminal_capture(tmp_path, status):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["capture"].update(status=status, ended_at=None)
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match="requires a non-empty capture.status_reason"):
        load_manifest(path, SCHEMA)


def test_rejects_unreported_gap_for_unavailable_source(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["coverage"]["unavailable_categories"] = []
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match="must include 'network'"):
        load_manifest(path, SCHEMA)


def test_rejects_duplicate_event_source_names(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["coverage"]["event_sources"][1]["name"] = data["coverage"]["event_sources"][0]["name"]
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match="duplicate coverage.event_sources name"):
        load_manifest(path, SCHEMA)


def test_rejects_malformed_json(tmp_path):
    path = tmp_path / "manifest.json"
    path.write_text("{not-json}\n", encoding="utf-8")

    with pytest.raises(ManifestValidationError, match="contains malformed JSON"):
        load_manifest(path, SCHEMA)


def test_rejects_unknown_run_role(tmp_path):
    path = tmp_path / "manifest.json"
    data = manifest()
    data["role"] = "maybe-baseline"
    write_manifest(path, data)

    with pytest.raises(ManifestValidationError, match=r"\$\.role: value is not in the allowed set"):
        load_manifest(path, SCHEMA)



def test_rejects_duplicate_json_object_keys(tmp_path):
    path = tmp_path / "manifest.json"
    raw = json.dumps(manifest()).replace(
        '"role": "baseline"',
        '"role": "baseline", "role": "candidate"',
    )
    path.write_text(raw + "\\n", encoding="utf-8")

    with pytest.raises(ManifestValidationError, match="duplicate JSON object keys are not valid"):
        load_manifest(path, SCHEMA)


def test_rejects_non_finite_numbers(tmp_path):
    path = tmp_path / "manifest.json"
    raw = json.dumps(manifest()).replace('"dropped_events": 0', '"dropped_events": NaN')
    path.write_text(raw + "\\n", encoding="utf-8")

    with pytest.raises(ManifestValidationError, match="non-finite numeric constants are not valid JSON"):
        load_manifest(path, SCHEMA)

def test_rejects_missing_manifest_file(tmp_path):
    with pytest.raises(ManifestValidationError, match="could not read manifest"):
        load_manifest(tmp_path / "missing.json", SCHEMA)


def test_rejects_manifest_over_file_size_limit(tmp_path, monkeypatch):
    path = tmp_path / "large-manifest.json"
    path.write_text('{"schema_version":1}', encoding="utf-8")
    monkeypatch.setattr("driftglass.manifests.MAX_MANIFEST_FILE_BYTES", 8)

    with pytest.raises(ManifestValidationError, match="exceeds the 8-byte file limit"):
        load_manifest(path, SCHEMA)
