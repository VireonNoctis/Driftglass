"""Explicit, deterministic privacy redaction for exported report copies."""

from __future__ import annotations

import copy
import hashlib
import ipaddress
import re
from typing import Any
from urllib.parse import urlsplit

from ._schema import canonical_json
from .reporting import ReportExportError, validate_report


_WINDOWS_PATH = re.compile(r"(?i)(?<![\w])[A-Z]:\\[^\r\n\t<>\"|]+")
_UNIX_PATH = re.compile(r"(?<![\w:/])/(?:[^\s/<>\"'|]+/)*[^\s/<>\"'|]+")
_HOST_BOUNDARY_LEFT = r"(?<![\w.-])"
_HOST_BOUNDARY_RIGHT = r"(?![\w.-])"
_PROCESS_BOUNDARY_LEFT = r"(?<![\w.-])"
_PROCESS_BOUNDARY_RIGHT = r"(?![\w.-])"
_PATH_KEYS = {"filename", "file_name"}


def _add_token(values: dict[str, str], value: str, label: str) -> str:
    if value not in values:
        values[value] = f"[REDACTED_{label}_{len(values) + 1:03d}]"
    return values[value]


def _looks_like_path(value: str) -> bool:
    return value.startswith("/") and not value.startswith("//") or bool(
        re.match(r"(?i)^[A-Z]:\\", value)
    )


def _is_path_field(key: str, resource_kind: str | None) -> bool:
    normalized = key.lower().replace("-", "_")
    return (
        "path" in normalized
        or normalized in _PATH_KEYS
        or (normalized == "identifier" and resource_kind == "file")
    )


def _host_from_identifier(value: str) -> str | None:
    if not value:
        return None

    if "://" in value:
        try:
            hostname = urlsplit(value).hostname
        except ValueError:
            hostname = None
        return hostname or value

    if value.startswith("[") and "]" in value:
        return value[1:value.index("]")]

    try:
        ipaddress.ip_address(value)
        return value
    except ValueError:
        pass

    host, separator, port = value.rpartition(":")
    if separator and port.isdigit() and host:
        return host

    return value


def _collect_paths(
    value: Any,
    paths: dict[str, str],
    *,
    key: str = "",
    resource_kind: str | None = None,
    process_context: bool = False,
) -> None:
    if isinstance(value, dict):
        kind = value.get("kind") if isinstance(value.get("kind"), str) else resource_kind
        inside_process = process_context or key.lower() == "process"
        for child_key, child in value.items():
            _collect_paths(
                child,
                paths,
                key=child_key,
                resource_kind=kind,
                process_context=inside_process,
            )
    elif isinstance(value, list):
        for child in value:
            _collect_paths(
                child,
                paths,
                key=key,
                resource_kind=resource_kind,
                process_context=process_context,
            )
    elif isinstance(value, str):
        if _is_path_field(key, resource_kind) or _looks_like_path(value):
            _add_token(paths, value, "PATH")


def _collect_network_hosts(value: Any, hosts: dict[str, str], *, key: str = "") -> None:
    if isinstance(value, dict):
        is_network_resource = value.get("kind") == "network"
        if is_network_resource:
            for field in ("address", "identifier"):
                candidate = value.get(field)
                if isinstance(candidate, str) and candidate:
                    host = candidate if field == "address" else _host_from_identifier(candidate)
                    if host:
                        _add_token(hosts, host, "HOST")

        for child_key, child in value.items():
            normalized = child_key.lower()
            if (
                isinstance(child, str)
                and child
                and any(marker in normalized for marker in ("hostname", "host", "address", "_ip", "destination"))
            ):
                host = _host_from_identifier(child)
                if host:
                    _add_token(hosts, host, "HOST")
            else:
                _collect_network_hosts(child, hosts, key=child_key)
    elif isinstance(value, list):
        for child in value:
            _collect_network_hosts(child, hosts, key=key)


def _collect_process_names(
    value: Any,
    names: dict[str, str],
    *,
    key: str = "",
    process_context: bool = False,
) -> None:
    if isinstance(value, dict):
        inside_process = process_context or key.lower() == "process"
        for child_key, child in value.items():
            is_executable = child_key.lower() == "process_executable" or (
                inside_process and child_key.lower() == "executable"
            )
            if is_executable and isinstance(child, str) and child:
                _add_token(names, child, "PROCESS")
            else:
                _collect_process_names(
                    child,
                    names,
                    key=child_key,
                    process_context=inside_process,
                )
    elif isinstance(value, list):
        for child in value:
            _collect_process_names(child, names, key=key, process_context=process_context)


def _replace_bounded(value: str, replacements: dict[str, str], *, label: str) -> str:
    left = _HOST_BOUNDARY_LEFT if label == "HOST" else _PROCESS_BOUNDARY_LEFT
    right = _HOST_BOUNDARY_RIGHT if label == "HOST" else _PROCESS_BOUNDARY_RIGHT
    for original, token in sorted(replacements.items(), key=lambda item: len(item[0]), reverse=True):
        value = re.sub(left + re.escape(original) + right, lambda _: token, value)
    return value


