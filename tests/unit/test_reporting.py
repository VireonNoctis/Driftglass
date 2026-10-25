import json
from pathlib import Path

import pytest

from driftglass._schema import canonical_json
from driftglass.comparison import compare_loaded_runs
from driftglass.reporting import ReportExportError, export_html_report, export_json_report, render_html_report


ROOT = Path(__file__).resolve().parents[2]
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"


def manifest(role, run_id, version, artifact_hash):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "report-export-test",
        "role": role,
        "application": {"name": "Example App", "version": version, "artifact_sha256": artifact_hash, "package_id": "org.example.app"},
        "environment": {"os": "Linux", "os_version": "fixture", "architecture": "x86_64", "snapshot_id": "snapshot-a"},
        "capture": {"status": "completed", "started_at": "2026-10-25T10:00:00Z", "ended_at": "2026-10-25T10:01:00Z", "scenario": "launch", "profile": "minimal"},
        "coverage": {
            "event_sources": [
                {"name": "process", "state": "active", "categories": ["process"]},
                {"name": "network", "state": "active", "categories": ["network"]},
                {"name": "file", "state": "active", "categories": ["file"]},
                {"name": "system", "state": "active", "categories": ["system"]},
                {"name": "persistence", "state": "active", "categories": ["persistence"]}
            ],
            "unavailable_categories": [],
            "dropped_events": 0,
            "errors": []
        }
    }


def event(event_id, executable="example-app"):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-25T10:00:30Z",
        "category": "network",
        "action": "connection_attempt",
        "source": {"collector": "fixture/1", "event_ref": "record-" + event_id},
        "collection_quality": "complete",
        "process": {"pid": 100, "parent_pid": 1, "executable": executable},
        "resource": {"kind": "network", "identifier": "192.0.2.2:50000", "address": "198.51.100.10", "port": 443, "protocol": "tcp"},
        "attributes": {"socket_state": "ESTABLISHED"}
    }


def make_report(executable="example-app"):
    base = manifest("baseline", "run-base", "1.0", "a" * 64)
    candidate = manifest("candidate", "run-candidate", "2.0", "b" * 64)
    return compare_loaded_runs(base, candidate, [event("evt-base")], [event("evt-candidate", executable)])


def test_json_export_is_readable_and_refuses_overwrite(tmp_path):
    value = make_report()
    output = tmp_path / "report.json"
    assert export_json_report(value, REPORT_SCHEMA, output) == output
    assert json.loads(output.read_text(encoding="utf-8")) == value
    assert output.stat().st_mode & 0o077 == 0
    with pytest.raises(ReportExportError, match="already exists"):
        export_json_report(value, REPORT_SCHEMA, output)
    assert json.loads(output.read_text(encoding="utf-8")) == value


def test_json_export_requires_explicit_overwrite(tmp_path):
    output = tmp_path / "report.json"
    output.write_text("previous contents", encoding="utf-8")
    export_json_report(make_report(), REPORT_SCHEMA, output, overwrite=True)
    assert json.loads(output.read_text(encoding="utf-8"))["schema_version"] == 1


def test_html_escapes_untrusted_event_text_and_has_no_remote_assets(tmp_path):
    value = make_report(executable='<script>alert("fixture")</script>')
    output = export_html_report(value, REPORT_SCHEMA, tmp_path / "report.html")
    text = output.read_text(encoding="utf-8")
    assert "&lt;script&gt;alert(&quot;fixture&quot;)&lt;/script&gt;" in text
    assert '<script>alert("fixture")</script>' not in text
    assert "http://" not in text
    assert "https://" not in text


def test_report_id_is_checked_before_rendering():
    value = make_report()
    value["report_id"] = "0" * 64
    with pytest.raises(ReportExportError, match="report_id does not match"):
        render_html_report(value, REPORT_SCHEMA)


def test_invalid_report_schema_is_rejected(tmp_path):
    bad_schema = tmp_path / "bad.schema.json"
    bad_schema.write_text('{"type": "not-a-schema-type"}', encoding="utf-8")
    with pytest.raises(ReportExportError, match="report schema.*is invalid"):
        export_json_report(make_report(), bad_schema, tmp_path / "report.json")


def test_removes_partial_output_when_fsync_fails(tmp_path, monkeypatch):
    import driftglass.reporting as reporting

    report = make_report()
    output = tmp_path / "partial.json"

    def fail_fsync(_descriptor):
        raise OSError("synthetic fsync failure")

    monkeypatch.setattr(reporting.os, "fsync", fail_fsync)
    with pytest.raises(ReportExportError, match="could not write report"):
        export_json_report(report, REPORT_SCHEMA, output)

    assert not output.exists()


def test_missing_output_directory_is_not_created_implicitly(tmp_path):
    output = tmp_path / "missing-dir" / "report.json"
    with pytest.raises(ReportExportError, match="output directory does not exist"):
        export_json_report(make_report(), REPORT_SCHEMA, output)
    assert not output.parent.exists()


def test_html_export_refuses_existing_file(tmp_path):
    output = tmp_path / "report.html"
    output.write_text("keep", encoding="utf-8")
    with pytest.raises(ReportExportError, match="already exists"):
        export_html_report(make_report(), REPORT_SCHEMA, output)
    assert output.read_text(encoding="utf-8") == "keep"


def test_canonical_json_rejects_non_finite_values():
    with pytest.raises(ValueError, match="Out of range float values are not JSON compliant"):
        canonical_json({"metric": float("nan")})
