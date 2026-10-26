"""Local SQLite history for validated Driftglass comparison reports."""

from __future__ import annotations

import json
import os
import re
import sqlite3
from pathlib import Path
from typing import Any

from ._schema import canonical_json, finite_json_float, reject_json_constant, unique_json_object
from .reporting import ReportExportError, validate_report

DATABASE_SCHEMA_VERSION = 1
MAX_HISTORY_REPORTS = 1000
MAX_HISTORY_REPORT_PAYLOAD_BYTES = 256 * 1024 * 1024
_REQUIRED_TABLES = {"reports", "runs", "report_runs", "findings"}
_REQUIRED_COLUMNS = {
    "reports": {
        "report_id", "schema_version", "experiment_id", "application_name", "status",
        "baseline_run_id", "candidate_run_id", "baseline_version", "candidate_version",
        "finding_count", "uncertainty_count", "report_json", "stored_at",
    },
    "runs": {
        "run_id", "experiment_id", "application_name", "version", "artifact_sha256",
        "capture_status",
    },
    "report_runs": {"report_id", "run_id", "role"},
    "findings": {
        "report_id", "finding_id", "rule_id", "change_type", "category", "summary",
        "finding_json",
    },
}


class StorageError(ValueError):
    """Raised when local report history is invalid, inconsistent, or unavailable."""


def _open_database(database_path: str | Path) -> sqlite3.Connection:
    path = Path(database_path)
    if not path.parent.is_dir():
        raise StorageError(f"database directory does not exist: '{path.parent}'")

    connection: sqlite3.Connection | None = None
    try:
        try:
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            os.close(descriptor)
        except FileExistsError:
            pass

        connection = sqlite3.connect(path, timeout=5.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        version = connection.execute("PRAGMA user_version").fetchone()[0]

        if version == 0:
            existing_tables = {
                row["name"]
                for row in connection.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
                )
            }
            if existing_tables:
                raise StorageError(
                    "database has tables but no Driftglass schema version; refusing to modify it"
                )
            _create_schema(connection)
        elif version != DATABASE_SCHEMA_VERSION:
            raise StorageError(
                f"unsupported database schema version {version}; expected {DATABASE_SCHEMA_VERSION}"
            )
        else:
            tables = {
                row["name"]
                for row in connection.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                )
            }
            missing = sorted(_REQUIRED_TABLES - tables)
            if missing:
                raise StorageError(
                    "database schema is incomplete; missing table(s): " + ", ".join(missing)
                )
            for table, required_columns in _REQUIRED_COLUMNS.items():
                columns = {
                    row["name"]
                    for row in connection.execute(f"PRAGMA table_info({table})")
                }
                missing_columns = sorted(required_columns - columns)
                if missing_columns:
                    raise StorageError(
                        f"database schema is incomplete; table '{table}' is missing column(s): "
                        + ", ".join(missing_columns)
                    )
        return connection
    except StorageError:
        if connection is not None:
            connection.close()
        raise
    except (OSError, sqlite3.Error) as exc:
        if connection is not None:
            connection.close()
        raise StorageError(
            f"could not open report database '{path}' ({type(exc).__name__})"
        ) from exc


def _create_schema(connection: sqlite3.Connection) -> None:
    connection.execute("BEGIN EXCLUSIVE")
    try:
        connection.execute(
            """
            CREATE TABLE reports (
                report_id TEXT PRIMARY KEY CHECK(length(report_id) = 64),
                schema_version INTEGER NOT NULL CHECK(schema_version = 1),
                experiment_id TEXT NOT NULL,
                application_name TEXT NOT NULL,
                status TEXT NOT NULL CHECK(status IN ('complete', 'partial')),
                baseline_run_id TEXT NOT NULL,
                candidate_run_id TEXT NOT NULL,
                baseline_version TEXT NOT NULL,
                candidate_version TEXT NOT NULL,
                finding_count INTEGER NOT NULL CHECK(finding_count >= 0),
                uncertainty_count INTEGER NOT NULL CHECK(uncertainty_count >= 0),
                report_json TEXT NOT NULL,
                stored_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
            )
            """
        )
        connection.execute(
            """
            CREATE TABLE runs (
                run_id TEXT PRIMARY KEY,
                experiment_id TEXT NOT NULL,
                application_name TEXT NOT NULL,
                version TEXT NOT NULL,
                artifact_sha256 TEXT,
                capture_status TEXT NOT NULL
                    CHECK(capture_status IN ('running', 'completed', 'incomplete', 'failed'))
            )
            """
        )
        connection.execute(
            """
            CREATE TABLE report_runs (
                report_id TEXT NOT NULL REFERENCES reports(report_id) ON DELETE CASCADE,
                run_id TEXT NOT NULL REFERENCES runs(run_id) ON DELETE RESTRICT,
                role TEXT NOT NULL CHECK(role IN ('baseline', 'candidate')),
                PRIMARY KEY(report_id, role),
                UNIQUE(report_id, run_id)
            )
            """
        )
        connection.execute(
            """
            CREATE TABLE findings (
                report_id TEXT NOT NULL REFERENCES reports(report_id) ON DELETE CASCADE,
                finding_id TEXT NOT NULL,
                rule_id TEXT NOT NULL,
                change_type TEXT NOT NULL CHECK(change_type IN ('added', 'removed', 'changed')),
                category TEXT NOT NULL,
                summary TEXT NOT NULL,
                finding_json TEXT NOT NULL,
                PRIMARY KEY(report_id, finding_id)
            )
            """
        )
        connection.execute("CREATE INDEX reports_history_idx ON reports(stored_at DESC, report_id DESC)")
        connection.execute("CREATE INDEX reports_experiment_idx ON reports(experiment_id, stored_at DESC)")
        connection.execute("PRAGMA user_version = 1")
        connection.commit()
    except sqlite3.Error:
        connection.rollback()
        raise


