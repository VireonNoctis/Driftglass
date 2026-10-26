"""Deterministic baseline/candidate comparison with evidence-linked findings."""

from __future__ import annotations

import hashlib
from collections import defaultdict
from pathlib import Path
from typing import Any

from ._schema import canonical_json
from .events import EventValidationError, load_jsonl
from .manifests import ManifestValidationError, load_manifest

COVERAGE_CATEGORIES = frozenset({"process", "network", "file", "system", "persistence"})


class ComparisonError(ValueError):
    """Raised when two runs cannot be meaningfully compared."""


def _process_name(event: dict[str, Any]) -> str | None:
    return (event.get("process") or {}).get("executable")


def _identity(event: dict[str, Any]) -> str:
    resource = event.get("resource") or {}
    return canonical_json({
        "category": event["category"],
        "action": event["action"],
        "process_executable": _process_name(event),
        "resource_kind": resource.get("kind"),
        "resource_identifier": resource.get("identifier"),
        "protocol": resource.get("protocol"),
    })


def _details(event: dict[str, Any]) -> dict[str, Any]:
    return {
        "category": event["category"],
        "action": event["action"],
        "process_executable": _process_name(event),
        "resource": event.get("resource"),
        "outcome": event.get("outcome"),
        "attributes": {
            key: value
            for key, value in event.get("attributes", {}).items()
            if key != "process_start_time_ticks"
        },
    }


def _signature(event: dict[str, Any]) -> str:
    # Run-specific IDs, timestamps, PIDs, and source references are evidence
    # pointers, not behaviour identity.
    return canonical_json(_details(event))


def _event_order(event: dict[str, Any]) -> tuple[str, str, str]:
    return (_signature(event), event["event_id"], event["timestamp"])


def _evidence(run_id: str, event: dict[str, Any]) -> dict[str, Any]:
    observed = _details(event)
    process = event.get("process")
    if isinstance(process, dict):
        pid = process.get("pid")
        parent_pid = process.get("parent_pid")
        executable = process.get("executable")
        if (
            type(pid) is int and pid >= 0
            and type(parent_pid) is int and parent_pid >= 0
            and isinstance(executable, str)
        ):
            identity: dict[str, Any] = {
                "pid": pid,
                "parent_pid": parent_pid,
                "executable": executable,
            }
            attributes = event.get("attributes")
            start_time_ticks = attributes.get("process_start_time_ticks") if isinstance(attributes, dict) else None
            if type(start_time_ticks) is int and start_time_ticks >= 0:
                identity["start_time_ticks"] = start_time_ticks
            observed["process_identity"] = identity

    return {
        "run_id": run_id,
        "event_id": event["event_id"],
        "collector": event["source"]["collector"],
        "source_event_ref": event["source"]["event_ref"],
        "timestamp": event["timestamp"],
        "observed": observed,
    }


def _uncertainty(code: str, message: str, run_id: str | None = None) -> dict[str, Any]:
    return {"code": code, "message": message, "run_id": run_id}


