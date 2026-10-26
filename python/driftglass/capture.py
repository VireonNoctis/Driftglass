"""Bounded Linux capture coordination for an already-running process.

This module starts only the Driftglass observer. It never launches or controls
the application being observed.
"""

from __future__ import annotations

import json
import os
import platform
import re
import subprocess
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from ._schema import load_validator
from .events import EventValidationError, load_jsonl

CATEGORIES = ("process", "network", "file", "system", "persistence")
OBSERVER_CATEGORIES = ("process", "network")
MAX_DURATION_SECONDS = 3600
MIN_INTERVAL_MS = 100
MAX_INTERVAL_MS = 10_000


class CaptureError(ValueError):
    """Raised when a capture cannot be prepared or completed reliably."""


@dataclass(frozen=True)
class CaptureResult:
    manifest: dict[str, Any]
    event_count: int
    event_path: Path
    manifest_path: Path


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def _required_text(name: str, value: str, maximum: int) -> str:
    if not isinstance(value, str) or not value.strip() or len(value.strip()) > maximum:
        raise CaptureError(f"{name} must be a non-empty string of at most {maximum} characters")
    return value.strip()


def _resolve_output_path(value: str | Path, label: str) -> Path:
    raw_path = Path(value).expanduser()
    try:
        path = raw_path.resolve()
    except (OSError, RuntimeError) as exc:
        raise CaptureError(f"cannot resolve {label} path ({type(exc).__name__})") from exc
    if raw_path.exists() or raw_path.is_symlink() or path.exists():
        raise CaptureError(f"{label} already exists; capture outputs are never overwritten")
    if not path.parent.is_dir():
        raise CaptureError(f"{label} parent directory does not exist")
    return path


def _write_new_json(path: Path, value: dict[str, Any]) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True, indent=2, ensure_ascii=False, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
    except (OSError, TypeError, ValueError) as exc:
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass
        raise CaptureError(f"could not write run manifest ({type(exc).__name__})") from exc


def _event_records(path: Path, schema_path: Path) -> tuple[list[dict[str, Any]], str | None]:
    if not path.is_file():
        return [], "observer did not create an event stream"
    try:
        return load_jsonl(path, schema_path), None
    except EventValidationError:
        return [], "observer output did not pass JSONL validation"


def _manifest_errors(
    records: list[dict[str, Any]],
    *,
    return_code: int | None,
    timed_out: bool,
    launch_error: str | None,
    stream_error: str | None,
    started: bool,
    finished: bool,
) -> list[str]:
    errors: list[str] = []
    if launch_error:
        errors.append(f"observer could not be started ({launch_error})")
    elif timed_out:
        errors.append("observer exceeded the coordinator time limit")
    elif return_code not in (None, 0):
        errors.append(f"observer exited with status {return_code}")
    if stream_error:
        errors.append(stream_error)
    if not started:
        errors.append("event stream did not contain capture_started")
    if not finished:
        errors.append("event stream did not contain capture_finished")

    for record in records:
        if record.get("category") != "coverage":
            continue
        action = record.get("action")
        attributes = record.get("attributes") or {}
        category = attributes.get("category")
        if action == "coverage_gap":
            suffix = f" for {category}" if category in OBSERVER_CATEGORIES else ""
            errors.append(f"observer reported a coverage gap{suffix}")
        if action == "capture_incomplete":
            errors.append("observer reported an incomplete capture")
        if record.get("collection_quality") != "complete":
            errors.append("observer marked one or more coverage records as partial-quality")

    return list(dict.fromkeys(errors))


