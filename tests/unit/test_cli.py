import json
import sqlite3
from pathlib import Path

from driftglass.cli import main
from driftglass.reporting import validate_report


ROOT = Path(__file__).resolve().parents[2]
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"
REPEAT_SCHEMA = ROOT / "schemas" / "reports" / "repeat-analysis-v1.schema.json"


def manifest(role, run_id, version, artifact_hash):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "cli-test",
        "role": role,
        "application": {"name": "CLI Fixture", "version": version, "artifact_sha256": artifact_hash, "package_id": "org.example.cli"},
        "environment": {"os": "Linux", "os_version": "fixture", "architecture": "x86_64", "snapshot_id": "cli-snapshot"},
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


def event(event_id, action):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-25T10:00:30Z",
        "category": "network",
        "action": action,
        "source": {"collector": "cli-fixture/1", "event_ref": event_id},
        "collection_quality": "complete",
        "process": {"pid": 10, "parent_pid": 1, "executable": "cli-fixture"},
        "resource": {"kind": "network", "identifier": "192.0.2.2:50000", "address": "198.51.100.10", "port": 443, "protocol": "tcp"},
        "attributes": {"state": "ESTABLISHED"}
    }


def write_json(path, value):
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")


def write_jsonl(path, values):
    path.write_text("".join(json.dumps(value) + "\n" for value in values), encoding="utf-8")


def captured_inputs(tmp_path):
    base_manifest = tmp_path / "base.json"
    candidate_manifest = tmp_path / "candidate.json"
    base_events = tmp_path / "base.jsonl"
    candidate_events = tmp_path / "candidate.jsonl"
    write_json(base_manifest, manifest("baseline", "run-base", "1.0", "a" * 64))
    write_json(candidate_manifest, manifest("candidate", "run-candidate", "2.0", "b" * 64))
    write_jsonl(base_events, [event("e-base", "connection_attempt")])
    write_jsonl(candidate_events, [event("e-candidate", "connection_established")])
    return base_manifest, candidate_manifest, base_events, candidate_events


def test_events_validate_reports_count_without_printing_event_values(tmp_path, capsys):
    events_path = tmp_path / "events.jsonl"
    write_jsonl(events_path, [event("evt", "connection_attempt")])
    result = main(["events", "validate", str(events_path), "--event-schema", str(EVENT_SCHEMA)])
    output = capsys.readouterr()
    assert result == 0
    assert "valid event stream: 1 record(s)" in output.out
    assert "198.51.100.10" not in output.out


def test_manifest_validate_reports_identity_and_capture_state(tmp_path, capsys):
    manifest_path = tmp_path / "manifest.json"
    write_json(manifest_path, manifest("baseline", "run-base", "1.0", "a" * 64))
    result = main(["manifest", "validate", str(manifest_path), "--manifest-schema", str(MANIFEST_SCHEMA)])
    assert result == 0
    output = capsys.readouterr().out
    assert "run_id=run-base" in output
    assert "role=baseline" in output
    assert "capture=completed" in output


def test_compare_exports_and_stores_local_report(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    json_output = tmp_path / "comparison.json"
    html_output = tmp_path / "comparison.html"
    database = tmp_path / "history.db"
    result = main([
        "compare", "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events), "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA), "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA), "--json-output", str(json_output),
        "--html-output", str(html_output), "--history-db", str(database),
    ])
    assert result == 0
    output = capsys.readouterr().out
    report = json.loads(json_output.read_text(encoding="utf-8"))
    assert f"report_id={report['report_id']}" in output
    assert report["summary"]["added_count"] == 1
    assert report["summary"]["removed_count"] == 1
    assert "Uncertainty and coverage gaps" in html_output.read_text(encoding="utf-8")


def test_history_list_show_and_export_workflow(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    database = tmp_path / "history.db"
    assert main([
        "compare", "--baseline-manifest", str(base_manifest), "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events), "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA), "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA), "--history-db", str(database)
    ]) == 0
    report_id = capsys.readouterr().out.split("report_id=", 1)[1].split()[0]

    assert main(["history", "list", "--database", str(database)]) == 0
    assert report_id in capsys.readouterr().out

    assert main(["history", "show", "--database", str(database), "--report-id", report_id, "--report-schema", str(REPORT_SCHEMA)]) == 0
    shown = json.loads(capsys.readouterr().out)
    assert shown["report_id"] == report_id

    export_path = tmp_path / "from-history.html"
    assert main([
        "history", "export", "--database", str(database), "--report-id", report_id,
        "--format", "html", "--output", str(export_path), "--report-schema", str(REPORT_SCHEMA)
    ]) == 0
    assert export_path.exists()


