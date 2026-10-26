"""Detached integrity manifests for exported comparison reports.

The manifest detects edits relative to a separately saved manifest. It does not
authenticate the report author or prove that collection was complete.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from ._schema import finite_json_float, load_validator, reject_json_constant, unique_json_object
from .repeatability import MAX_REPORT_FILE_BYTES
from .reporting import ReportExportError, validate_report, write_text_output

MAX_INTEGRITY_MANIFEST_BYTES = 64 * 1024


class IntegrityError(ValueError):
    """Raised when a report integrity manifest cannot be created or verified."""


def _read_bounded(path: Path, maximum: int, label: str) -> bytes:
    try:
        with path.open("rb") as stream:
            content = stream.read(maximum + 1)
    except OSError as exc:
        raise IntegrityError(f"could not read {label} '{path}' ({type(exc).__name__})") from exc
    if len(content) > maximum:
        raise IntegrityError(f"{label} exceeds the {maximum}-byte file limit")
    return content


def _load_report_bytes(
    report_path: str | Path,
    report_schema_path: str | Path,
) -> tuple[bytes, dict[str, Any]]:
    path = Path(report_path).expanduser()
    raw = _read_bounded(path, MAX_REPORT_FILE_BYTES, "comparison report")
    try:
        report = json.loads(
            raw.decode("utf-8"),
            parse_constant=reject_json_constant,
            parse_float=finite_json_float,
            object_pairs_hook=unique_json_object,
        )
    except (UnicodeError, json.JSONDecodeError, ValueError) as exc:
        raise IntegrityError(
            f"comparison report is not strict UTF-8 JSON ({type(exc).__name__})"
        ) from exc
    try:
        validate_report(report, report_schema_path)
    except ReportExportError as exc:
        raise IntegrityError(f"comparison report is invalid: {exc}") from exc
    return raw, report


def _load_manifest(
    manifest_path: str | Path,
    schema_path: str | Path,
) -> dict[str, Any]:
    path = Path(manifest_path).expanduser()
    raw = _read_bounded(path, MAX_INTEGRITY_MANIFEST_BYTES, "integrity manifest")
    try:
        manifest = json.loads(
            raw.decode("utf-8"),
            parse_constant=reject_json_constant,
            parse_float=finite_json_float,
            object_pairs_hook=unique_json_object,
        )
    except (UnicodeError, json.JSONDecodeError, ValueError) as exc:
        raise IntegrityError(
            f"integrity manifest is not strict UTF-8 JSON ({type(exc).__name__})"
        ) from exc

    try:
        validator = load_validator(Path(schema_path), "export manifest schema")
    except ValueError as exc:
        raise IntegrityError(str(exc)) from exc
    errors = sorted(
        validator.iter_errors(manifest),
        key=lambda error: (
            tuple(str(part) for part in error.absolute_path),
            str(error.validator),
            error.message,
        ),
    )
    if errors:
        error = errors[0]
        location = "$" + "".join(f".{part}" for part in error.absolute_path)
        raise IntegrityError(f"invalid integrity manifest at {location}: {error.validator}")
    return manifest


def attest_report(
    report_path: str | Path,
    report_schema_path: str | Path,
    manifest_schema_path: str | Path,
    output_path: str | Path,
    *,
    overwrite: bool = False,
) -> dict[str, Any]:
    """Validate a report and write a detached SHA-256 integrity manifest."""
    raw, report = _load_report_bytes(report_path, report_schema_path)
    manifest = {
        "schema_version": 1,
        "artifact_type": "driftglass-comparison-report",
        "artifact_sha256": hashlib.sha256(raw).hexdigest(),
        "artifact_size_bytes": len(raw),
        "report_id": report["report_id"],
    }
    try:
        validator = load_validator(Path(manifest_schema_path), "export manifest schema")
    except ValueError as exc:
        raise IntegrityError(str(exc)) from exc
    if next(validator.iter_errors(manifest), None) is not None:
        raise IntegrityError("generated integrity manifest failed schema validation")

    content = json.dumps(manifest, sort_keys=True, indent=2, ensure_ascii=False) + "\n"
    try:
        write_text_output(output_path, content, overwrite)
    except ReportExportError as exc:
        raise IntegrityError(str(exc)) from exc
    return manifest


def verify_report(
    report_path: str | Path,
    manifest_path: str | Path,
    report_schema_path: str | Path,
    manifest_schema_path: str | Path,
) -> dict[str, Any]:
    """Verify report bytes and report ID against a previously saved manifest."""
    raw, report = _load_report_bytes(report_path, report_schema_path)
    manifest = _load_manifest(manifest_path, manifest_schema_path)
    actual_hash = hashlib.sha256(raw).hexdigest()

    if manifest["artifact_sha256"] != actual_hash:
        raise IntegrityError("report SHA-256 does not match integrity manifest")
    if manifest["artifact_size_bytes"] != len(raw):
        raise IntegrityError("report size does not match integrity manifest")
    if manifest["report_id"] != report["report_id"]:
        raise IntegrityError("report_id does not match integrity manifest")
    return manifest
