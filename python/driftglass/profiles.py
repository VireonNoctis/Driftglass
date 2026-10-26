"""Validated user-supplied deterministic comparison rule profiles."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
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


MAX_RULE_PROFILE_FILE_BYTES = 1024 * 1024

RULE_CATEGORIES = ("process", "network", "file", "system")


class RuleProfileError(ValueError):
    """Raised when a comparison rule profile is missing, malformed, or invalid."""


@dataclass(frozen=True)
class RuleProfile:
    profile_id: str
    included_categories: tuple[str, ...]
    ignored_attributes: tuple[str, ...]
    sha256: str

    def report_metadata(self) -> dict[str, Any]:
        """Return the immutable, auditable profile summary embedded in a report."""
        return {
            "profile_id": self.profile_id,
            "sha256": self.sha256,
            "included_categories": list(self.included_categories),
            "ignored_attributes": list(self.ignored_attributes),
        }


def _normalized_profile(value: dict[str, Any]) -> RuleProfile:
    normalized = {
        "schema_version": 1,
        "profile_id": value["profile_id"],
        "included_categories": sorted(value["included_categories"]),
        "ignored_attributes": sorted(value["ignored_attributes"]),
    }
    digest = hashlib.sha256(canonical_json(normalized).encode("utf-8")).hexdigest()
    return RuleProfile(
        profile_id=normalized["profile_id"],
        included_categories=tuple(normalized["included_categories"]),
        ignored_attributes=tuple(normalized["ignored_attributes"]),
        sha256=digest,
    )


def default_rule_profile() -> RuleProfile:
    """The built-in v1 profile preserves the original comparison behaviour."""
    return _normalized_profile({
        "schema_version": 1,
        "profile_id": "default-v1",
        "included_categories": list(RULE_CATEGORIES),
        "ignored_attributes": [],
    })


def load_rule_profile(
    profile_path: str | Path,
    schema_path: str | Path,
) -> RuleProfile:
    """Load a strict JSON profile; list ordering does not affect its digest."""
    path = Path(profile_path).expanduser()
    try:
        with path.open("rb") as stream:
            raw = stream.read(MAX_RULE_PROFILE_FILE_BYTES + 1)
        if len(raw) > MAX_RULE_PROFILE_FILE_BYTES:
            raise RuleProfileError(
                f"rule profile exceeds the {MAX_RULE_PROFILE_FILE_BYTES}-byte file limit"
            )
        value = json.loads(
            raw.decode("utf-8"),
            parse_constant=reject_json_constant,
            parse_float=finite_json_float,
            object_pairs_hook=unique_json_object,
        )
    except RuleProfileError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        if isinstance(exc, json.JSONDecodeError):
            reason = f"malformed JSON at line {exc.lineno}, column {exc.colno}"
        elif isinstance(exc, ValueError) and str(exc) in {
            "duplicate JSON object keys are not valid",
            "non-finite numeric constants are not valid JSON",
            "non-finite numeric values are not valid JSON",
        }:
            reason = str(exc)
        else:
            reason = type(exc).__name__
        raise RuleProfileError(
            f"could not read rule profile '{path}' ({reason})"
        ) from exc

    try:
        validator = load_validator(Path(schema_path), "rule-profile schema")
    except ValueError as exc:
        raise RuleProfileError(str(exc)) from exc

    errors = sorted(
        validator.iter_errors(value),
        key=lambda error: (
            tuple(str(part) for part in error.absolute_path),
            str(error.validator),
            error.message,
        ),
    )
    if errors:
        error = errors[0]
        raise RuleProfileError(
            f"{instance_path(error.absolute_path)}: {safe_error_reason(error)}"
        )
    return _normalized_profile(value)