def _same_run(stored: sqlite3.Row, experiment_id: str, app_name: str, summary: dict[str, Any]) -> bool:
    return (
        stored["experiment_id"] == experiment_id
        and stored["application_name"] == app_name
        and stored["version"] == summary["version"]
        and stored["artifact_sha256"] == summary["artifact_sha256"]
        and stored["capture_status"] == summary["capture_status"]
    )


def save_report(
    database_path: str | Path,
    report: dict[str, Any],
    report_schema_path: str | Path,
) -> str:
    """Validate and persist a report, its run references, and finding index.

    Saving a report with the same ID and content is idempotent. A conflicting
    report or a run ID already stored with different metadata is rejected.
    """
    try:
        validate_report(report, report_schema_path)
    except ReportExportError as exc:
        raise StorageError(f"invalid report: {exc}") from exc

    report_json = json.dumps(report, sort_keys=True, indent=2, ensure_ascii=False)
    report_json_bytes = len(report_json.encode("utf-8"))
    if report_json_bytes > MAX_HISTORY_REPORT_PAYLOAD_BYTES:
        raise StorageError("report exceeds the local history report-payload budget")
    app_name = report["application_name"]
    run_summaries = (("baseline", report["baseline"]), ("candidate", report["candidate"]))
    connection = _open_database(database_path)
    try:
        connection.execute("BEGIN IMMEDIATE")
        existing = connection.execute(
            "SELECT report_json FROM reports WHERE report_id = ?",
            (report["report_id"],),
        ).fetchone()
        if existing is not None:
            saved_value = json.loads(existing["report_json"])
            if canonical_json(saved_value) != canonical_json(report):
                raise StorageError("report_id already exists with different content")
            connection.commit()
            return report["report_id"]

        stored_report_count = connection.execute("SELECT count(*) FROM reports").fetchone()[0]
        if stored_report_count >= MAX_HISTORY_REPORTS:
            raise StorageError(
                f"local history is full ({MAX_HISTORY_REPORTS} reports); "
                "delete selected reports before saving more"
            )
        stored_payload_bytes = connection.execute(
            "SELECT COALESCE(sum(length(CAST(report_json AS BLOB))), 0) FROM reports"
        ).fetchone()[0]
        if stored_payload_bytes + report_json_bytes > MAX_HISTORY_REPORT_PAYLOAD_BYTES:
            raise StorageError(
                "local history report-payload budget would be exceeded; "
                "delete selected reports before saving more"
            )

        for _, summary in run_summaries:
            connection.execute(
                """
                INSERT INTO runs (
                    run_id, experiment_id, application_name, version,
                    artifact_sha256, capture_status
                ) VALUES (?, ?, ?, ?, ?, ?)
                ON CONFLICT(run_id) DO NOTHING
                """,
                (
                    summary["run_id"], report["experiment_id"], app_name,
                    summary["version"], summary["artifact_sha256"], summary["capture_status"],
                ),
            )
            stored = connection.execute(
                "SELECT * FROM runs WHERE run_id = ?", (summary["run_id"],)
            ).fetchone()
            if stored is None or not _same_run(stored, report["experiment_id"], app_name, summary):
                raise StorageError(
                    f"run_id '{summary['run_id']}' already exists with different metadata"
                )

        connection.execute(
            """
            INSERT INTO reports (
                report_id, schema_version, experiment_id, application_name, status,
                baseline_run_id, candidate_run_id, baseline_version, candidate_version,
                finding_count, uncertainty_count, report_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                report["report_id"], report["schema_version"], report["experiment_id"],
                app_name, report["status"], report["baseline"]["run_id"],
                report["candidate"]["run_id"], report["baseline"]["version"],
                report["candidate"]["version"], len(report["findings"]),
                len(report["uncertainties"]), report_json,
            ),
        )

        for role, summary in run_summaries:
            connection.execute(
                "INSERT INTO report_runs(report_id, run_id, role) VALUES (?, ?, ?)",
                (report["report_id"], summary["run_id"], role),
            )

        for finding in report["findings"]:
            connection.execute(
                """
                INSERT INTO findings (
                    report_id, finding_id, rule_id, change_type, category, summary, finding_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    report["report_id"], finding["finding_id"], finding["rule_id"],
                    finding["change_type"], finding["category"], finding["summary"],
                    json.dumps(finding, sort_keys=True, ensure_ascii=False),
                ),
            )

        connection.commit()
        return report["report_id"]
    except StorageError:
        connection.rollback()
        raise
    except (sqlite3.Error, json.JSONDecodeError, TypeError, ValueError) as exc:
        connection.rollback()
        raise StorageError(f"could not store report ({type(exc).__name__})") from exc
    finally:
        connection.close()


