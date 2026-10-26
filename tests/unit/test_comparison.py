import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, FormatChecker

from driftglass.comparison import ComparisonError, compare_runs


ROOT = Path(__file__).resolve().parents[2]
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"


def manifest(role, run_id, version, artifact_hash, **updates):
    value = {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "experiment-001",
        "role": role,
        "application": {"name": "Synthetic App", "version": version, "artifact_sha256": artifact_hash, "package_id": "org.example.synthetic"},
        "environment": {"os": "Linux", "os_version": "synthetic-kernel", "architecture": "x86_64", "snapshot_id": "snapshot-001"},
        "capture": {"status": "completed", "started_at": "2026-10-25T10:00:00Z", "ended_at": "2026-10-25T10:05:00Z", "scenario": "launch and idle", "profile": "minimal-v1"},
        "coverage": {
            "event_sources": [
                {"name": "process-source", "state": "active", "categories": ["process"]},
                {"name": "network-source", "state": "active", "categories": ["network"]},
                {"name": "file-source", "state": "active", "categories": ["file"]},
                {"name": "system-source", "state": "active", "categories": ["system"]},
                {"name": "persistence-source", "state": "active", "categories": ["persistence"]}
            ],
            "unavailable_categories": [],
            "dropped_events": 0,
            "errors": []
        }
    }
    for key, replacement in updates.items():
        value[key] = replacement
    return value


def event(event_id="evt-001", **updates):
    value = {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-25T10:01:00Z",
        "category": "network",
        "action": "connection_attempt",
        "source": {"collector": "synthetic/1", "event_ref": "source-" + event_id},
        "collection_quality": "complete",
        "process": {"pid": 100, "parent_pid": 1, "executable": "synthetic-app"},
        "resource": {"kind": "network", "identifier": "192.0.2.2:51000", "address": "198.51.100.10", "port": 443, "protocol": "tcp"},
        "attributes": {"socket_state": "ESTABLISHED"}
    }
    value.update(updates)
    return value


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")


def write_jsonl(path: Path, values: list[dict]) -> None:
    path.write_text("".join(json.dumps(value) + "\n" for value in values), encoding="utf-8")


def compare(tmp_path, base, candidate, base_events, candidate_events):
    base_manifest_path = tmp_path / "baseline-manifest.json"
    candidate_manifest_path = tmp_path / "candidate-manifest.json"
    base_events_path = tmp_path / "baseline-events.jsonl"
    candidate_events_path = tmp_path / "candidate-events.jsonl"
    write_json(base_manifest_path, base)
    write_json(candidate_manifest_path, candidate)
    write_jsonl(base_events_path, base_events)
    write_jsonl(candidate_events_path, candidate_events)
    return compare_runs(base_manifest_path, candidate_manifest_path, base_events_path, candidate_events_path, MANIFEST_SCHEMA, EVENT_SCHEMA)


def pair():
    return manifest("baseline", "run-base", "1.0.0", "a" * 64), manifest("candidate", "run-candidate", "1.1.0", "b" * 64)


def test_equivalent_observations_ignore_timestamps_pids_and_source_refs(tmp_path):
    base, candidate = pair()
    left = event()
    right = event("evt-other", timestamp="2026-10-25T10:03:00Z", source={"collector": "synthetic/2", "event_ref": "another-record"}, process={"pid": 900, "parent_pid": 7, "executable": "synthetic-app"})
    report = compare(tmp_path, base, candidate, [left], [right])
    assert report["findings"] == []
    assert report["status"] == "complete"
    assert report["summary"]["baseline_behaviour_event_count"] == 1


def test_changed_network_destination_is_linked_to_both_inputs(tmp_path):
    base, candidate = pair()
    left = event()
    right = event("evt-new", resource={**event()["resource"], "address": "203.0.113.77", "port": 8443})
    report = compare(tmp_path, base, candidate, [left], [right])
    assert len(report["findings"]) == 1
    finding = report["findings"][0]
    assert finding["change_type"] == "changed"
    assert finding["rule_id"] == "DG-COMPARE-002"
    assert finding["baseline_evidence"][0]["event_id"] == "evt-001"
    assert finding["candidate_evidence"][0]["event_id"] == "evt-new"
    assert finding["candidate_evidence"][0]["observed"]["resource"]["address"] == "203.0.113.77"


