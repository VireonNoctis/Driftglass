"""Loading and validating versioned Driftglass run manifests."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from ._schema import (
    instance_path,
    load_validator,
    finite_json_float,
    reject_json_constant,
    safe_error_reason,
    unique_json_object,
)


class ManifestValidationError(ValueError):
    """Raised when a run manifest is missing, malformed, or inconsistent."""


def _parse_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def load_manifest(
    manifest_path: str | Path, schema_path: str | Path
) -> dict[str, Any]:
    """Read a run manifest, validate its schema, and check cross-field invariants.

    A manifest describes what was attempted and which telemetry sources were
    available. It does not itself establish that collection was comprehensive.
    """
    path = Path(manifest_path)
    try:
        validator = load_validator(Path(schema_path), "run manifest schema")
    except ValueError as exc:
        raise ManifestValidationError(str(exc)) from exc

    try:
        with path.open("r", encoding="utf-8") as stream:
            manifest = json.load(
                stream,
                parse_constant=reject_json_constant,
                parse_float=finite_json_float,
                object_pairs_hook=unique_json_object,
            )
    except json.JSONDecodeError as exc:
        raise ManifestValidationError(
            f"manifest '{path}' contains malformed JSON at line {exc.lineno}, column {exc.colno}"
        ) from exc
    except ValueError as exc:
        raise ManifestValidationError(
            f"manifest '{path}' contains invalid JSON content ({exc})"
        ) from exc
    except (OSError, UnicodeError) as exc:
        raise ManifestValidationError(
            f"could not read manifest '{path}' ({type(exc).__name__})"
        ) from exc

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
        raise ManifestValidationError(
            f"manifest '{path}': {instance_path(error.absolute_path)}: "
            f"{safe_error_reason(error)}"
        )

    capture = manifest["capture"]
    started_at = _parse_timestamp(capture["started_at"])
    ended_value = capture["ended_at"]
    ended_at = _parse_timestamp(ended_value) if ended_value is not None else None
    status = capture["status"]

    if ended_at is not None and ended_at < started_at:
        raise ManifestValidationError(
            "capture.ended_at must not be earlier than capture.started_at"
        )
    if status == "completed" and ended_at is None:
        raise ManifestValidationError("completed capture requires capture.ended_at")
    if status == "running" and ended_at is not None:
        raise ManifestValidationError("running capture must not have capture.ended_at")
    if status in {"incomplete", "failed"} and not capture.get("status_reason", "").strip():
        raise ManifestValidationError(
            f"{status} capture requires a non-empty capture.status_reason"
        )

    coverage = manifest["coverage"]
    source_names: set[str] = set()
    unavailable = set(coverage["unavailable_categories"])
    for source in coverage["event_sources"]:
        if source["name"] in source_names:
            raise ManifestValidationError(
                f"duplicate coverage.event_sources name '{source['name']}'"
            )
        source_names.add(source["name"])
        if source["state"] != "active":
            missing = set(source["categories"]) - unavailable
            if missing:
                category = sorted(missing)[0]
                raise ManifestValidationError(
                    f"coverage.unavailable_categories must include '{category}' "
                    f"for non-active source '{source['name']}'"
                )

    return manifest
