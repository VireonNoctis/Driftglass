"""Repeatability analysis over already-exported comparison reports."""

from __future__ import annotations

import hashlib
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

from ._schema import (
    canonical_json,
    finite_json_float,
    instance_path,
    load_validator,
    reject_json_constant,
    safe_error_reason,
    unique_json_object,
)
from .reporting import ReportExportError, validate_report, write_text_output


MAX_REPORT_FILE_BYTES = 64 * 1024 * 1024
MAX_REPEAT_REPORT_BYTES = 256 * 1024 * 1024


class RepeatAnalysisError(ValueError):
    """Raised when reports cannot be safely grouped or analysed."""


def _load_report_file(
    report_path: str | Path,
    report_schema_path: str | Path,
    *,
    max_bytes: int,
    too_large_message: str,
) -> tuple[dict[str, Any], int]:
    """Load a report under a caller-supplied byte limit and validate it."""
    path = Path(report_path).expanduser()
    try:
        with path.open("rb") as stream:
            raw = stream.read(max_bytes + 1)
        if len(raw) > max_bytes:
            raise RepeatAnalysisError(too_large_message)
        report = json.loads(
            raw.decode("utf-8"),
            parse_constant=reject_json_constant,
            parse_float=finite_json_float,
            object_pairs_hook=unique_json_object,
        )
    except RepeatAnalysisError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        raise RepeatAnalysisError(
            f"could not read comparison report '{path}' ({type(exc).__name__})"
        ) from exc

    try:
        validate_report(report, report_schema_path)
    except ReportExportError as exc:
        raise RepeatAnalysisError(f"invalid comparison report '{path}': {exc}") from exc
    return report, len(raw)


def load_report_file(
    report_path: str | Path,
    report_schema_path: str | Path,
) -> dict[str, Any]:
    """Load one source report with the per-file byte limit."""
    report, _ = _load_report_file(
        report_path,
        report_schema_path,
        max_bytes=MAX_REPORT_FILE_BYTES,
        too_large_message=(
            f"comparison report exceeds the {MAX_REPORT_FILE_BYTES}-byte file limit"
        ),
    )
    return report


def load_report_files(
    report_paths: list[str | Path],
    report_schema_path: str | Path,
) -> list[dict[str, Any]]:
    """Load repeat-analysis inputs under per-file and aggregate byte limits."""
    if len(report_paths) > 500:
        raise RepeatAnalysisError("repeat analysis accepts at most 500 reports")

    reports: list[dict[str, Any]] = []
    total_bytes = 0
    for report_path in report_paths:
        remaining_bytes = MAX_REPEAT_REPORT_BYTES - total_bytes
        if remaining_bytes <= 0:
            raise RepeatAnalysisError(
                f"combined repeat-analysis input exceeds the {MAX_REPEAT_REPORT_BYTES}-byte limit"
            )
        file_limit = min(MAX_REPORT_FILE_BYTES, remaining_bytes)
        if remaining_bytes < MAX_REPORT_FILE_BYTES:
            too_large_message = (
                f"combined repeat-analysis input exceeds the {MAX_REPEAT_REPORT_BYTES}-byte limit"
            )
        else:
            too_large_message = (
                f"comparison report exceeds the {MAX_REPORT_FILE_BYTES}-byte file limit"
            )
        report, byte_count = _load_report_file(
            report_path,
            report_schema_path,
            max_bytes=file_limit,
            too_large_message=too_large_message,
        )
        reports.append(report)
        total_bytes += byte_count
    return reports


def _observed_signature(
    evidence: dict[str, Any], ignored_attributes: set[str]
) -> str:
    observed = dict(evidence["observed"])
    # PID/start-time identity helps build a within-run tree, but does not define
    # whether the same behaviour recurred in another run.
    observed.pop("process_identity", None)
    attributes = dict(observed.get("attributes") or {})
    attributes.pop("process_start_time_ticks", None)
    for key in ignored_attributes:
        attributes.pop(key, None)
    observed["attributes"] = attributes
    return canonical_json(observed)


def _finding_pattern(
    finding: dict[str, Any], ignored_attributes: set[str]
) -> str:
    core = {
        "rule_id": finding["rule_id"],
        "change_type": finding["change_type"],
        "category": finding["category"],
        "baseline_observations": sorted(
            _observed_signature(evidence, ignored_attributes) for evidence in finding["baseline_evidence"]
        ),
        "candidate_observations": sorted(
            _observed_signature(evidence, ignored_attributes) for evidence in finding["candidate_evidence"]
        ),
    }
    return hashlib.sha256(canonical_json(core).encode("utf-8")).hexdigest()


