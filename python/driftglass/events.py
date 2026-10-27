"""Loading and validating versioned Driftglass JSONL event records."""

from __future__ import annotations

import json
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


MAX_EVENT_STREAM_BYTES = 64 * 1024 * 1024
MAX_EVENT_LINE_BYTES = 1024 * 1024
MAX_EVENT_RECORDS = 250_000


class EventValidationError(ValueError):
    """Raised when an event stream or its schema is invalid."""


def load_jsonl(event_path: str | Path, schema_path: str | Path) -> list[dict[str, Any]]:
    """Load a non-empty, size-bounded JSONL event stream and validate each record.

    Blank lines, malformed JSON, invalid records, duplicate event IDs, over-limit
    streams, and empty files are rejected. Run-level coverage is tracked separately.
    """
    path = Path(event_path)
    try:
        validator = load_validator(Path(schema_path), "event schema")
    except ValueError as exc:
        raise EventValidationError(str(exc)) from exc

    events: list[dict[str, Any]] = []
    seen_ids: dict[str, int] = {}

    try:
        if path.stat().st_size > MAX_EVENT_STREAM_BYTES:
            raise EventValidationError(
                f"event file exceeds the {MAX_EVENT_STREAM_BYTES}-byte stream limit"
            )

        with path.open("rb") as stream:
            line_number = 0
            total_bytes = 0
            while True:
                # Bound each read before decoding JSON so one huge line does not
                # get loaded into memory in full.
                line = stream.readline(MAX_EVENT_LINE_BYTES + 1)
                if not line:
                    break
                line_number += 1
                line_bytes = len(line)
                if line_bytes > MAX_EVENT_LINE_BYTES:
                    raise EventValidationError(
                        f"{path}:{line_number}: record exceeds the {MAX_EVENT_LINE_BYTES}-byte line limit"
                    )
                total_bytes += line_bytes
                if total_bytes > MAX_EVENT_STREAM_BYTES:
                    raise EventValidationError(
                        f"{path}: event stream exceeds the {MAX_EVENT_STREAM_BYTES}-byte size limit"
                    )
                if line_number > MAX_EVENT_RECORDS:
                    raise EventValidationError(
                        f"{path}: event stream exceeds the {MAX_EVENT_RECORDS}-record limit"
                    )
                if not line.strip():
                    raise EventValidationError(
                        f"{path}:{line_number}: blank lines are not valid JSONL records"
                    )

                try:
                    decoded_line = line.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise EventValidationError(
                        f"{path}:{line_number}: record contains invalid UTF-8"
                    ) from exc

                try:
                    event = json.loads(
                        decoded_line,
                        parse_constant=reject_json_constant,
                        parse_float=finite_json_float,
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
