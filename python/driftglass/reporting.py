"""Validated, local-only JSON and standalone HTML report export."""

from __future__ import annotations

import hashlib
import html
import json
import os
import tempfile
from pathlib import Path
from typing import Any

from ._schema import canonical_json, instance_path, load_validator, safe_error_reason


class ReportExportError(ValueError):
    """Raised when a report is invalid or cannot be written safely."""


def _validate_report_relationships(report: dict[str, Any]) -> None:
    """Check cross-field invariants that JSON Schema cannot express directly."""
    baseline_run_id = report["baseline"]["run_id"]
    candidate_run_id = report["candidate"]["run_id"]
    if baseline_run_id == candidate_run_id:
        raise ReportExportError("baseline and candidate must reference distinct runs")

    summary = report["summary"]
    findings = report["findings"]
    for change_type in ("added", "removed", "changed"):
        expected_count = sum(
            finding["change_type"] == change_type for finding in findings
        )
        if summary[f"{change_type}_count"] != expected_count:
            raise ReportExportError(
                f"summary.{change_type}_count does not match findings"
            )
    if summary["uncertainty_count"] != len(report["uncertainties"]):
        raise ReportExportError("summary.uncertainty_count does not match uncertainties")
    if summary["baseline_behaviour_event_count"] > summary["baseline_event_count"]:
        raise ReportExportError(
            "summary.baseline_behaviour_event_count exceeds baseline_event_count"
        )
    if summary["candidate_behaviour_event_count"] > summary["candidate_event_count"]:
        raise ReportExportError(
            "summary.candidate_behaviour_event_count exceeds candidate_event_count"
        )

    for finding in findings:
        for evidence_key, expected_run_id in (
            ("baseline_evidence", baseline_run_id),
            ("candidate_evidence", candidate_run_id),
        ):
            for evidence in finding[evidence_key]:
                if evidence["run_id"] != expected_run_id:
                    raise ReportExportError(
                        f"finding {evidence_key} references an unexpected run_id"
                    )
                if evidence["observed"]["category"] != finding["category"]:
                    raise ReportExportError(
                        f"finding {evidence_key} category does not match finding category"
                    )


def validate_report(report: dict[str, Any], schema_path: str | Path) -> None:
    if not isinstance(report, dict):
        raise ReportExportError("report must be a JSON object")

    try:
        validator = load_validator(Path(schema_path), "report schema")
    except ValueError as exc:
        raise ReportExportError(str(exc)) from exc

    errors = sorted(
        validator.iter_errors(report),
        key=lambda error: (
            tuple(str(part) for part in error.absolute_path),
            str(error.validator),
            error.message,
        ),
    )
    if errors:
        error = errors[0]
        raise ReportExportError(
            f"{instance_path(error.absolute_path)}: {safe_error_reason(error)}"
        )

    _validate_report_relationships(report)
    core = {key: value for key, value in report.items() if key != "report_id"}
    try:
        expected_id = hashlib.sha256(canonical_json(core).encode("utf-8")).hexdigest()
    except (TypeError, ValueError) as exc:
        raise ReportExportError("report cannot be represented as canonical JSON") from exc
    if report["report_id"] != expected_id:
        raise ReportExportError("report_id does not match the report content")


def _write_output(output_path: str | Path, content: str, overwrite: bool) -> Path:
    path = Path(output_path)
    if not path.parent.is_dir():
        raise ReportExportError(f"output directory does not exist: '{path.parent}'")

    if not overwrite:
        created_identity: tuple[int, int] | None = None
        try:
            descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            file_stat = os.fstat(descriptor)
            created_identity = (file_stat.st_dev, file_stat.st_ino)
            with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
        except FileExistsError as exc:
            raise ReportExportError(f"output already exists: '{path}'") from exc
        except (OSError, UnicodeError) as exc:
            # Remove a partial file only if the path still names the inode we created.
            if created_identity is not None:
                try:
                    current = path.lstat()
                    if (current.st_dev, current.st_ino) == created_identity:
                        path.unlink()
                except OSError:
                    pass
            raise ReportExportError(
                f"could not write report to '{path}' ({type(exc).__name__})"
            ) from exc
        return path

    temporary_name: str | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=".driftglass-report-", dir=path.parent
        )
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except (OSError, UnicodeError) as exc:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass
        raise ReportExportError(
            f"could not replace report at '{path}' ({type(exc).__name__})"
        ) from exc
    return path


