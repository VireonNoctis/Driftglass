import json
from pathlib import Path

import pytest

from driftglass._schema import canonical_json
from driftglass.comparison import compare_loaded_runs
from driftglass.reporting import ReportExportError, export_html_report, export_json_report, render_html_report, validate_report


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


@pytest.mark.parametrize(
    ("status", "uncertainties", "expected_status"),
    [
        ("complete", [{"code": "synthetic_gap", "message": "A capture gap was reported.", "run_id": None}], "partial"),
        ("partial", [], "complete"),
    ],
)
def test_report_status_matches_declared_uncertainties(status, uncertainties, expected_status):
    report = make_report()
    report["status"] = status
    report["uncertainties"] = uncertainties
    report["summary"]["uncertainty_count"] = len(uncertainties)

    with pytest.raises(ReportExportError, match=expected_status):
        validate_report(report, REPORT_SCHEMA)


def test_incomplete_capture_cannot_be_marked_complete():
    report = make_report()
    report["candidate"]["capture_status"] = "failed"

    with pytest.raises(ReportExportError):
        validate_report(report, REPORT_SCHEMA)


def test_incomplete_capture_requires_uncertainty_for_that_run():
    report = make_report()
    report["candidate"]["capture_status"] = "incomplete"
    report["status"] = "partial"
    report["uncertainties"] = [{
        "code": "capture_not_completed",
        "message": "Synthetic incomplete capture.",
        "run_id": report["baseline"]["run_id"],
    }]
    report["summary"]["uncertainty_count"] = 1

    with pytest.raises(
        ReportExportError,
        match="candidate capture status requires a matching capture_not_completed uncertainty",
    ):
        validate_report(report, REPORT_SCHEMA)


def test_incomplete_capture_with_matching_uncertainty_is_valid():
    base = manifest("baseline", "run-base", "1.0", "a" * 64)
    candidate = manifest("candidate", "run-candidate", "2.0", "b" * 64)
    candidate["capture"]["status"] = "incomplete"
    report = compare_loaded_runs(base, candidate, [event("evt-base")], [event("evt-candidate")])

    assert report["status"] == "partial"
    assert any(
        item["code"] == "capture_not_completed" and item["run_id"] == "run-candidate"
        for item in report["uncertainties"]
    )
    validate_report(report, REPORT_SCHEMA)


def test_coverage_snapshot_gap_requires_matching_uncertainty():
    report = make_report()
    report["coverage"]["baseline"]["dropped_events"] = 1

    with pytest.raises(
        ReportExportError,
        match="baseline coverage snapshot does not match uncertainty codes.*events_dropped",
    ):
        validate_report(report, REPORT_SCHEMA)


def test_behaviour_event_count_matches_profile_categories_in_coverage():
    report = make_report()
    report["summary"]["baseline_behaviour_event_count"] = 0

    with pytest.raises(
        ReportExportError,
        match="baseline behaviour event count does not match coverage counts",
    ):
        validate_report(report, REPORT_SCHEMA)


def test_coverage_record_count_must_match_report_summary():
    report = make_report()
    report["coverage"]["baseline"]["recorded_event_counts"]["network"] += 1

    with pytest.raises(
        ReportExportError,
        match="baseline coverage snapshot record count does not match summary.baseline_event_count",
    ):
        validate_report(report, REPORT_SCHEMA)


def test_coverage_uncertainty_must_reference_the_run_with_the_gap():
    report = make_report()
    report["coverage"]["baseline"]["errors"] = ["Synthetic collector error"]
    report["status"] = "partial"
    report["uncertainties"] = [{
        "code": "collection_errors",
        "message": "Synthetic collection error.",
        "run_id": report["candidate"]["run_id"],
    }]
    report["summary"]["uncertainty_count"] = 1

    with pytest.raises(
        ReportExportError,
        match="baseline coverage snapshot does not match uncertainty codes.*collection_errors",
    ):
        validate_report(report, REPORT_SCHEMA)