def capture_run(
    *,
    observer_path: str | Path,
    pid: int,
    duration_seconds: int,
    interval_ms: int,
    experiment_id: str,
    role: str,
    application_name: str,
    application_version: str,
    event_output_path: str | Path,
    manifest_output_path: str | Path,
    event_schema_path: str | Path,
    manifest_schema_path: str | Path,
    scenario: str,
    artifact_sha256: str | None = None,
    package_id: str | None = None,
    profile: str = "linux-procfs-polling",
    snapshot_id: str | None = None,
) -> CaptureResult:
    """Run the Linux observer against an existing PID and write a run manifest.

    An observer failure still produces a failed/incomplete manifest whenever
    the output path and schema are usable. The function then raises CaptureError
    so callers cannot accidentally treat a partial capture as successful.
    """
    if platform.system() != "Linux":
        raise CaptureError("the configured procfs observer supports Linux only")
    if type(pid) is not int or pid <= 0:
        raise CaptureError("pid must be a positive integer")
    if type(duration_seconds) is not int or not 1 <= duration_seconds <= MAX_DURATION_SECONDS:
        raise CaptureError(f"duration_seconds must be between 1 and {MAX_DURATION_SECONDS}")
    if type(interval_ms) is not int or not MIN_INTERVAL_MS <= interval_ms <= MAX_INTERVAL_MS:
        raise CaptureError(f"interval_ms must be between {MIN_INTERVAL_MS} and {MAX_INTERVAL_MS}")
    if role not in {"baseline", "candidate"}:
        raise CaptureError("role must be baseline or candidate")

    experiment_id = _required_text("experiment_id", experiment_id, 128)
    application_name = _required_text("application_name", application_name, 256)
    application_version = _required_text("application_version", application_version, 128)
    scenario = _required_text("scenario", scenario, 1024)
    profile = _required_text("profile", profile, 128)
    if package_id is not None:
        package_id = _required_text("package_id", package_id, 512)
    if snapshot_id is not None:
        snapshot_id = _required_text("snapshot_id", snapshot_id, 256)
    if artifact_sha256 is not None and not re.fullmatch(r"[a-f0-9]{64}", artifact_sha256):
        raise CaptureError("artifact_sha256 must be a lowercase 64-character SHA-256 digest")

    try:
        observer = Path(observer_path).expanduser().resolve(strict=True)
        event_schema = Path(event_schema_path).expanduser().resolve(strict=True)
        manifest_schema = Path(manifest_schema_path).expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise CaptureError(f"cannot resolve observer or schema path ({type(exc).__name__})") from exc
    if not observer.is_file() or not os.access(observer, os.X_OK):
        raise CaptureError("observer_path must be an executable file")

    try:
        load_validator(event_schema, "event schema")
        manifest_validator = load_validator(manifest_schema, "run manifest schema")
    except ValueError as exc:
        raise CaptureError("configured schema could not be loaded and validated") from exc

    event_path = _resolve_output_path(event_output_path, "event output")
    manifest_path = _resolve_output_path(manifest_output_path, "manifest output")
    if event_path == manifest_path:
        raise CaptureError("event output and manifest output must be different paths")
    protected_paths = {observer, event_schema, manifest_schema}
    if event_path in protected_paths or manifest_path in protected_paths:
        raise CaptureError("capture outputs must not replace the observer or a schema file")

    started_at = _timestamp()
    command = [
        str(observer),
        "--pid", str(pid),
        "--duration-seconds", str(duration_seconds),
        "--interval-ms", str(interval_ms),
        "--output", str(event_path),
    ]
    return_code: int | None = None
    timed_out = False
    launch_error: str | None = None
    stderr_text = ""
    try:
        completed = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            timeout=duration_seconds + 15,
            check=False,
        )
        return_code = completed.returncode
        stderr_text = completed.stderr or ""
    except subprocess.TimeoutExpired:
        timed_out = True
    except OSError as exc:
        launch_error = type(exc).__name__
        stderr_text = str(exc)

    records, stream_error = _event_records(event_path, event_schema)
    started = any(
        record.get("category") == "coverage" and record.get("action") == "capture_started"
        for record in records
    )
    finished = any(
        record.get("category") == "coverage" and record.get("action") == "capture_finished"
        for record in records
    )
    errors = _manifest_errors(
        records,
        return_code=return_code,
        timed_out=timed_out,
        launch_error=launch_error,
        stream_error=stream_error,
        started=started,
        finished=finished,
    )

    normal_completion = (
        return_code == 0
        and not timed_out
        and launch_error is None
        and stream_error is None
        and started
        and finished
    )
    status = "completed" if normal_completion else ("incomplete" if records else "failed")
    source_state = "active" if started else "failed"
    unavailable_categories = ["file", "system", "persistence"]
    if source_state != "active":
        unavailable_categories = list(CATEGORIES)

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "run_id": f"run-{uuid.uuid4().hex}",
        "experiment_id": experiment_id,
        "role": role,
        "application": {
            "name": application_name,
            "version": application_version,
            "artifact_sha256": artifact_sha256,
            "package_id": package_id,
        },
        "environment": {
            "os": platform.system(),
            "os_version": platform.release(),
            "architecture": platform.machine() or "unknown",
            "snapshot_id": snapshot_id,
        },
        "capture": {
            "status": status,
            "started_at": started_at,
            "ended_at": _timestamp(),
            "scenario": scenario,
            "profile": profile,
        },
        "coverage": {
            "event_sources": [
                {
                    "name": "linux-procfs-polling",
                    "state": source_state,
                    "categories": list(OBSERVER_CATEGORIES),
                }
            ],
            "unavailable_categories": unavailable_categories,
            "dropped_events": 0,
            "errors": errors,
        },
    }
    if status in {"incomplete", "failed"}:
        manifest["capture"]["status_reason"] = (
            "The observer did not complete the capture contract; review coverage errors and event records."
            if status == "incomplete"
            else "The observer did not produce a valid capture stream."
        )

    schema_errors = sorted(
        manifest_validator.iter_errors(manifest),
        key=lambda error: (
            tuple(str(part) for part in error.absolute_path),
            str(error.validator),
            error.message,
        ),
    )
    if schema_errors:
        raise CaptureError("generated run manifest failed schema validation")

    _write_new_json(manifest_path, manifest)

    if not normal_completion:
        detail = ""
        printable = "".join(char for char in stderr_text if char.isprintable() or char in "\t\r\n")
        printable = " ".join(printable.split())[:240]
        if printable:
            detail = f"; observer diagnostic: {printable}"
        raise CaptureError(
            f"capture status is {status}; a run manifest was written to {manifest_path}{detail}"
        )

    return CaptureResult(
        manifest=manifest,
        event_count=len(records),
        event_path=event_path,
        manifest_path=manifest_path,
    )