def _profile_digest(report: dict[str, Any]) -> str | None:
    profile = report.get("comparison_profile")
    return profile["sha256"] if isinstance(profile, dict) else None


def analyze_repeat_reports(reports: list[dict[str, Any]]) -> dict[str, Any]:
    """Aggregate finding patterns across compatible reports without making safety claims."""
    if len(reports) < 2:
        raise RepeatAnalysisError("repeat analysis requires at least two comparison reports")
    if len(reports) > 500:
        raise RepeatAnalysisError("repeat analysis accepts at most 500 reports")
    if any(not isinstance(report, dict) for report in reports):
        raise RepeatAnalysisError("each comparison report must be a JSON object")

    ordered_reports = sorted(reports, key=lambda report: str(report.get("report_id", "")))
    report_ids = [report.get("report_id") for report in ordered_reports]
    if any(not isinstance(report_id, str) for report_id in report_ids):
        raise RepeatAnalysisError("every comparison report must have a validated report_id")
    if len(set(report_ids)) != len(report_ids):
        raise RepeatAnalysisError("repeat analysis cannot contain the same report more than once")

    first = ordered_reports[0]
    baseline = first.get("baseline")
    candidate = first.get("candidate")
    if not isinstance(baseline, dict) or not isinstance(candidate, dict):
        raise RepeatAnalysisError("comparison report baseline/candidate metadata is missing")

    cohort = {
        "application_name": first.get("application_name"),
        "baseline_version": baseline.get("version"),
        "candidate_version": candidate.get("version"),
        "baseline_artifact_sha256": baseline.get("artifact_sha256"),
        "candidate_artifact_sha256": candidate.get("artifact_sha256"),
        "comparison_profile_sha256": _profile_digest(first),
    }
    for report in ordered_reports:
        base = report.get("baseline")
        after = report.get("candidate")
        if not isinstance(base, dict) or not isinstance(after, dict):
            raise RepeatAnalysisError("comparison report baseline/candidate metadata is missing")
        current = {
            "application_name": report.get("application_name"),
            "baseline_version": base.get("version"),
            "candidate_version": after.get("version"),
            "baseline_artifact_sha256": base.get("artifact_sha256"),
            "candidate_artifact_sha256": after.get("artifact_sha256"),
            "comparison_profile_sha256": _profile_digest(report),
        }
        if current != cohort:
            raise RepeatAnalysisError(
                "all reports must use the same application, baseline/candidate versions, "
                "artifact hashes, and comparison profile"
            )

    # fingerprint -> report ID -> all source finding IDs for this pattern
    references: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    exemplars: dict[str, dict[str, Any]] = {}

    source_reports: list[dict[str, Any]] = []
    partial_report_ids: list[str] = []
    for report in ordered_reports:
        report_id = report["report_id"]
        if report.get("status") == "partial":
            partial_report_ids.append(report_id)

        summary = report.get("summary")
        findings = report.get("findings")
        uncertainties = report.get("uncertainties")
        if not isinstance(summary, dict) or not isinstance(findings, list) or not isinstance(uncertainties, list):
            raise RepeatAnalysisError(f"comparison report {report_id} is missing summary, findings, or uncertainties")

        source_reports.append({
            "report_id": report_id,
            "status": report["status"],
            "added_count": summary["added_count"],
            "removed_count": summary["removed_count"],
            "changed_count": summary["changed_count"],
            "uncertainty_count": len(uncertainties),
        })

        profile_metadata = report.get("comparison_profile") or {}
        ignored_attributes = set(profile_metadata.get("ignored_attributes", []))
        for finding in findings:
            fingerprint = _finding_pattern(finding, ignored_attributes)
            references[fingerprint][report_id].add(finding["finding_id"])
            if fingerprint not in exemplars:
                exemplars[fingerprint] = {
                    "rule_id": finding["rule_id"],
                    "change_type": finding["change_type"],
                    "category": finding["category"],
                    "summary": finding["summary"],
                    "observation": finding["observation"],
                }

    patterns: list[dict[str, Any]] = []
    total = len(ordered_reports)
    for fingerprint in sorted(references):
        by_report = references[fingerprint]
        report_refs = [
            {"report_id": report_id, "finding_ids": sorted(finding_ids)}
            for report_id, finding_ids in sorted(by_report.items())
        ]
        occurrence_count = len(report_refs)
        patterns.append({
            "fingerprint": fingerprint,
            **exemplars[fingerprint],
            "occurrence_count": occurrence_count,
            "repeatability": "recurring" if occurrence_count == total else "not_consistently_observed",
            "report_refs": report_refs,
        })

    summary = {
        "recurring_pattern_count": sum(item["repeatability"] == "recurring" for item in patterns),
        "not_consistently_observed_pattern_count": sum(
            item["repeatability"] == "not_consistently_observed" for item in patterns
        ),
        "partial_report_count": len(partial_report_ids),
    }
    core = {
        "schema_version": 1,
        "cohort": cohort,
        "report_count": total,
        "reports": source_reports,
        "partial_report_ids": sorted(partial_report_ids),
        "patterns": patterns,
        "summary": summary,
    }
    analysis_id = hashlib.sha256(canonical_json(core).encode("utf-8")).hexdigest()
    return {"analysis_id": analysis_id, **core}