def test_coverage_snapshot_with_matching_uncertainty_is_valid():
    base = manifest("baseline", "run-base", "1.0", "a" * 64)
    candidate = manifest("candidate", "run-candidate", "2.0", "b" * 64)
    base["coverage"]["dropped_events"] = 1
    report = compare_loaded_runs(base, candidate, [event("evt-base")], [event("evt-candidate")])

    assert report["status"] == "partial"
    assert any(
        item["code"] == "events_dropped" and item["run_id"] == "run-base"
        for item in report["uncertainties"]
    )
    validate_report(report, REPORT_SCHEMA)


def test_report_summary_counts_match_findings():
    report = make_report()
    report["summary"]["added_count"] += 1

    with pytest.raises(ReportExportError, match="summary.added_count does not match"):
        validate_report(report, REPORT_SCHEMA)


def test_evidence_run_id_must_match_its_report_side():
    report = make_report(executable="candidate-app")
    candidate_evidence = next(
        finding["candidate_evidence"][0]
        for finding in report["findings"]
        if finding["candidate_evidence"]
    )
    candidate_evidence["run_id"] = report["baseline"]["run_id"]

    with pytest.raises(ReportExportError, match="candidate_evidence references an unexpected run_id"):
        validate_report(report, REPORT_SCHEMA)


def test_evidence_category_must_match_finding_category():
    report = make_report(executable="candidate-app")
    candidate_evidence = next(
        finding["candidate_evidence"][0]
        for finding in report["findings"]
        if finding["candidate_evidence"]
    )
    candidate_evidence["observed"]["category"] = "process"

    with pytest.raises(ReportExportError, match="candidate_evidence category does not match"):
        validate_report(report, REPORT_SCHEMA)


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



def test_html_report_includes_escaped_per_run_coverage_and_input_counts():
    report = make_report()
    report["coverage"]["baseline"]["errors"] = ["synthetic note <script>alert(1)</script>"]
    report["status"] = "partial"
    report["uncertainties"] = [{
        "code": "collection_errors",
        "message": "A synthetic collection error was reported.",
        "run_id": report["baseline"]["run_id"],
    }]
    report["summary"]["uncertainty_count"] = 1
    report["report_id"] = __import__("hashlib").sha256(
        canonical_json({key: value for key, value in report.items() if key != "report_id"}).encode("utf-8")
    ).hexdigest()

    html_report = render_html_report(report, REPORT_SCHEMA)

    assert "Declared collection coverage" in html_report
    assert "Baseline" in html_report and "Candidate" in html_report
    source_name = report["coverage"]["baseline"]["event_sources"][0]["name"]
    assert source_name in html_report
    assert "Input records" in html_report
    assert "<script>alert(1)</script>" not in html_report
    assert "&lt;script&gt;alert(1)&lt;/script&gt;" in html_report


def test_html_report_keeps_older_v1_reports_without_coverage_viewable():
    report = make_report()
    report.pop("coverage")
    core = {key: value for key, value in report.items() if key != "report_id"}
    report["report_id"] = __import__("hashlib").sha256(canonical_json(core).encode("utf-8")).hexdigest()
    html_report = render_html_report(report, REPORT_SCHEMA)
    assert "does not include a per-run coverage snapshot" in html_report



def test_report_rejects_mismatched_comparison_profile_digest():
    report = make_report()
    report["comparison_profile"]["sha256"] = "0" * 64

    with pytest.raises(ReportExportError, match="comparison_profile.sha256"):
        validate_report(report, REPORT_SCHEMA)


def test_html_report_displays_the_applied_comparison_profile():
    report = make_report()
    html_report = render_html_report(report, REPORT_SCHEMA)

    assert "Comparison profile" in html_report
    assert "default-v1" in html_report
    assert report["comparison_profile"]["sha256"] in html_report
    assert "Included categories" in html_report
