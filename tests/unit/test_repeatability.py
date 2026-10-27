import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from driftglass.comparison import compare_loaded_runs
from driftglass.profiles import default_rule_profile
from driftglass.repeatability import (
    RepeatAnalysisError,
    analyze_repeat_reports,
    export_repeat_analysis,
    load_report_file,
    load_report_files,
    validate_repeat_analysis,
)

ROOT = Path(__file__).resolve().parents[2]
REPEAT_SCHEMA = ROOT / "schemas" / "reports" / "repeat-analysis-v1.schema.json"


def manifest(role, run_id, version, artifact_hash, *, capture_status="completed"):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "repeat-fixture",
        "role": role,
        "application": {
            "name": "Synthetic App",
            "version": version,
            "artifact_sha256": artifact_hash,
            "package_id": "org.example.synthetic",
        },
        "environment": {
            "os": "Linux",
            "os_version": "fixture",
            "architecture": "x86_64",
            "snapshot_id": "snapshot-1",
        },
        "capture": {
            "status": capture_status,
            "started_at": "2026-10-26T10:00:00Z",
            "ended_at": "2026-10-26T10:01:00Z",
            "scenario": "synthetic run",
            "profile": "fixture",
        },
        "coverage": {
            "event_sources": [
                {"name": "process-source", "state": "active", "categories": ["process"]},
                {"name": "network-source", "state": "active", "categories": ["network"]},
                {"name": "file-source", "state": "active", "categories": ["file"]},
                {"name": "system-source", "state": "active", "categories": ["system"]},
                {"name": "persistence-source", "state": "active", "categories": ["persistence"]},
            ],
            "unavailable_categories": [],
            "dropped_events": 0,
            "errors": [],
        },
    }


def event(event_id, minute=1):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": f"2026-10-26T10:{minute:02d}:00Z",
        "category": "network",
        "action": "connection_attempt",
        "source": {"collector": "synthetic/1", "event_ref": "source-" + event_id},
        "collection_quality": "complete",
        "process": {
            "pid": 100 + minute,
            "parent_pid": 1,
            "executable": "synthetic-app",
        },
        "resource": {
            "kind": "network",
            "identifier": "192.0.2.2:50000",
            "address": "198.51.100.10",
            "port": 443,
            "protocol": "tcp",
        },
        "attributes": {
            "socket_state": "ESTABLISHED",
            "process_start_time_ticks": 1000 + minute,
        },
    }


def report(index, *, observed=True, capture_status="completed", candidate_version="2.0"):
    baseline = manifest("baseline", f"baseline-{index}", "1.0", "a" * 64)
    candidate = manifest(
        "candidate",
        f"candidate-{index}",
        candidate_version,
        "b" * 64,
        capture_status=capture_status,
    )
    events = [event(f"candidate-event-{index}", index)] if observed else []
    return compare_loaded_runs(baseline, candidate, [], events, default_rule_profile())


def test_repeat_analysis_marks_patterns_recurring_only_when_present_everywhere():
    reports = [report(1), report(2), report(3, observed=False, capture_status="incomplete")]

    analysis = analyze_repeat_reports(reports)
    validate_repeat_analysis(analysis, REPEAT_SCHEMA)

    assert analysis["report_count"] == 3
    assert analysis["summary"]["partial_report_count"] == 1
    assert analysis["partial_report_ids"] == [reports[2]["report_id"]]
    assert len(analysis["patterns"]) == 1
    pattern = analysis["patterns"][0]
    assert pattern["repeatability"] == "not_consistently_observed"
    assert pattern["occurrence_count"] == 2
    assert {item["report_id"] for item in pattern["report_refs"]} == {
        reports[0]["report_id"], reports[1]["report_id"]
    }