def test_compare_refuses_to_overwrite_input_capture(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    original = base_events.read_text(encoding="utf-8")
    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--json-output", str(base_events),
        "--overwrite",
    ])

    assert result == 2
    assert base_events.read_text(encoding="utf-8") == original
    assert "JSON report output must not target the same path as baseline events" in capsys.readouterr().err


def test_compare_refuses_shared_json_and_history_output_path(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    shared_output = tmp_path / "shared-output"
    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--json-output", str(shared_output),
        "--history-db", str(shared_output),
    ])

    assert result == 2
    assert not shared_output.exists()
    assert "must use different paths" in capsys.readouterr().err


def test_default_schema_root_contains_all_contracts():
    from driftglass.cli import EVENT_SCHEMA as default_event_schema
    from driftglass.cli import MANIFEST_SCHEMA as default_manifest_schema
    from driftglass.cli import REPORT_SCHEMA as default_report_schema
    from driftglass.cli import REPEAT_ANALYSIS_SCHEMA as default_repeat_schema
    from driftglass.cli import RULE_PROFILE_SCHEMA as default_profile_schema

    assert default_event_schema.is_file()
    assert default_manifest_schema.is_file()
    assert default_report_schema.is_file()
    assert default_repeat_schema.is_file()
    assert default_profile_schema.is_file()


def test_history_export_refuses_history_database_as_output(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    database = tmp_path / "history.db"
    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events), "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA), "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA), "--history-db", str(database),
    ])
    assert result == 0
    capsys.readouterr()

    # The database remains the protected target regardless of whether the report ID is known.
    connection = sqlite3.connect(database)
    stored_id = connection.execute("SELECT report_id FROM reports LIMIT 1").fetchone()[0]
    connection.close()
    result = main([
        "history", "export", "--database", str(database), "--report-id", stored_id,
        "--format", "json", "--output", str(database), "--report-schema", str(REPORT_SCHEMA),
        "--overwrite",
    ])

    assert result == 2
    assert "export output must not target the same path as history database" in capsys.readouterr().err
    connection = sqlite3.connect(database)
    assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 1
    connection.close()


def test_invalid_input_returns_nonzero_and_writes_error_to_stderr(tmp_path, capsys):
    result = main(["events", "validate", str(tmp_path / "missing.jsonl"), "--event-schema", str(EVENT_SCHEMA)])
    output = capsys.readouterr()
    assert result == 2
    assert "could not read event file" in output.err



def test_redacted_export_is_schema_valid_and_history_keeps_original(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    json_output = tmp_path / "comparison-redacted.json"
    database = tmp_path / "history.db"

    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--json-output", str(json_output),
        "--history-db", str(database),
        "--redact-network",
    ])

    assert result == 0
    output = capsys.readouterr().out
    exported = json.loads(json_output.read_text(encoding="utf-8"))
    exported_text = json.dumps(exported)
    validate_report(exported, REPORT_SCHEMA)
    assert "198.51.100.10" not in exported_text
    assert "192.0.2.2" not in exported_text
    assert "[REDACTED_HOST_" in exported_text
    assert "source_report_id=" in output
    assert "exported_report_id=" in output
    assert exported["report_id"] != output.split("source_report_id=", 1)[1].split()[0]

    connection = sqlite3.connect(database)
    stored_json = connection.execute("SELECT report_json FROM reports LIMIT 1").fetchone()[0]
    connection.close()
    assert "198.51.100.10" in stored_json
    assert json.loads(stored_json)["report_id"] == output.split("source_report_id=", 1)[1].split()[0]


def test_redaction_requires_file_export_and_does_not_create_history(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    database = tmp_path / "history.db"

    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--history-db", str(database),
        "--redact-network",
    ])

    assert result == 2
    assert "redaction flags require" in capsys.readouterr().err
    assert not database.exists()



def test_history_delete_requires_exact_confirmation_and_removes_only_database_rows(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    database = tmp_path / "history.db"
    assert main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--history-db", str(database),
    ]) == 0
    capsys.readouterr()

    connection = sqlite3.connect(database)
    report_id = connection.execute("SELECT report_id FROM reports LIMIT 1").fetchone()[0]
    connection.close()

    result = main([
        "history", "delete",
        "--database", str(database),
        "--report-id", report_id,
        "--confirm", "wrong-confirmation",
    ])
    assert result == 2
    assert "nothing was deleted" in capsys.readouterr().err
    connection = sqlite3.connect(database)
    assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 1
    connection.close()

    result = main([
        "history", "delete",
        "--database", str(database),
        "--report-id", report_id,
        "--confirm", report_id,
    ])
    assert result == 0
    output = capsys.readouterr().out
    assert "source capture files unchanged" in output
    connection = sqlite3.connect(database)
    assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 0
    connection.close()