def validate_repeat_analysis(
    analysis: dict[str, Any],
    schema_path: str | Path,
) -> None:
    """Validate the aggregate schema and its content-derived analysis ID."""
    try:
        validator = load_validator(Path(schema_path), "repeat-analysis schema")
    except ValueError as exc:
        raise RepeatAnalysisError(str(exc)) from exc

    errors = sorted(
        validator.iter_errors(analysis),
        key=lambda error: (
            tuple(str(part) for part in error.absolute_path),
            str(error.validator),
            error.message,
        ),
    )
    if errors:
        error = errors[0]
        raise RepeatAnalysisError(
            f"{instance_path(error.absolute_path)}: {safe_error_reason(error)}"
        )
    reports = analysis["reports"]
    report_ids = [item["report_id"] for item in reports]
    if analysis["report_count"] != len(reports):
        raise RepeatAnalysisError("report_count does not match source reports")
    if len(report_ids) != len(set(report_ids)):
        raise RepeatAnalysisError("source report IDs must be unique")

    expected_partial_ids = sorted(
        item["report_id"] for item in reports if item["status"] == "partial"
    )
    if analysis["partial_report_ids"] != expected_partial_ids:
        raise RepeatAnalysisError("partial_report_ids do not match source report statuses")

    for pattern in analysis["patterns"]:
        refs = pattern["report_refs"]
        referenced_ids = [item["report_id"] for item in refs]
        if len(referenced_ids) != len(set(referenced_ids)):
            raise RepeatAnalysisError("a pattern cannot reference the same report more than once")
        if any(report_id not in set(report_ids) for report_id in referenced_ids):
            raise RepeatAnalysisError("a pattern references a report outside the analysis")
        if pattern["occurrence_count"] != len(refs):
            raise RepeatAnalysisError("pattern occurrence_count does not match report_refs")
        expected_repeatability = (
            "recurring" if pattern["occurrence_count"] == analysis["report_count"]
            else "not_consistently_observed"
        )
        if pattern["repeatability"] != expected_repeatability:
            raise RepeatAnalysisError("pattern repeatability label does not match its occurrence count")

    summary = analysis["summary"]
    expected_recurring = sum(item["repeatability"] == "recurring" for item in analysis["patterns"])
    expected_not_consistent = sum(
        item["repeatability"] == "not_consistently_observed" for item in analysis["patterns"]
    )
    if summary["recurring_pattern_count"] != expected_recurring:
        raise RepeatAnalysisError("summary.recurring_pattern_count does not match patterns")
    if summary["not_consistently_observed_pattern_count"] != expected_not_consistent:
        raise RepeatAnalysisError("summary.not_consistently_observed_pattern_count does not match patterns")
    if summary["partial_report_count"] != len(expected_partial_ids):
        raise RepeatAnalysisError("summary.partial_report_count does not match partial_report_ids")

    core = {key: value for key, value in analysis.items() if key != "analysis_id"}
    expected_id = hashlib.sha256(canonical_json(core).encode("utf-8")).hexdigest()
    if analysis["analysis_id"] != expected_id:
        raise RepeatAnalysisError("analysis_id does not match the repeat-analysis content")


def export_repeat_analysis(
    analysis: dict[str, Any],
    schema_path: str | Path,
    output_path: str | Path,
    *,
    overwrite: bool = False,
) -> Path:
    """Validate and safely export a repeat-analysis v1 JSON file."""
    validate_repeat_analysis(analysis, schema_path)
    content = json.dumps(analysis, sort_keys=True, indent=2, ensure_ascii=False) + "\n"
    try:
        return write_text_output(output_path, content, overwrite)
    except ReportExportError as exc:
        raise RepeatAnalysisError(str(exc)) from exc
