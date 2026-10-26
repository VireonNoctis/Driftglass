import json
from pathlib import Path

import pytest

from driftglass.comparison import compare_loaded_runs
from driftglass.integrity import IntegrityError, attest_report, verify_report
from driftglass.reporting import export_json_report

ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"
INTEGRITY_SCHEMA = ROOT / "schemas" / "reports" / "export-manifest-v1.schema.json"


def manifest(role, run_id, version, artifact_hash):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "integrity-test",
        "role": role,
        "application": {
            "name": "Synthetic App", "version": version,
            "artifact_sha256": artifact_hash, "package_id": "org.example.integrity",
        },
        "environment": {
            "os": "Linux", "os_version": "fixture",
            "architecture": "x86_64", "snapshot_id": "integrity-fixture",
        },
        "capture": {
            "status": "completed",
            "started_at": "2026-10-25T10:00:00Z",
            "ended_at": "2026-10-25T10:01:00Z",
            "scenario": "synthetic test", "profile": "fixture",
        },
        "coverage": {
            "event_sources": [
                {"name": category, "state": "active", "categories": [category]}
                for category in ("process", "network", "file", "system", "persistence")
            ],
            "unavailable_categories": [], "dropped_events": 0, "errors": [],
        },
    }


def event(event_id, action):
    return {
        "schema_version": 1, "event_id": event_id,
        "timestamp": "2026-10-25T10:00:30Z",
        "category": "network", "action": action,
        "source": {"collector": "integrity-fixture/1", "event_ref": event_id},
        "collection_quality": "complete",
        "process": {"pid": 123, "parent_pid": 1, "executable": "fixture"},
        "resource": {
            "kind": "network", "identifier": "192.0.2.2:50000",
            "address": "198.51.100.5", "port": 443, "protocol": "tcp",
        },
        "attributes": {"state": "ESTABLISHED"},
    }


def create_report(path):
    baseline = manifest("baseline", "integrity-base", "1.0", "a" * 64)
    candidate = manifest("candidate", "integrity-candidate", "2.0", "b" * 64)
    report = compare_loaded_runs(
        baseline, candidate,
        [event("base-event", "connection_attempt")],
        [event("candidate-event", "connection_established")],
    )
    export_json_report(report, REPORT_SCHEMA, path)
    return report


def test_attest_and_verify_detect_report_changes(tmp_path):
    report_path = tmp_path / "comparison.json"
    manifest_path = tmp_path / "comparison.integrity.json"
    report = create_report(report_path)

    attestation = attest_report(report_path, REPORT_SCHEMA, INTEGRITY_SCHEMA, manifest_path)
    assert attestation["report_id"] == report["report_id"]
    assert attestation["artifact_size_bytes"] == report_path.stat().st_size
    assert verify_report(report_path, manifest_path, REPORT_SCHEMA, INTEGRITY_SCHEMA) == attestation

    report_path.write_text(report_path.read_text(encoding="utf-8") + " ", encoding="utf-8")
    with pytest.raises(IntegrityError, match="report SHA-256 does not match"):
        verify_report(report_path, manifest_path, REPORT_SCHEMA, INTEGRITY_SCHEMA)


def test_manifest_is_validated_and_output_is_not_overwritten(tmp_path):
    report_path = tmp_path / "comparison.json"
    manifest_path = tmp_path / "comparison.integrity.json"
    create_report(report_path)
    attest_report(report_path, REPORT_SCHEMA, INTEGRITY_SCHEMA, manifest_path)

    with pytest.raises(IntegrityError, match="output already exists"):
        attest_report(report_path, REPORT_SCHEMA, INTEGRITY_SCHEMA, manifest_path)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["artifact_sha256"] = "0" * 64
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(IntegrityError, match="does not match integrity manifest"):
        verify_report(report_path, manifest_path, REPORT_SCHEMA, INTEGRITY_SCHEMA)


def test_rejects_malformed_and_oversized_integrity_manifest(tmp_path, monkeypatch):
    report_path = tmp_path / "comparison.json"
    manifest_path = tmp_path / "comparison.integrity.json"
    create_report(report_path)
    manifest_path.write_text("{not json}", encoding="utf-8")
    with pytest.raises(IntegrityError, match="strict UTF-8 JSON"):
        verify_report(report_path, manifest_path, REPORT_SCHEMA, INTEGRITY_SCHEMA)

    manifest_path.write_text(" " * 100, encoding="utf-8")
    monkeypatch.setattr("driftglass.integrity.MAX_INTEGRITY_MANIFEST_BYTES", 32)
    with pytest.raises(IntegrityError, match="file limit"):
        verify_report(report_path, manifest_path, REPORT_SCHEMA, INTEGRITY_SCHEMA)