def export_json_report(
    report: dict[str, Any],
    report_schema_path: str | Path,
    output_path: str | Path,
    *,
    overwrite: bool = False,
) -> Path:
    """Validate and export one report as deterministic, indented UTF-8 JSON.

    The destination is created with owner-only permissions by default and is
    never overwritten unless overwrite=True is explicitly provided.
    """
    validate_report(report, report_schema_path)
    content = json.dumps(report, sort_keys=True, indent=2, ensure_ascii=False) + "\n"
    return _write_output(output_path, content, overwrite)


def _render_evidence(title: str, evidence: list[dict[str, Any]]) -> str:
    escaped_title = html.escape(title, quote=True)
    if not evidence:
        return f"<p class='empty'>{escaped_title}: no matching evidence in this run.</p>"

    rendered = []
    for item in evidence:
        payload = html.escape(json.dumps(item, sort_keys=True, indent=2, ensure_ascii=False), quote=True)
        rendered.append(
            f"<details><summary>{escaped_title}: {html.escape(item['event_id'], quote=True)}</summary>"
            f"<pre>{payload}</pre></details>"
        )
    return "".join(rendered)


def render_html_report(report: dict[str, Any], report_schema_path: str | Path) -> str:
    """Render a validated report as standalone HTML with no remote resources or scripts."""
    validate_report(report, report_schema_path)
    summary = report["summary"]

    summary_rows = "".join(
        "<tr><th>" + html.escape(label) + "</th><td>" + str(summary[key]) + "</td></tr>"
        for key, label in (
            ("baseline_event_count", "Baseline input records"),
            ("candidate_event_count", "Candidate input records"),
            ("baseline_behaviour_event_count", "Baseline behaviour records"),
            ("candidate_behaviour_event_count", "Candidate behaviour records"),
            ("added_count", "Added findings"),
            ("removed_count", "Removed findings"),
            ("changed_count", "Changed findings"),
            ("uncertainty_count", "Uncertainty items"),
        )
    )

    context_rows = "".join(
        "<tr><th>" + html.escape(item["field"]) + "</th><td>"
        + html.escape("—" if item["baseline_value"] is None else item["baseline_value"])
        + "</td><td>"
        + html.escape("—" if item["candidate_value"] is None else item["candidate_value"])
        + "</td></tr>"
        for item in report["environment_differences"]
    )
    if not context_rows:
        context_rows = "<tr><td colspan='3'>No declared context differences.</td></tr>"

    finding_sections = []
    for finding in report["findings"]:
        change_type = html.escape(finding["change_type"], quote=True)
        finding_sections.append(
            f"<article class='finding'><p class='meta'>{html.escape(finding['rule_id'])} · "
            f"{html.escape(finding['category'])} · {change_type}</p>"
            f"<h3>{html.escape(finding['summary'], quote=True)}</h3>"
            f"<p>{html.escape(finding['observation'], quote=True)}</p>"
            f"<p class='interpretation'>{html.escape(finding['interpretation'], quote=True)}</p>"
            f"<p class='id'>Finding {html.escape(finding['finding_id'], quote=True)}</p>"
            + _render_evidence("Baseline evidence", finding["baseline_evidence"])
            + _render_evidence("Candidate evidence", finding["candidate_evidence"])
            + "</article>"
        )
    if not finding_sections:
        finding_sections.append("<p class='empty'>No differences were observed in the supplied behavioural records. This is limited to the input coverage.</p>")

    uncertainty_rows = "".join(
        "<li><strong>" + html.escape(item["code"]) + "</strong>: "
        + html.escape(item["message"], quote=True)
        + (f" <span class='id'>Run {html.escape(item['run_id'], quote=True)}</span>" if item["run_id"] else "")
        + "</li>"
        for item in report["uncertainties"]
    )
    if not uncertainty_rows:
        uncertainty_rows = "<li>No gaps were declared in the validated inputs. This is not a safety verdict.</li>"

    baseline = report["baseline"]
    candidate = report["candidate"]
    status = report["status"]
    title = report["application_name"]
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Driftglass report — {html.escape(title, quote=True)}</title>
<style>
:root {{ color-scheme: dark; font-family: system-ui, sans-serif; background: #101112; color: #eceeef; }}
body {{ margin: 0 auto; max-width: 1100px; padding: 2rem 1.2rem 4rem; line-height: 1.5; }}
h1, h2, h3 {{ line-height: 1.2; }}
h1 {{ margin-bottom: .35rem; }}
h2 {{ margin-top: 2.2rem; padding-bottom: .55rem; border-bottom: 1px solid #43474b; }}
p {{ max-width: 85ch; }}
.meta, .id {{ color: #aeb4ba; font: .82rem ui-monospace, monospace; overflow-wrap: anywhere; }}
.id {{ font-size: .75rem; }}
.status {{ display: inline-block; border: 1px solid #737980; padding: .2rem .55rem; text-transform: uppercase; letter-spacing: .08em; font-size: .75rem; }}
table {{ border-collapse: collapse; width: 100%; margin: .7rem 0; }}
th, td {{ border-bottom: 1px solid #363a3e; padding: .55rem .6rem; text-align: left; vertical-align: top; overflow-wrap: anywhere; }}
th {{ color: #bdc3c9; font-weight: 600; }}
article.finding {{ border-top: 1px solid #43474b; padding: 1rem 0 1.5rem; }}
.interpretation, .empty {{ color: #c2c7cc; }}
details {{ margin: .5rem 0; border: 1px solid #3b4044; padding: .4rem .6rem; }}
summary {{ cursor: pointer; }}
pre {{ overflow-x: auto; white-space: pre-wrap; overflow-wrap: anywhere; font: .8rem ui-monospace, monospace; }}
li {{ margin: .6rem 0; }}
</style>
</head>
<body>
<header>
<p class="meta">DRIFTGLASS · REPORT SCHEMA V1</p>
<h1>{html.escape(title, quote=True)}</h1>
<p class="status">{html.escape(status)}</p>
<p>Report ID <span class="id">{html.escape(report["report_id"])}</span></p>
<p>These findings describe observed differences in supplied records. They do not prove intent or establish that the application is safe.</p>
</header>
<h2>Compared runs</h2>
<table><thead><tr><th>Role</th><th>Version</th><th>Run ID</th><th>Capture status</th></tr></thead><tbody>
<tr><th>Baseline</th><td>{html.escape(baseline["version"])}</td><td class="id">{html.escape(baseline["run_id"])}</td><td>{html.escape(baseline["capture_status"])}</td></tr>
<tr><th>Candidate</th><td>{html.escape(candidate["version"])}</td><td class="id">{html.escape(candidate["run_id"])}</td><td>{html.escape(candidate["capture_status"])}</td></tr>
</tbody></table>
<h2>Summary</h2>
<table><tbody>{summary_rows}</tbody></table>
<h2>Run context differences</h2>
<table><thead><tr><th>Field</th><th>Baseline</th><th>Candidate</th></tr></thead><tbody>{context_rows}</tbody></table>
<h2>Findings</h2>
{"".join(finding_sections)}
<h2>Uncertainty and coverage gaps</h2>
<ul>{uncertainty_rows}</ul>
<footer><p class="meta">Generated locally from a validated report object. Review metadata before sharing; paths, hostnames, and event attributes can be sensitive.</p></footer>
</body>
</html>
"""


def export_html_report(
    report: dict[str, Any],
    report_schema_path: str | Path,
    output_path: str | Path,
    *,
    overwrite: bool = False,
) -> Path:
    """Validate and export a standalone HTML report with no external assets."""
    content = render_html_report(report, report_schema_path)
    return _write_output(output_path, content, overwrite)