def _coverage_uncertainties(
    manifest: dict[str, Any], events: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    run_id = manifest["run_id"]
    capture = manifest["capture"]
    coverage = manifest["coverage"]
    found: list[dict[str, Any]] = []

    if capture["status"] != "completed":
        found.append(_uncertainty(
            "capture_not_completed",
            f"Capture status is '{capture['status']}'; observations may be incomplete.",
            run_id,
        ))

    unavailable = sorted(set(coverage["unavailable_categories"]))
    if unavailable:
        found.append(_uncertainty(
            "categories_unavailable",
            "Unavailable event categories: " + ", ".join(unavailable) + ".",
            run_id,
        ))

    inactive = [source for source in coverage["event_sources"] if source["state"] != "active"]
    if inactive:
        found.append(_uncertainty(
            "event_sources_not_active",
            f"{len(inactive)} configured event source(s) were not active.",
            run_id,
        ))

    if coverage["dropped_events"] > 0:
        found.append(_uncertainty(
            "events_dropped",
            f"Collector reports {coverage['dropped_events']} dropped event(s).",
            run_id,
        ))

    if coverage["errors"]:
        found.append(_uncertainty(
            "collection_errors",
            f"Run manifest records {len(coverage['errors'])} collection error(s); inspect the local manifest for details.",
            run_id,
        ))

    active_categories = {
        category
        for source in coverage["event_sources"] if source["state"] == "active"
        for category in source["categories"]
    }
    declared_unavailable = set(coverage["unavailable_categories"])
    undeclared = sorted(COVERAGE_CATEGORIES - active_categories - declared_unavailable)
    if undeclared:
        found.append(_uncertainty(
            "coverage_not_declared",
            "Coverage was not declared active or unavailable for: " + ", ".join(undeclared) + ".",
            run_id,
        ))

    quality_counts: dict[str, int] = defaultdict(int)
    coverage_actions: set[str] = set()
    for event in events:
        if event["collection_quality"] != "complete":
            quality_counts[event["collection_quality"]] += 1
        if event["category"] == "coverage" and event["action"] in {
            "coverage_gap", "capture_incomplete", "collector_error", "events_dropped"
        }:
            coverage_actions.add(event["action"])

    if quality_counts:
        detail = ", ".join(f"{key}: {count}" for key, count in sorted(quality_counts.items()))
        found.append(_uncertainty(
            "events_with_reduced_quality",
            f"Input contains event records with non-complete collection quality ({detail}).",
            run_id,
        ))

    if coverage_actions:
        found.append(_uncertainty(
            "coverage_events_present",
            "Coverage events report: " + ", ".join(sorted(coverage_actions)) + ".",
            run_id,
        ))

    if manifest["application"]["artifact_sha256"] is None:
        found.append(_uncertainty(
            "artifact_hash_missing",
            "Application artifact SHA-256 was unavailable for this run.",
            run_id,
        ))

    return found


def _validate_pair(baseline: dict[str, Any], candidate: dict[str, Any]) -> None:
    if baseline.get("role") != "baseline":
        raise ComparisonError("first run manifest must have role 'baseline'")
    if candidate.get("role") != "candidate":
        raise ComparisonError("second run manifest must have role 'candidate'")
    if baseline["experiment_id"] != candidate["experiment_id"]:
        raise ComparisonError("run manifests must share the same experiment_id")
    if baseline["run_id"] == candidate["run_id"]:
        raise ComparisonError("baseline and candidate must have different run_id values")

    baseline_app = baseline["application"]
    candidate_app = candidate["application"]
    if baseline_app["name"] != candidate_app["name"]:
        raise ComparisonError("run manifests identify different application names")

    baseline_package = baseline_app.get("package_id")
    candidate_package = candidate_app.get("package_id")
    if baseline_package and candidate_package and baseline_package != candidate_package:
        raise ComparisonError("run manifests identify different application package IDs")

    for label, manifest in (("baseline", baseline), ("candidate", candidate)):
        if manifest["capture"]["status"] == "running":
            raise ComparisonError(f"{label} capture is still running; compare only after capture has stopped")


def _context_differences(baseline: dict[str, Any], candidate: dict[str, Any]) -> list[dict[str, Any]]:
    fields = [
        ("environment.os", baseline["environment"]["os"], candidate["environment"]["os"]),
        ("environment.os_version", baseline["environment"]["os_version"], candidate["environment"]["os_version"]),
        ("environment.architecture", baseline["environment"]["architecture"], candidate["environment"]["architecture"]),
        ("environment.snapshot_id", baseline["environment"]["snapshot_id"], candidate["environment"]["snapshot_id"]),
        ("capture.scenario", baseline["capture"]["scenario"], candidate["capture"]["scenario"]),
        ("capture.profile", baseline["capture"]["profile"], candidate["capture"]["profile"]),
    ]
    return [
        {"field": name, "baseline_value": left, "candidate_value": right}
        for name, left, right in fields if left != right
    ]


def _finding(
    rule_id: str,
    change_type: str,
    category: str,
    baseline_run_id: str,
    candidate_run_id: str,
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> dict[str, Any]:
    representative = (after or before)[0]
    action = representative["action"]
    executable = _process_name(representative)
    subject = f"{category} action '{action}'"
    if executable:
        subject += f" for process '{executable}'"

    if change_type == "added":
        summary = f"New candidate observation: {subject}."
        observation = "This observation appears in the candidate input and has no matching baseline record."
    elif change_type == "removed":
        summary = f"Baseline observation not repeated: {subject}."
        observation = "This observation appears in the baseline input but has no matching candidate record in this run."
    else:
        summary = f"Observation details changed: {subject}."
        observation = "Baseline and candidate observations share the comparison identity but differ in normalized details."

    baseline_evidence = [_evidence(baseline_run_id, event) for event in before]
    candidate_evidence = [_evidence(candidate_run_id, event) for event in after]
    payload = {
        "rule_id": rule_id,
        "change_type": change_type,
        "category": category,
        "summary": summary,
        "observation": observation,
        "baseline_evidence": baseline_evidence,
        "candidate_evidence": candidate_evidence,
    }
    finding_id = hashlib.sha256(canonical_json(payload).encode("utf-8")).hexdigest()
    return {
        "finding_id": finding_id,
        **payload,
        "interpretation": (
            "A behavioural difference is a lead for review, not proof of intent. "
            "An observation not repeated in another run can reflect timing, environment, or collection coverage."
        ),
    }


def _match_events(
    baseline_manifest: dict[str, Any],
    candidate_manifest: dict[str, Any],
    baseline_events: list[dict[str, Any]],
    candidate_events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    base_id = baseline_manifest["run_id"]
    candidate_id = candidate_manifest["run_id"]
    base_groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    candidate_groups: dict[str, list[dict[str, Any]]] = defaultdict(list)

    for event in baseline_events:
        if event["category"] != "coverage":
            base_groups[_identity(event)].append(event)
    for event in candidate_events:
        if event["category"] != "coverage":
            candidate_groups[_identity(event)].append(event)

    findings: list[dict[str, Any]] = []
    for identity in sorted(set(base_groups) | set(candidate_groups)):
        before = sorted(base_groups.get(identity, []), key=_event_order)
        after = sorted(candidate_groups.get(identity, []), key=_event_order)
        base_details: dict[str, list[dict[str, Any]]] = defaultdict(list)
        candidate_details: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for event in before:
            base_details[_signature(event)].append(event)
        for event in after:
            candidate_details[_signature(event)].append(event)

        before_remaining: list[dict[str, Any]] = []
        after_remaining: list[dict[str, Any]] = []
        for sig in sorted(set(base_details) | set(candidate_details)):
            base_items = base_details.get(sig, [])
            candidate_items = candidate_details.get(sig, [])
            common = min(len(base_items), len(candidate_items))
            before_remaining.extend(base_items[common:])
            after_remaining.extend(candidate_items[common:])

        before_remaining.sort(key=_event_order)
        after_remaining.sort(key=_event_order)
        paired = min(len(before_remaining), len(after_remaining))
        changed: dict[tuple[str, str], dict[str, list[dict[str, Any]]]] = {}
        for index in range(paired):
            old = before_remaining[index]
            new = after_remaining[index]
            key = (_signature(old), _signature(new))
            group = changed.setdefault(key, {"before": [], "after": []})
            group["before"].append(old)
            group["after"].append(new)

        for _, group in sorted(changed.items()):
            findings.append(_finding(
                "DG-COMPARE-002", "changed", group["after"][0]["category"],
                base_id, candidate_id, group["before"], group["after"],
            ))

        removed: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for event in before_remaining[paired:]:
            removed[_signature(event)].append(event)
        for _, items in sorted(removed.items()):
            findings.append(_finding(
                "DG-COMPARE-001", "removed", items[0]["category"],
                base_id, candidate_id, items, [],
            ))

        added: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for event in after_remaining[paired:]:
            added[_signature(event)].append(event)
        for _, items in sorted(added.items()):
            findings.append(_finding(
                "DG-COMPARE-001", "added", items[0]["category"],
                base_id, candidate_id, [], items,
            ))

    return sorted(findings, key=lambda item: (item["change_type"], item["category"], item["finding_id"]))


def compare_loaded_runs(
    baseline_manifest: dict[str, Any],
    candidate_manifest: dict[str, Any],
    baseline_events: list[dict[str, Any]],
    candidate_events: list[dict[str, Any]],
) -> dict[str, Any]:
    """Compare already-validated runs and return a reproducible report object."""
    _validate_pair(baseline_manifest, candidate_manifest)
    baseline_id = baseline_manifest["run_id"]
    candidate_id = candidate_manifest["run_id"]

    differences = _context_differences(baseline_manifest, candidate_manifest)
    uncertainties = (
        _coverage_uncertainties(baseline_manifest, baseline_events)
        + _coverage_uncertainties(candidate_manifest, candidate_events)
    )
    for difference in differences:
        uncertainties.append(_uncertainty(
            "run_context_differs",
            f"Run context differs at {difference['field']}; review this before attributing a behaviour change.",
        ))

    base_hash = baseline_manifest["application"]["artifact_sha256"]
    candidate_hash = candidate_manifest["application"]["artifact_sha256"]
    if base_hash and candidate_hash and base_hash == candidate_hash:
        uncertainties.append(_uncertainty(
            "same_artifact_hash",
            "Both runs identify the same artifact hash; this may be a repeat-run comparison rather than an update comparison.",
        ))

    uncertainties.sort(key=lambda item: (item["code"], item.get("run_id") or "", item["message"]))
    findings = _match_events(baseline_manifest, candidate_manifest, baseline_events, candidate_events)
    core = {
        "schema_version": 1,
        "status": "partial" if uncertainties else "complete",
        "experiment_id": baseline_manifest["experiment_id"],
        "application_name": baseline_manifest["application"]["name"],
        "baseline": {
            "run_id": baseline_id,
            "version": baseline_manifest["application"]["version"],
            "artifact_sha256": base_hash,
            "capture_status": baseline_manifest["capture"]["status"],
        },
        "candidate": {
            "run_id": candidate_id,
            "version": candidate_manifest["application"]["version"],
            "artifact_sha256": candidate_hash,
            "capture_status": candidate_manifest["capture"]["status"],
        },
        "environment_differences": differences,
        "findings": findings,
        "uncertainties": uncertainties,
        "summary": {
            "baseline_event_count": len(baseline_events),
            "candidate_event_count": len(candidate_events),
            "baseline_behaviour_event_count": sum(event["category"] != "coverage" for event in baseline_events),
            "candidate_behaviour_event_count": sum(event["category"] != "coverage" for event in candidate_events),
            "added_count": sum(item["change_type"] == "added" for item in findings),
            "removed_count": sum(item["change_type"] == "removed" for item in findings),
            "changed_count": sum(item["change_type"] == "changed" for item in findings),
            "uncertainty_count": len(uncertainties),
        },
    }
    report_id = hashlib.sha256(canonical_json(core).encode("utf-8")).hexdigest()
    return {"report_id": report_id, **core}


def compare_runs(
    baseline_manifest_path: str | Path,
    candidate_manifest_path: str | Path,
    baseline_events_path: str | Path,
    candidate_events_path: str | Path,
    manifest_schema_path: str | Path,
    event_schema_path: str | Path,
) -> dict[str, Any]:
    """Validate the two run manifests and event streams, then compare them."""
    try:
        base = load_manifest(baseline_manifest_path, manifest_schema_path)
        candidate = load_manifest(candidate_manifest_path, manifest_schema_path)
    except ManifestValidationError as exc:
        raise ComparisonError(f"invalid run manifest: {exc}") from exc
    try:
        base_events = load_jsonl(baseline_events_path, event_schema_path)
        candidate_events = load_jsonl(candidate_events_path, event_schema_path)
    except EventValidationError as exc:
        raise ComparisonError(f"invalid event stream: {exc}") from exc
    return compare_loaded_runs(base, candidate, base_events, candidate_events)