def test_repeat_analysis_is_input_order_independent_and_links_findings():
    reports = [report(4), report(5)]
    first = analyze_repeat_reports(reports)
    second = analyze_repeat_reports(list(reversed(reports)))

    assert first == second
    assert first["summary"]["recurring_pattern_count"] == 1
    pattern = first["patterns"][0]
    assert pattern["repeatability"] == "recurring"
    assert all(item["finding_ids"] for item in pattern["report_refs"])


def test_repeat_analysis_rejects_duplicate_reports_and_incompatible_cohorts():
    one = report(6)
    with pytest.raises(RepeatAnalysisError, match="same report more than once"):
        analyze_repeat_reports([one, one])

    with pytest.raises(RepeatAnalysisError, match="same application"):
        analyze_repeat_reports([report(7), report(8, candidate_version="3.0")])


def test_repeat_analysis_export_validates_and_refuses_overwrite(tmp_path):
    analysis = analyze_repeat_reports([report(9), report(10)])
    output = tmp_path / "repeat-analysis.json"

    assert export_repeat_analysis(analysis, REPEAT_SCHEMA, output) == output
    assert json.loads(output.read_text(encoding="utf-8")) == analysis
    with pytest.raises(RepeatAnalysisError, match="already exists"):
        export_repeat_analysis(analysis, REPEAT_SCHEMA, output)
    damaged = dict(analysis)
    damaged["analysis_id"] = "0" * 64
    with pytest.raises(RepeatAnalysisError, match="analysis_id"):
        validate_repeat_analysis(damaged, REPEAT_SCHEMA)



def test_report_loader_rejects_oversized_json_before_parsing(tmp_path, monkeypatch):
    path = tmp_path / "oversized-report.json"
    path.write_text('{"schema_version":1}', encoding="utf-8")
    monkeypatch.setattr("driftglass.repeatability.MAX_REPORT_FILE_BYTES", 8)

    with pytest.raises(RepeatAnalysisError, match="exceeds the 8-byte file limit"):
        load_report_file(path, ROOT / "schemas" / "reports" / "report-v1.schema.json")

def test_repeat_report_inputs_enforce_aggregate_byte_limit(tmp_path, monkeypatch):
    paths = []
    for index in (20, 21):
        path = tmp_path / f"report-{index}.json"
        path.write_text(json.dumps(report(index)), encoding="utf-8")
        paths.append(path)

    combined_size = sum(path.stat().st_size for path in paths)
    monkeypatch.setattr("driftglass.repeatability.MAX_REPEAT_REPORT_BYTES", combined_size - 1)

    with pytest.raises(RepeatAnalysisError, match="combined repeat-analysis input exceeds"):
        load_report_files(paths, ROOT / "schemas" / "reports" / "report-v1.schema.json")


def test_repeat_schema_caps_source_reports_at_500():
    schema = json.loads(REPEAT_SCHEMA.read_text(encoding="utf-8"))
    reports = [
        {
            "report_id": f"{index:064x}",
            "status": "complete",
            "added_count": 0,
            "removed_count": 0,
            "changed_count": 0,
            "uncertainty_count": 0,
        }
        for index in range(501)
    ]
    value = {
        "schema_version": 1,
        "analysis_id": "b" * 64,
        "cohort": {
            "application_name": "Synthetic",
            "baseline_version": "1.0",
            "candidate_version": "2.0",
            "baseline_artifact_sha256": None,
            "candidate_artifact_sha256": None,
            "comparison_profile_sha256": None,
        },
        "report_count": 501,
        "reports": reports,
        "partial_report_ids": [],
        "patterns": [],
        "summary": {
            "recurring_pattern_count": 0,
            "not_consistently_observed_pattern_count": 0,
            "partial_report_count": 0,
        },
    }

    errors = list(Draft202012Validator(schema).iter_errors(value))
    assert any(
        error.validator == "maxItems" and list(error.absolute_path) == ["reports"]
        for error in errors
    )

    value["reports"] = reports[:2]
    errors = list(Draft202012Validator(schema).iter_errors(value))
    assert any(
        error.validator == "maximum" and list(error.absolute_path) == ["report_count"]
        for error in errors
    )
