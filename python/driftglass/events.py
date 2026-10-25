"""Loading and validating versioned Driftglass JSONL event records."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import SchemaError


class EventValidationError(ValueError):
    """Raised when an event stream or its schema is invalid."""


def _load_validator(schema_path: Path) -> Draft202012Validator:
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise EventValidationError(
            f"could not read event schema '{schema_path}' ({type(exc).__name__})"
        ) from exc

    try:
        Draft202012Validator.check_schema(schema)
    except SchemaError as exc:
        raise EventValidationError(
            f"event schema '{schema_path}' is invalid ({exc.validator})"
        ) from exc

    return Draft202012Validator(schema, format_checker=FormatChecker())


def _instance_path(path: Any) -> str:
    result = "$"
    for part in path:
        if isinstance(part, int):
            result += f"[{part}]"
        else:
            result += f".{part}"
    return result


def _safe_error_reason(error: Any) -> str:
    """Describe the failed constraint without echoing potentially sensitive values."""
    if error.validator == "required" and isinstance(error.instance, dict):
        missing = sorted(set(error.validator_value) - set(error.instance))
        if missing:
            return f"missing required field '{missing[0]}'"
    if error.validator == "type":
        return f"expected type {error.validator_value}"
    if error.validator == "enum":
        return "value is not in the allowed set"
    if error.validator == "const":
        return f"expected constant {error.validator_value!r}"
    if error.validator == "format":
        return f"expected format '{error.validator_value}'"
    if error.validator:
        return f"'{error.validator}' constraint failed"
    return "schema constraint failed"


def load_jsonl(event_path: str | Path, schema_path: str | Path) -> list[dict[str, Any]]:
    """Load a non-empty JSONL event stream and validate each record.

    Blank lines, malformed JSON, invalid records, duplicate event IDs, and empty
    files are rejected. The schema's collection_quality field describes an
    individual record; run-level collection coverage must be tracked separately.
    """
    path = Path(event_path)
    validator = _load_validator(Path(schema_path))
    events: list[dict[str, Any]] = []
    seen_ids: dict[str, int] = {}

    try:
        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, start=1):
                if not line.strip():
                    raise EventValidationError(
                        f"{path}:{line_number}: blank lines are not valid JSONL records"
                    )

                try:
                    event = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise EventValidationError(
                        f"{path}:{line_number}: malformed JSON at column {exc.colno}"
                    ) from exc

                errors = sorted(
                    validator.iter_errors(event),
                    key=lambda error: (
                        tuple(str(part) for part in error.absolute_path),
                        str(error.validator),
                        error.message,
                    ),
                )
                if errors:
                    error = errors[0]
                    raise EventValidationError(
                        f"{path}:{line_number}: {_instance_path(error.absolute_path)}: "
                        f"{_safe_error_reason(error)}"
                    )

                event_id = event["event_id"]
                if event_id in seen_ids:
                    raise EventValidationError(
                        f"{path}:{line_number}: duplicate event_id "
                        f"(first seen on line {seen_ids[event_id]})"
                    )
                seen_ids[event_id] = line_number
                events.append(event)
    except EventValidationError:
        raise
    except (OSError, UnicodeError) as exc:
        raise EventValidationError(
            f"could not read event file '{path}' ({type(exc).__name__})"
        ) from exc

    if not events:
        raise EventValidationError(
            f"event file '{path}' contains no records; empty telemetry is not evidence of no activity"
        )

    return events
