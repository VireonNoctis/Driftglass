"""Developer CLI for validated Driftglass inputs, comparison, and local history."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
import sysconfig
from typing import Sequence

from .comparison import ComparisonError, compare_runs
from .events import EventValidationError, load_jsonl
from .manifests import ManifestValidationError, load_manifest
from .reporting import ReportExportError, export_html_report, export_json_report
from .storage import StorageError, get_report, list_reports, save_report

def _default_schema_root() -> Path:
    """Locate schema contracts in a checkout or a normal wheel installation."""
    candidates = (
        Path(__file__).resolve().parents[2] / "schemas",
        Path(sysconfig.get_path("data")) / "share" / "driftglass" / "schemas",
    )
    required = (
        Path("events/event-v1.schema.json"),
        Path("manifests/run-manifest-v1.schema.json"),
        Path("reports/report-v1.schema.json"),
    )
    for candidate in candidates:
        if all((candidate / relative).is_file() for relative in required):
            return candidate
    return candidates[0]


SCHEMA_ROOT = _default_schema_root()
EVENT_SCHEMA = SCHEMA_ROOT / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = SCHEMA_ROOT / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = SCHEMA_ROOT / "reports" / "report-v1.schema.json"


class CliError(ValueError):
    """Raised when a requested CLI operation cannot be completed."""


def _resolved_path(value: str | Path) -> Path:
    try:
        return Path(value).expanduser().resolve()
    except (OSError, RuntimeError) as exc:
        raise CliError(f"cannot resolve a requested path ({type(exc).__name__})") from exc


def _reject_path_collisions(
    outputs: Sequence[tuple[str, str | None]],
    protected_inputs: Sequence[tuple[str, str | None]],
) -> None:
    seen_outputs: dict[Path, str] = {}
    for label, value in outputs:
        if value is None:
            continue
        resolved = _resolved_path(value)
        previous = seen_outputs.get(resolved)
        if previous is not None:
            raise CliError(f"{label} and {previous} must use different paths")
        seen_outputs[resolved] = label
        for input_label, input_value in protected_inputs:
            if input_value is not None and resolved == _resolved_path(input_value):
                raise CliError(f"{label} must not target the same path as {input_label}")


def _add_schema_argument(parser: argparse.ArgumentParser, name: str, default: Path) -> None:
    parser.add_argument(
        "--" + name.replace("_", "-"),
        default=str(default),
        help=f"path to the {name.replace('_', ' ')} file (default: repository schema)",
    )


def _validate_events(args: argparse.Namespace) -> int:
    events = load_jsonl(args.path, args.event_schema)
    print(f"valid event stream: {len(events)} record(s)")
    return 0


def _validate_manifest(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.path, args.manifest_schema)
    coverage = manifest["coverage"]
    print(
        f"valid run manifest: run_id={manifest['run_id']} "
        f"role={manifest['role']} capture={manifest['capture']['status']} "
        f"dropped_events={coverage['dropped_events']}"
    )
    return 0


def _compare(args: argparse.Namespace) -> int:
    outputs_requested = bool(args.json_output or args.html_output or args.history_db)
    if args.overwrite and not (args.json_output or args.html_output):
        raise CliError("--overwrite requires --json-output or --html-output")

    _reject_path_collisions(
        [
            ("JSON report output", args.json_output),
            ("HTML report output", args.html_output),
            ("history database", args.history_db),
        ],
        [
            ("baseline manifest", args.baseline_manifest),
            ("candidate manifest", args.candidate_manifest),
            ("baseline events", args.baseline_events),
            ("candidate events", args.candidate_events),
            ("manifest schema", args.manifest_schema),
            ("event schema", args.event_schema),
            ("report schema", args.report_schema),
        ],
    )

    report = compare_runs(
        args.baseline_manifest,
        args.candidate_manifest,
        args.baseline_events,
        args.candidate_events,
        args.manifest_schema,
        args.event_schema,
    )

    if args.json_output:
        export_json_report(report, args.report_schema, args.json_output, overwrite=args.overwrite)
    if args.html_output:
        export_html_report(report, args.report_schema, args.html_output, overwrite=args.overwrite)
    if args.history_db:
        save_report(args.history_db, report, args.report_schema)

    if not outputs_requested:
        print(json.dumps(report, sort_keys=True, indent=2, ensure_ascii=False))
    else:
        print(
            f"report_id={report['report_id']} status={report['status']} "
            f"added={report['summary']['added_count']} "
            f"removed={report['summary']['removed_count']} "
            f"changed={report['summary']['changed_count']} "
            f"uncertainties={report['summary']['uncertainty_count']}"
        )
    return 0


def _history_list(args: argparse.Namespace) -> int:
    reports = list_reports(args.database, limit=args.limit)
    if not reports:
        print("no reports in local history")
        return 0
    for report in reports:
        print(
            f"{report['report_id']}  {report['status']}  "
            f"{report['application_name']}  "
            f"{report['baseline_version']} -> {report['candidate_version']}  "
            f"findings={report['finding_count']} uncertainties={report['uncertainty_count']}"
        )
    return 0


def _history_show(args: argparse.Namespace) -> int:
    report = get_report(args.database, args.report_id, args.report_schema)
    if report is None:
        raise CliError(f"report not found in local history: {args.report_id}")
    print(json.dumps(report, sort_keys=True, indent=2, ensure_ascii=False))
    return 0


def _history_export(args: argparse.Namespace) -> int:
    _reject_path_collisions(
        [("export output", args.output)],
        [("history database", args.database), ("report schema", args.report_schema)],
    )
    report = get_report(args.database, args.report_id, args.report_schema)
    if report is None:
        raise CliError(f"report not found in local history: {args.report_id}")
    if args.format == "json":
        export_json_report(report, args.report_schema, args.output, overwrite=args.overwrite)
    else:
        export_html_report(report, args.report_schema, args.output, overwrite=args.overwrite)
    print(f"exported report {args.report_id} to {args.output}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="driftglass",
        description=(
            "Validate captured event data, compare baseline and candidate runs, "
            "and inspect local reports. This tool does not launch applications."
        ),
    )
    commands = parser.add_subparsers(dest="command", required=True)

    events_parser = commands.add_parser("events", help="validate captured JSONL events")
    events_commands = events_parser.add_subparsers(dest="events_command", required=True)
    events_validate = events_commands.add_parser("validate", help="validate one JSONL stream")
    events_validate.add_argument("path")
    _add_schema_argument(events_validate, "event_schema", EVENT_SCHEMA)
    events_validate.set_defaults(handler=_validate_events)

    manifest_parser = commands.add_parser("manifest", help="validate a run manifest")
    manifest_commands = manifest_parser.add_subparsers(dest="manifest_command", required=True)
    manifest_validate = manifest_commands.add_parser("validate", help="validate one manifest")
    manifest_validate.add_argument("path")
    _add_schema_argument(manifest_validate, "manifest_schema", MANIFEST_SCHEMA)
    manifest_validate.set_defaults(handler=_validate_manifest)

    compare_parser = commands.add_parser("compare", help="compare two existing captured runs")
    compare_parser.add_argument("--baseline-manifest", required=True)
    compare_parser.add_argument("--candidate-manifest", required=True)
    compare_parser.add_argument("--baseline-events", required=True)
    compare_parser.add_argument("--candidate-events", required=True)
    _add_schema_argument(compare_parser, "manifest_schema", MANIFEST_SCHEMA)
    _add_schema_argument(compare_parser, "event_schema", EVENT_SCHEMA)
    _add_schema_argument(compare_parser, "report_schema", REPORT_SCHEMA)
    compare_parser.add_argument("--json-output", help="write a structured JSON report")
    compare_parser.add_argument("--html-output", help="write a standalone HTML report")
    compare_parser.add_argument("--history-db", help="save report to this local SQLite database")
    compare_parser.add_argument("--overwrite", action="store_true", help="allow replacing selected output files")
    compare_parser.set_defaults(handler=_compare)

    history_parser = commands.add_parser("history", help="browse locally stored reports")
    history_commands = history_parser.add_subparsers(dest="history_command", required=True)

    history_list = history_commands.add_parser("list", help="list report summaries")
    history_list.add_argument("--database", required=True)
    history_list.add_argument("--limit", type=int, default=100)
    history_list.set_defaults(handler=_history_list)

    history_show = history_commands.add_parser("show", help="print one stored report as JSON")
    history_show.add_argument("--database", required=True)
    history_show.add_argument("--report-id", required=True)
    _add_schema_argument(history_show, "report_schema", REPORT_SCHEMA)
    history_show.set_defaults(handler=_history_show)

    history_export = history_commands.add_parser("export", help="export one stored report")
    history_export.add_argument("--database", required=True)
    history_export.add_argument("--report-id", required=True)
    history_export.add_argument("--format", choices=("json", "html"), required=True)
    history_export.add_argument("--output", required=True)
    _add_schema_argument(history_export, "report_schema", REPORT_SCHEMA)
    history_export.add_argument("--overwrite", action="store_true", help="allow replacing the output file")
    history_export.set_defaults(handler=_history_export)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (
        CliError,
        ComparisonError,
        EventValidationError,
        ManifestValidationError,
        ReportExportError,
        StorageError,
    ) as exc:
        print(f"driftglass: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