def test_compare_accepts_custom_rule_profile_and_records_its_digest(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    profile_path = tmp_path / "network-review.json"
    write_json(profile_path, {
        "schema_version": 1,
        "profile_id": "network-review",
        "included_categories": ["network"],
        "ignored_attributes": ["state"],
    })
    output_path = tmp_path / "network-report.json"

    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--rule-profile", str(profile_path),
        "--json-output", str(output_path),
    ])

    assert result == 0
    capsys.readouterr()
    report = json.loads(output_path.read_text(encoding="utf-8"))
    assert report["comparison_profile"]["profile_id"] == "network-review"
    assert report["comparison_profile"]["included_categories"] == ["network"]
    assert report["comparison_profile"]["ignored_attributes"] == ["state"]
    assert len(report["comparison_profile"]["sha256"]) == 64


def test_repeat_cli_aggregates_distinct_comparison_reports(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    first_output = tmp_path / "first-report.json"
    second_output = tmp_path / "second-report.json"

    common_args = [
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
    ]
    assert main(common_args + ["--json-output", str(first_output)]) == 0
    capsys.readouterr()

    base_value = json.loads(base_manifest.read_text(encoding="utf-8"))
    candidate_value = json.loads(candidate_manifest.read_text(encoding="utf-8"))
    base_value["run_id"] = "run-base-repeat-2"
    candidate_value["run_id"] = "run-candidate-repeat-2"
    write_json(base_manifest, base_value)
    write_json(candidate_manifest, candidate_value)

    baseline_records = [json.loads(line) for line in base_events.read_text(encoding="utf-8").splitlines()]
    candidate_records = [json.loads(line) for line in candidate_events.read_text(encoding="utf-8").splitlines()]
    for record in baseline_records + candidate_records:
        record["event_id"] += "-repeat-2"
        record["source"]["event_ref"] += "-repeat-2"
    write_jsonl(base_events, baseline_records)
    write_jsonl(candidate_events, candidate_records)

    assert main(common_args + ["--json-output", str(second_output)]) == 0
    capsys.readouterr()

    analysis_path = tmp_path / "repeat-analysis.json"
    result = main([
        "repeat",
        "--reports", str(first_output), str(second_output),
        "--report-schema", str(REPORT_SCHEMA),
        "--analysis-schema", str(REPEAT_SCHEMA),
        "--json-output", str(analysis_path),
    ])
    assert result == 0
    assert "reports=2" in capsys.readouterr().out
    analysis = json.loads(analysis_path.read_text(encoding="utf-8"))
    assert analysis["report_count"] == 2
    assert analysis["summary"]["recurring_pattern_count"] == 2
    assert all(pattern["repeatability"] == "recurring" for pattern in analysis["patterns"])



def test_repeat_cli_requires_two_reports_before_reading_inputs(tmp_path, capsys):
    result = main(["repeat", "--reports", str(tmp_path / "missing.json")])
    assert result == 2
    assert "at least two comparison reports" in capsys.readouterr().err


def test_repeat_cli_overwrite_requires_json_output(tmp_path, capsys):
    result = main([
        "repeat",
        "--reports", str(tmp_path / "missing-a.json"), str(tmp_path / "missing-b.json"),
        "--overwrite",
    ])
    assert result == 2
    assert "--overwrite requires --json-output" in capsys.readouterr().err



def test_report_validate_checks_schema_and_content_hash(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    report_path = tmp_path / "comparison.json"
    result = main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--json-output", str(report_path),
    ])
    assert result == 0
    capsys.readouterr()

    result = main([
        "report", "validate", str(report_path),
        "--report-schema", str(REPORT_SCHEMA),
    ])
    output = capsys.readouterr()
    assert result == 0
    assert "valid comparison report:" in output.out
    assert "findings=2" in output.out
    assert "198.51.100.10" not in output.out


def test_report_validate_rejects_a_tampered_content_hash(tmp_path, capsys):
    base_manifest, candidate_manifest, base_events, candidate_events = captured_inputs(tmp_path)
    report_path = tmp_path / "comparison.json"
    assert main([
        "compare",
        "--baseline-manifest", str(base_manifest),
        "--candidate-manifest", str(candidate_manifest),
        "--baseline-events", str(base_events),
        "--candidate-events", str(candidate_events),
        "--manifest-schema", str(MANIFEST_SCHEMA),
        "--event-schema", str(EVENT_SCHEMA),
        "--report-schema", str(REPORT_SCHEMA),
        "--json-output", str(report_path),
    ]) == 0
    capsys.readouterr()

    report = json.loads(report_path.read_text(encoding="utf-8"))
    report["summary"]["added_count"] += 1
    write_json(report_path, report)

    result = main([
        "report", "validate", str(report_path),
        "--report-schema", str(REPORT_SCHEMA),
    ])
    output = capsys.readouterr()
    assert result == 2
    assert "report_id does not match the report content" in output.err
