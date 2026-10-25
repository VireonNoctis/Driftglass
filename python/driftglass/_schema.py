"""Shared helpers for loading JSON Schema contracts."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import SchemaError


def load_validator(schema_path: Path, contract_name: str) -> Draft202012Validator:
    """Load and check a Draft 2020-12 schema before validating instances."""
    try:
        schema = json.loads(
            schema_path.read_text(encoding="utf-8"),
            parse_constant=reject_json_constant,
            parse_float=finite_json_float,
            object_pairs_hook=unique_json_object,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(
            f"could not read {contract_name} '{schema_path}' ({type(exc).__name__})"
        ) from exc
    except ValueError as exc:
        raise ValueError(
            f"could not read {contract_name} '{schema_path}' ({exc})"
        ) from exc

    try:
        Draft202012Validator.check_schema(schema)
    except SchemaError as exc:
        raise ValueError(
            f"{contract_name} '{schema_path}' is invalid ({exc.validator})"
        ) from exc

    return Draft202012Validator(schema, format_checker=FormatChecker())


def instance_path(path: Any) -> str:
    """Format a JSON Schema instance path without exposing field values."""
    result = "$"
    for part in path:
        result += f"[{part}]" if isinstance(part, int) else f".{part}"
    return result


def safe_error_reason(error: Any) -> str:
    """Describe a schema failure without echoing potentially sensitive values."""
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


def reject_json_constant(_value: str) -> None:
    """Reject JavaScript-style NaN and Infinity tokens outside standard JSON."""
    raise ValueError("non-finite numeric constants are not valid JSON")


def finite_json_float(value: str) -> float:
    """Reject JSON numeric literals that overflow Python's finite float range."""
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("non-finite numeric values are not valid JSON")
    return result


def unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """Reject duplicate keys rather than silently accepting the last value."""
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON object keys are not valid")
        result[key] = value
    return result



def canonical_json(value: Any) -> str:
    """Serialize JSON deterministically, rejecting non-standard numeric constants."""
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
        allow_nan=False,
    )
