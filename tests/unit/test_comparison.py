import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, FormatChecker

from driftglass.comparison import ComparisonError, compare_loaded_runs, compare_runs
from driftglass.profiles import default_rule_profile, load_rule_profile


ROOT = Path(__file__).resolve().parents[2]
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"
PROFILE_SCHEMA = ROOT / "schemas" / "rules" / "rule-profile-v1.schema.json"


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
        "attributes": {"socket_state": "ESTABLISHED", "process_start_time_ticks": 1000}
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


def test_network_local_source_port_changes_do_not_create_findings(tmp_path):
    base, candidate = pair()
    resource = event()["resource"]
    left = event(
        "base-network",
        resource={**resource, "identifier": "192.0.2.2:51000"},
    )
    right = event(
        "candidate-network",
        timestamp="2026-10-25T10:03:00Z",
        source={"collector": "synthetic/2", "event_ref": "candidate-record"},
        process={"pid": 900, "parent_pid": 7, "executable": "synthetic-app"},
        attributes={"socket_state": "ESTABLISHED", "process_start_time_ticks": 9000},
        resource={**resource, "identifier": "192.0.2.2:61000"},
    )

    report = compare(tmp_path, base, candidate, [left], [right])

    assert report["findings"] == []
    assert report["summary"]["added_count"] == 0
    assert report["summary"]["removed_count"] == 0
    assert report["summary"]["changed_count"] == 0
    # Volatile identity is ignored for matching, not stripped from evidence.
    assert report["status"] == "complete"


def noisy_variant(original, event_id, minute, pid, *, address=None):
    resource = dict(original["resource"])
    if address is not None:
        resource["address"] = address
    return {
        **original,
        "event_id": event_id,
        "timestamp": f"2026-10-25T10:{minute:02d}:00Z",
        "source": {"collector": "synthetic/2", "event_ref": f"source-{event_id}"},
        "process": {**original["process"], "pid": pid, "parent_pid": 7},
        "attributes": {**original["attributes"], "process_start_time_ticks": pid * 100},
        "resource": resource,
    }


def test_noisy_equivalent_runs_do_not_create_findings_when_event_order_changes(tmp_path):
    base, candidate = pair()
    baseline_events = [
        event("base-a", resource={**event()["resource"], "identifier": "socket-a", "address": "198.51.100.10"}),
        event("base-b", resource={**event()["resource"], "identifier": "socket-b", "address": "198.51.100.11"}),
        event("base-c", resource={**event()["resource"], "identifier": "socket-c", "address": "198.51.100.12"}),
    ]
    candidate_events = [
        noisy_variant(baseline_events[2], "candidate-c", 3, 903),
        noisy_variant(baseline_events[0], "candidate-a", 1, 901),
        noisy_variant(baseline_events[1], "candidate-b", 2, 902),
    ]

    report = compare(tmp_path, base, candidate, baseline_events, candidate_events)

    assert report["findings"] == []
    assert report["summary"]["added_count"] == 0
    assert report["summary"]["removed_count"] == 0
    assert report["summary"]["changed_count"] == 0
    assert report["status"] == "complete"


def test_real_destination_change_survives_noise_in_other_observations(tmp_path):
    base, candidate = pair()
    baseline_events = [
        event("base-a", resource={**event()["resource"], "identifier": "socket-a", "address": "198.51.100.10"}),
        event("base-b", resource={**event()["resource"], "identifier": "socket-b", "address": "198.51.100.11"}),
        event("base-c", resource={**event()["resource"], "identifier": "socket-c", "address": "198.51.100.12"}),
    ]
    candidate_events = [
        noisy_variant(baseline_events[2], "candidate-c", 3, 903),
        noisy_variant(baseline_events[0], "candidate-a", 1, 901, address="203.0.113.77"),
        noisy_variant(baseline_events[1], "candidate-b", 2, 902),
    ]

    report = compare(tmp_path, base, candidate, baseline_events, candidate_events)

    assert len(report["findings"]) == 1
    finding = report["findings"][0]
    assert finding["change_type"] == "changed"
    assert finding["baseline_evidence"][0]["event_id"] == "base-a"
    assert finding["candidate_evidence"][0]["event_id"] == "candidate-a"
    assert finding["candidate_evidence"][0]["observed"]["resource"]["address"] == "203.0.113.77"
    assert finding["baseline_evidence"][0]["observed"]["resource"]["identifier"] == "192.0.2.2:51000"
    assert finding["candidate_evidence"][0]["observed"]["resource"]["identifier"] == "192.0.2.2:61000"
    assert report["summary"]["added_count"] == 0
    assert report["summary"]["removed_count"] == 0
    assert report["summary"]["changed_count"] == 1