def _replace_paths(value: str, paths: dict[str, str]) -> str:
    for original, token in sorted(paths.items(), key=lambda item: len(item[0]), reverse=True):
        value = value.replace(original, token)

    def replace_match(match: re.Match[str]) -> str:
        raw = match.group(0)
        suffix = ""
        while raw and raw[-1] in ".,;:)]}":
            suffix = raw[-1] + suffix
            raw = raw[:-1]
        if not raw:
            return match.group(0)
        return _add_token(paths, raw, "PATH") + suffix

    value = _WINDOWS_PATH.sub(replace_match, value)
    return _UNIX_PATH.sub(replace_match, value)


def _redact_value(
    value: Any,
    *,
    paths: dict[str, str],
    hosts: dict[str, str],
    process_names: dict[str, str],
    redact_paths: bool,
    redact_network: bool,
    redact_process_names: bool,
    key: str = "",
    resource_kind: str | None = None,
    process_context: bool = False,
) -> Any:
    if isinstance(value, dict):
        kind = value.get("kind") if isinstance(value.get("kind"), str) else resource_kind
        inside_process = process_context or key.lower() == "process"
        return {
            child_key: _redact_value(
                child,
                paths=paths,
                hosts=hosts,
                process_names=process_names,
                redact_paths=redact_paths,
                redact_network=redact_network,
                redact_process_names=redact_process_names,
                key=child_key,
                resource_kind=kind,
                process_context=inside_process,
            )
            for child_key, child in value.items()
        }
    if isinstance(value, list):
        return [
            _redact_value(
                child,
                paths=paths,
                hosts=hosts,
                process_names=process_names,
                redact_paths=redact_paths,
                redact_network=redact_network,
                redact_process_names=redact_process_names,
                key=key,
                resource_kind=resource_kind,
                process_context=process_context,
            )
            for child in value
        ]
    if not isinstance(value, str) or not value:
        return value

    result = value
    normalized_key = key.lower().replace("-", "_")
    if redact_paths:
        if _is_path_field(key, resource_kind) or _looks_like_path(result):
            return _add_token(paths, result, "PATH")
        result = _replace_paths(result, paths)

    if redact_network:
        if resource_kind == "network" and normalized_key == "address":
            host = result
            return hosts.get(host, _add_token(hosts, host, "HOST"))
        if resource_kind == "network" and normalized_key == "identifier":
            for host, token in sorted(hosts.items(), key=lambda item: len(item[0]), reverse=True):
                result = re.sub(
                    _HOST_BOUNDARY_LEFT + re.escape(host) + _HOST_BOUNDARY_RIGHT,
                    lambda _: token,
                    result,
                )
        else:
            result = _replace_bounded(result, hosts, label="HOST")

    if redact_process_names:
        is_executable = normalized_key == "process_executable" or (
            process_context and normalized_key == "executable"
        )
        if is_executable:
            return process_names.get(result, _add_token(process_names, result, "PROCESS"))
        result = _replace_bounded(result, process_names, label="PROCESS")

    return result


def redact_report(
    report: dict[str, Any],
    report_schema_path: str,
    *,
    paths: bool = False,
    network: bool = False,
    process_names: bool = False,
) -> dict[str, Any]:
    """Return a validated, redacted copy of a report.

    The source object is never mutated. Redacted values keep stable tokens within
    one export so repeated references remain linked. Finding/report hashes are
    recalculated for the changed content; local stored reports are untouched.
    """
    if not any((paths, network, process_names)):
        raise ReportExportError("select at least one redaction category")
    validate_report(report, report_schema_path)
    result = copy.deepcopy(report)

    path_tokens: dict[str, str] = {}
    host_tokens: dict[str, str] = {}
    process_tokens: dict[str, str] = {}
    if paths:
        _collect_paths(result, path_tokens)
    if network:
        _collect_network_hosts(result, host_tokens)
    if process_names:
        _collect_process_names(result, process_tokens)

    result = _redact_value(
        result,
        paths=path_tokens,
        hosts=host_tokens,
        process_names=process_tokens,
        redact_paths=paths,
        redact_network=network,
        redact_process_names=process_names,
    )

    for finding in result["findings"]:
        identity = {
            key: value
            for key, value in finding.items()
            if key not in {"finding_id", "interpretation"}
        }
        finding["finding_id"] = hashlib.sha256(
            canonical_json(identity).encode("utf-8")
        ).hexdigest()

    core = {key: value for key, value in result.items() if key != "report_id"}
    result["report_id"] = hashlib.sha256(
        canonical_json(core).encode("utf-8")
    ).hexdigest()
    validate_report(result, report_schema_path)
    return result