def test_action_change_is_reported_as_added_and_removed(tmp_path):
    base, candidate = pair()
    report = compare(tmp_path, base, candidate, [event(action="connection_attempt")], [event("evt-new", action="connection_established")])
    assert {item["change_type"] for item in report["findings"]} == {"added", "removed"}


def test_duplicate_observations_are_multiset_matched(tmp_path):
    base, candidate = pair()
    report = compare(tmp_path, base, candidate, [event("evt-1"), event("evt-2")], [event("evt-3")])
    assert len(report["findings"]) == 1
    assert report["findings"][0]["change_type"] == "removed"
    assert len(report["findings"][0]["baseline_evidence"]) == 1


def test_coverage_events_are_not_behaviour_findings(tmp_path):
    base, candidate = pair()
    coverage = {"schema_version": 1, "event_id": "coverage-1", "timestamp": "2026-10-25T10:00:00Z", "category": "coverage", "action": "capture_started", "source": {"collector": "synthetic/1", "event_ref": "capture:start"}, "collection_quality": "complete", "attributes": {"scope": "fixture"}}
    report = compare(tmp_path, base, candidate, [coverage], [coverage])
    assert report["findings"] == []
    assert report["summary"]["baseline_behaviour_event_count"] == 0


def test_coverage_gap_produces_partial_report_and_uncertainty(tmp_path):
    base, candidate = pair()
    candidate["coverage"]["unavailable_categories"] = ["network"]
    candidate["coverage"]["event_sources"][1]["state"] = "unavailable"
    gap = {"schema_version": 1, "event_id": "coverage-gap", "timestamp": "2026-10-25T10:00:00Z", "category": "coverage", "action": "coverage_gap", "source": {"collector": "synthetic/1", "event_ref": "coverage:network"}, "collection_quality": "partial", "attributes": {"category": "network"}}
    report = compare(tmp_path, base, candidate, [event()], [event("evt-candidate"), gap])
    assert report["status"] == "partial"
    codes = {item["code"] for item in report["uncertainties"]}
    assert "categories_unavailable" in codes
    assert "coverage_events_present" in codes
    assert "events_with_reduced_quality" in codes


def test_context_mismatch_is_recorded_as_uncertainty(tmp_path):
    base, candidate = pair()
    candidate["environment"]["snapshot_id"] = "different-snapshot"
    report = compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])
    assert report["status"] == "partial"
    assert report["environment_differences"][0]["field"] == "environment.snapshot_id"


def test_rejects_different_experiments(tmp_path):
    base, candidate = pair()
    candidate["experiment_id"] = "other"
    with pytest.raises(ComparisonError, match="same experiment_id"):
        compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])


def test_rejects_different_application_package_ids(tmp_path):
    base, candidate = pair()
    candidate["application"]["package_id"] = "org.example.other"
    with pytest.raises(ComparisonError, match="different application package IDs"):
        compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])


def test_rejects_running_capture(tmp_path):
    base, candidate = pair()
    base["capture"]["status"] = "running"
    base["capture"]["ended_at"] = None
    with pytest.raises(ComparisonError, match="still running"):
        compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])


def test_output_is_deterministic_and_validates_against_report_schema(tmp_path):
    base, candidate = pair()
    first = compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])
    second = compare(tmp_path, base, candidate, [event()], [event("evt-candidate")])
    schema = json.loads(REPORT_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(first)
    assert first == second
    assert len(first["report_id"]) == 64


def test_invalid_event_input_is_reported_as_comparison_error(tmp_path):
    base, candidate = pair()
    base_manifest_path = tmp_path / "baseline-manifest.json"
    candidate_manifest_path = tmp_path / "candidate-manifest.json"
    base_events_path = tmp_path / "baseline-events.jsonl"
    candidate_events_path = tmp_path / "candidate-events.jsonl"
    write_json(base_manifest_path, base)
    write_json(candidate_manifest_path, candidate)
    write_jsonl(base_events_path, [event()])
    candidate_events_path.write_text("", encoding="utf-8")
    with pytest.raises(ComparisonError, match="invalid event stream"):
        compare_runs(base_manifest_path, candidate_manifest_path, base_events_path, candidate_events_path, MANIFEST_SCHEMA, EVENT_SCHEMA)