def test_changed_network_destination_is_linked_to_both_inputs(tmp_path):
    base, candidate = pair()
    left = event()
    right = event("evt-new", resource={
        **event()["resource"],
        "identifier": "192.0.2.2:61000",
        "address": "203.0.113.77",
        "port": 8443,
    })
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



def test_process_identity_is_linked_to_evidence_without_changing_behaviour_matching(tmp_path):
    base, candidate = pair()
    baseline = event(
        "base-process",
        category="process",
        action="process_observed",
        process={"pid": 100, "parent_pid": 1, "executable": "synthetic-child"},
        attributes={"process_start_time_ticks": 1234},
    )
    candidate_event = event(
        "candidate-process",
        timestamp="2026-10-25T10:02:00Z",
        category="process",
        action="process_created",
        process={"pid": 900, "parent_pid": 7, "executable": "synthetic-child"},
        attributes={"process_start_time_ticks": 9876},
    )
    report = compare(tmp_path, base, candidate, [baseline], [candidate_event])

    identities = [
        evidence["observed"]["process_identity"]
        for finding in report["findings"]
        for evidence in finding["baseline_evidence"] + finding["candidate_evidence"]
        if "process_identity" in evidence["observed"]
    ]
    assert any(item == {
        "pid": 100, "parent_pid": 1, "executable": "synthetic-child", "start_time_ticks": 1234
    } for item in identities)
    assert any(item == {
        "pid": 900, "parent_pid": 7, "executable": "synthetic-child", "start_time_ticks": 9876
    } for item in identities)
    assert all(
        "process_start_time_ticks" not in evidence["observed"]["attributes"]
        for finding in report["findings"]
        for evidence in finding["baseline_evidence"] + finding["candidate_evidence"]
    )


def test_process_start_ticks_do_not_turn_equivalent_behaviour_into_a_difference(tmp_path):
    base, candidate = pair()
    left = event(
        "evt-left",
        process={"pid": 100, "parent_pid": 1, "executable": "synthetic-app"},
        attributes={"socket_state": "ESTABLISHED", "process_start_time_ticks": 1234},
    )
    right = event(
        "evt-right",
        timestamp="2026-10-25T10:02:00Z",
        process={"pid": 900, "parent_pid": 7, "executable": "synthetic-app"},
        attributes={"socket_state": "ESTABLISHED", "process_start_time_ticks": 9876},
    )

    report = compare(tmp_path, base, candidate, [left], [right])

    assert report["findings"] == []



