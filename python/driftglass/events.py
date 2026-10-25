"""Loading and validating versioned Driftglass JSONL event records."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from ._schema import (
    instance_path,
    load_validator,
    reject_json_constant,
    safe_error_reason,
    unique_json_object,
)


class EventValidationError(ValueError):
    """Raised when an event stream or its schema is invalid."""


def load_jsonl(event_path: str | Path, schema_path: str | Path) -> list[dict[str, Any]]:
    """Load a non-empty JSONL event stream and validate each record.

    Blank lines, malformed JSON, invalid records, duplicate event IDs, and empty
    files are rejected. The schema's collection_quality field describes an
    individual record; run-level collection coverage must be tracked separately.
    """
    path = Path(event_path)
    try:
        validator = load_validator(Path(schema_path), "event schema")
    except ValueError as exc:
        raise EventValidationError(str(exc)) from exc

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
                    event = json.loads(
                        line,
                        parse_constant=reject_json_constant,
                        object_pairs_hook=unique_json_object,
                    )
                except json.JSONDecodeError as exc:
                    raise EventValidationError(
                        f"{path}:{line_number}: malformed JSON at column {exc.colno}"
                    ) from exc
                except ValueError as exc:
                    raise EventValidationError(
                        f"{path}:{line_number}: invalid JSON content ({exc})"
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
                        f"{path}:{line_number}: {instance_path(error.absolute_path)}: "
                        f"{safe_error_reason(error)}"
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