def list_reports(database_path: str | Path, *, limit: int = 100) -> list[dict[str, Any]]:
    """List report-history summaries, newest stored reports first."""
    if type(limit) is not int or not 1 <= limit <= 1000:
        raise StorageError("limit must be an integer between 1 and 1000")

    connection = _open_database(database_path)
    try:
        rows = connection.execute(
            """
            SELECT report_id, experiment_id, application_name, status,
                   baseline_run_id, candidate_run_id, baseline_version,
                   candidate_version, finding_count, uncertainty_count, stored_at
            FROM reports
            ORDER BY stored_at DESC, report_id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
        return [dict(row) for row in rows]
    except sqlite3.Error as exc:
        raise StorageError(f"could not list stored reports ({type(exc).__name__})") from exc
    finally:
        connection.close()


def get_report(
    database_path: str | Path,
    report_id: str,
    report_schema_path: str | Path,
) -> dict[str, Any] | None:
    """Load and revalidate a saved report; return None when its ID is absent."""
    connection = _open_database(database_path)
    try:
        row = connection.execute(
            "SELECT report_json FROM reports WHERE report_id = ?", (report_id,)
        ).fetchone()
        if row is None:
            return None
        try:
            report = json.loads(
                row["report_json"],
                parse_constant=reject_json_constant,
                parse_float=finite_json_float,
                object_pairs_hook=unique_json_object,
            )
        except json.JSONDecodeError as exc:
            raise StorageError("stored report contains malformed JSON") from exc
        except ValueError as exc:
            raise StorageError("stored report contains invalid JSON content") from exc
        try:
            validate_report(report, report_schema_path)
        except ReportExportError as exc:
            raise StorageError(f"stored report failed validation: {exc}") from exc
        if report.get("report_id") != report_id:
            raise StorageError("stored report ID does not match its history key")
        return report
    except sqlite3.Error as exc:
        raise StorageError(f"could not read stored report ({type(exc).__name__})") from exc
    finally:
        connection.close()



def delete_report(database_path: str | Path, report_id: str) -> bool:
    """Delete one local report and its indexes; return False when it is absent.

    Report removal is explicit and transactional. Run rows are removed only if
    no remaining report references them. The function does not delete source
    JSONL or manifest files.
    """
    if not isinstance(report_id, str) or re.fullmatch(r"[a-f0-9]{64}", report_id) is None:
        raise StorageError("report_id must be a 64-character lowercase SHA-256 digest")

    connection = _open_database(database_path)
    try:
        connection.execute("BEGIN IMMEDIATE")
        cursor = connection.execute(
            "DELETE FROM reports WHERE report_id = ?",
            (report_id,),
        )
        if cursor.rowcount == 0:
            connection.commit()
            return False

        connection.execute(
            """
            DELETE FROM runs
            WHERE NOT EXISTS (
                SELECT 1 FROM report_runs WHERE report_runs.run_id = runs.run_id
            )
            """
        )
        connection.commit()
        return True
    except sqlite3.Error as exc:
        connection.rollback()
        raise StorageError(f"could not delete stored report ({type(exc).__name__})") from exc
    finally:
        connection.close()