def test_report_preserves_declared_coverage_and_counts_received_records(tmp_path):
    base, candidate = pair()
    base["coverage"]["unavailable_categories"] = ["persistence"]
    base["coverage"]["errors"] = ["synthetic permission note"]
    candidate["coverage"]["event_sources"][0]["state"] = "inactive"
    candidate["coverage"]["unavailable_categories"] = sorted(
        set(candidate["coverage"]["unavailable_categories"])
        | set(candidate["coverage"]["event_sources"][0]["categories"])
    )
    candidate["coverage"]["dropped_events"] = 2
    candidate["coverage"]["errors"] = ["synthetic observer read error"]

    baseline_records = [event("base-network"), event("base-process", category="process")]
    candidate_records = [event("candidate-network"), event("candidate-gap", category="coverage")]

    report = compare(tmp_path, base, candidate, baseline_records, candidate_records)

    baseline_coverage = report["coverage"]["baseline"]
    candidate_coverage = report["coverage"]["candidate"]
    assert baseline_coverage["unavailable_categories"] == ["persistence"]
    assert baseline_coverage["errors"] == ["synthetic permission note"]
    assert baseline_coverage["recorded_event_counts"] == {
        "process": 1, "network": 1, "file": 0, "system": 0, "coverage": 0
    }
    assert any(source["state"] == "inactive" for source in candidate_coverage["event_sources"])
    assert candidate_coverage["dropped_events"] == 2
    assert candidate_coverage["errors"] == ["synthetic observer read error"]
    assert candidate_coverage["recorded_event_counts"]["network"] == 1
    assert candidate_coverage["recorded_event_counts"]["coverage"] == 1
    schema = json.loads(REPORT_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(report)


def test_coverage_source_order_is_normalized_for_deterministic_report_ids(tmp_path):
    base, candidate = pair()
    base["coverage"]["event_sources"].reverse()
    first = compare(tmp_path, base, candidate, [event()], [event("candidate-event")])
    base["coverage"]["event_sources"].reverse()
    second = compare(tmp_path, base, candidate, [event()], [event("candidate-event")])
    assert first["coverage"] == second["coverage"]
    assert first["report_id"] == second["report_id"]



def test_custom_profile_filters_categories_and_ignores_named_attributes(tmp_path):
    profile_file = tmp_path / "network-profile.json"
    profile_file.write_text(
        json.dumps({
            "schema_version": 1,
            "profile_id": "network-only",
            "included_categories": ["network"],
            "ignored_attributes": ["volatile_state"],
        }),
        encoding="utf-8",
    )
    profile = load_rule_profile(profile_file, PROFILE_SCHEMA)
    base, candidate = pair()
    before = event("before", attributes={
        "socket_state": "ESTABLISHED",
        "volatile_state": "old",
        "connection_tag": "before",
        "process_start_time_ticks": 1000,
    })
    after = event(
        "after",
        timestamp="2026-10-25T10:03:00Z",
        process={"pid": 900, "parent_pid": 7, "executable": "synthetic-app"},
        attributes={
            "socket_state": "ESTABLISHED",
            "volatile_state": "new",
            "connection_tag": "after",
            "process_start_time_ticks": 9000,
        },
    )
    process_only_candidate = event(
        "process-event",
        category="process",
        action="process_created",
        resource=None,
    )

    report = compare_loaded_runs(
        base, candidate, [before], [after, process_only_candidate], profile
    )

    assert [finding["change_type"] for finding in report["findings"]] == ["changed"]
    assert report["summary"]["candidate_event_count"] == 2
    assert report["summary"]["candidate_behaviour_event_count"] == 1
    assert report["comparison_profile"] == profile.report_metadata()
    finding = report["findings"][0]
    baseline_attributes = finding["baseline_evidence"][0]["observed"]["attributes"]
    candidate_attributes = finding["candidate_evidence"][0]["observed"]["attributes"]
    assert baseline_attributes["volatile_state"] == "old"
    assert candidate_attributes["volatile_state"] == "new"
    assert "process_created" not in " ".join(item["observed"]["action"] for item in finding["candidate_evidence"])


def test_default_profile_keeps_the_original_matching_behavior():
    base, candidate = pair()
    left = event("profile-before", attributes={"socket_state": "ESTABLISHED"})
    right = event(
        "profile-after",
        timestamp="2026-10-25T10:03:00Z",
        attributes={"socket_state": "CLOSE_WAIT"},
    )
    report = compare_loaded_runs(base, candidate, [left], [right])

    assert any(finding["change_type"] == "changed" for finding in report["findings"])
    assert report["comparison_profile"] == default_rule_profile().report_metadata()
