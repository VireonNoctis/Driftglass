import copy
import json
from pathlib import Path

import pytest

from driftglass.comparison import compare_loaded_runs
from driftglass.redaction import redact_report
from driftglass.reporting import ReportExportError, validate_report


ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"


def manifest(role, run_id, version, artifact_hash):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "redaction-test",
        "role": role,
        "application": {
            "name": "Synthetic App",
            "version": version,
            "artifact_sha256": artifact_hash,
            "package_id": "org.example.redaction",
        },
        "environment": {
            "os": "Linux",
            "os_version": "fixture",
            "architecture": "x86_64",
            "snapshot_id": "synthetic-snapshot",
        },
        "capture": {
            "status": "completed",
            "started_at": "2026-10-26T10:00:00Z",
            "ended_at": "2026-10-26T10:01:00Z",
            "scenario": "synthetic network check",
            "profile": "fixture",
        },
        "coverage": {
            "event_sources": [
                {"name": "process", "state": "active", "categories": ["process"]},
                {"name": "network", "state": "active", "categories": ["network"]},
                {"name": "file", "state": "active", "categories": ["file"]},
                {"name": "system", "state": "active", "categories": ["system"]},
                {"name": "persistence", "state": "active", "categories": ["persistence"]},
            ],
            "unavailable_categories": [],
            "dropped_events": 0,
            "errors": [],
        },
    }


def event(event_id, action):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-26T10:00:30Z",
        "category": "network",
        "action": action,
        "source": {"collector": "synthetic-fixture/1", "event_ref": f"source-{event_id}"},
        "collection_quality": "complete",
        "process": {"pid": 100, "parent_pid": 1, "executable": "synthetic-runner"},
        "resource": {
            "kind": "network",
            "identifier": "192.0.2.2:50000",
            "address": "api.internal.example",
            "port": 443,
            "protocol": "tcp",
        },
        "attributes": {
            "config_path": "/home/alice/private/config.json",
            "destination_host": "api.internal.example",
            "state": "ESTABLISHED",
        },
    }


def make_report():
    base = manifest("baseline", "run-base", "1.0", "a" * 64)
    candidate = manifest("candidate", "run-candidate", "2.0", "b" * 64)
    return compare_loaded_runs(
        base,
        candidate,
        [event("evt-base", "connection_attempt")],
        [event("evt-candidate", "connection_established")],
    )


def test_redacts_paths_network_and_process_names_without_mutating_source():
    source = make_report()
    original = copy.deepcopy(source)

    redacted = redact_report(
        source,
        REPORT_SCHEMA,
        paths=True,
        network=True,
        process_names=True,
    )
    rendered = json.dumps(redacted, sort_keys=True)

    assert "/home/alice/private/config.json" not in rendered
    assert "api.internal.example" not in rendered
    assert "192.0.2.2" not in rendered
    assert "synthetic-runner" not in rendered
    assert "[REDACTED_PATH_001]" in rendered
    assert "[REDACTED_HOST_001]" in rendered
    assert "[REDACTED_PROCESS_001]" in rendered
    assert source == original
    assert redacted["report_id"] != source["report_id"]
    assert all(
        finding["finding_id"] != source_finding["finding_id"]
        for finding, source_finding in zip(redacted["findings"], source["findings"], strict=True)
    )
    validate_report(redacted, REPORT_SCHEMA)


def test_redaction_preserves_equal_value_tokens_across_evidence():
    source = make_report()
    redacted = redact_report(source, REPORT_SCHEMA, network=True)

    evidence = [
        item
        for finding in redacted["findings"]
        for item in finding["baseline_evidence"] + finding["candidate_evidence"]
    ]
    addresses = {
        item["observed"]["resource"]["address"]
        for item in evidence
        if item["observed"]["resource"] is not None
    }

    assert addresses == {"[REDACTED_HOST_001]"}
    assert all(
        "[REDACTED_HOST_001]" in item["observed"]["attributes"]["destination_host"]
        for item in evidence
    )


def test_selective_redaction_leaves_other_categories_unchanged():
    source = make_report()
    path_only = redact_report(source, REPORT_SCHEMA, paths=True)
    network_only = redact_report(source, REPORT_SCHEMA, network=True)

    path_text = json.dumps(path_only)
    network_text = json.dumps(network_only)
    assert "/home/alice/private/config.json" not in path_text
    assert "api.internal.example" in path_text
    assert "synthetic-runner" in path_text
    assert "/home/alice/private/config.json" in network_text
    assert "api.internal.example" not in network_text
    assert "synthetic-runner" in network_text


def test_rejects_empty_redaction_profile():
    with pytest.raises(ReportExportError, match="at least one redaction category"):
        redact_report(make_report(), REPORT_SCHEMA)
