import json
import sqlite3
from pathlib import Path

import pytest

from driftglass.comparison import compare_loaded_runs
from driftglass.storage import StorageError, delete_report, get_report, list_reports, save_report


ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"


def manifest(role, run_id, version, artifact_hash):
    return {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": "storage-test",
        "role": role,
        "application": {"name": "Stored App", "version": version, "artifact_sha256": artifact_hash, "package_id": "org.example.stored"},
        "environment": {"os": "Linux", "os_version": "fixture", "architecture": "x86_64", "snapshot_id": "snapshot-a"},
        "capture": {"status": "completed", "started_at": "2026-10-25T10:00:00Z", "ended_at": "2026-10-25T10:01:00Z", "scenario": "launch", "profile": "minimal"},
        "coverage": {
            "event_sources": [
                {"name": "process", "state": "active", "categories": ["process"]},
                {"name": "network", "state": "active", "categories": ["network"]},
                {"name": "file", "state": "active", "categories": ["file"]},
                {"name": "system", "state": "active", "categories": ["system"]},
                {"name": "persistence", "state": "active", "categories": ["persistence"]}
            ],
            "unavailable_categories": [],
            "dropped_events": 0,
            "errors": []
        }
    }


def event(event_id, action="connection_attempt"):
    return {
        "schema_version": 1,
        "event_id": event_id,
        "timestamp": "2026-10-25T10:00:30Z",
        "category": "network",
        "action": action,
        "source": {"collector": "fixture/1", "event_ref": "source-" + event_id},
        "collection_quality": "complete",
        "process": {"pid": 100, "parent_pid": 1, "executable": "stored-app"},
        "resource": {"kind": "network", "identifier": "192.0.2.2:50000", "address": "198.51.100.10", "port": 443, "protocol": "tcp"},
        "attributes": {"state": "ESTABLISHED"}
    }


def make_report(base_run="run-base", base_version="1.0", base_hash="a" * 64, candidate_run="run-candidate", candidate_version="2.0", candidate_hash="b" * 64):
    base = manifest("baseline", base_run, base_version, base_hash)
    candidate = manifest("candidate", candidate_run, candidate_version, candidate_hash)
    return compare_loaded_runs(base, candidate, [event("evt-base")], [event("evt-candidate", "connection_established")])


def test_saves_and_reloads_report_and_indexes_findings(tmp_path):
    db = tmp_path / "history.db"
    value = make_report()
    assert save_report(db, value, REPORT_SCHEMA) == value["report_id"]
    assert get_report(db, value["report_id"], REPORT_SCHEMA) == value
    history = list_reports(db)
    assert len(history) == 1
    assert history[0]["report_id"] == value["report_id"]
    assert history[0]["finding_count"] == len(value["findings"])
    assert history[0]["baseline_run_id"] == "run-base"


def test_saving_identical_report_is_idempotent(tmp_path):
    db = tmp_path / "history.db"
    value = make_report()
    save_report(db, value, REPORT_SCHEMA)
    save_report(db, value, REPORT_SCHEMA)
    assert len(list_reports(db)) == 1


def test_rejects_a_run_id_reused_with_conflicting_metadata_atomically(tmp_path):
    db = tmp_path / "history.db"
    first = make_report()
    save_report(db, first, REPORT_SCHEMA)
    conflict = make_report(base_run="run-base", base_version="9.9", base_hash="c" * 64, candidate_run="run-new", candidate_version="3.0", candidate_hash="d" * 64)
    with pytest.raises(StorageError, match="run_id 'run-base' already exists"):
        save_report(db, conflict, REPORT_SCHEMA)
    assert len(list_reports(db)) == 1
    assert get_report(db, conflict["report_id"], REPORT_SCHEMA) is None


def test_rejects_modified_report_id_before_creating_database(tmp_path):
    db = tmp_path / "history.db"
    value = make_report()
    value["report_id"] = "0" * 64
    with pytest.raises(StorageError, match="invalid report"):
        save_report(db, value, REPORT_SCHEMA)
    assert not db.exists()


def test_refuses_incomplete_versioned_database_columns(tmp_path):
    db = tmp_path / "incomplete.db"
    connection = sqlite3.connect(db)
    connection.execute("CREATE TABLE reports (report_id TEXT)")
    connection.execute("CREATE TABLE runs (run_id TEXT)")
    connection.execute("CREATE TABLE report_runs (report_id TEXT, run_id TEXT, role TEXT)")
    connection.execute("CREATE TABLE findings (report_id TEXT, finding_id TEXT)")
    connection.execute("PRAGMA user_version = 1")
    connection.commit()
    connection.close()

    with pytest.raises(StorageError, match="table 'reports' is missing column"):
        list_reports(db)


def test_refuses_unknown_database_schema_version(tmp_path):
    db = tmp_path / "future.db"
    connection = sqlite3.connect(db)
    connection.execute("PRAGMA user_version = 99")
    connection.close()
    with pytest.raises(StorageError, match="unsupported database schema version 99"):
        list_reports(db)


def test_rejects_overflowed_float_literals_in_stored_json(tmp_path):
    db = tmp_path / "history.db"
    report = make_report()
    save_report(db, report, REPORT_SCHEMA)
    connection = sqlite3.connect(db)
    stored = connection.execute(
        "SELECT report_json FROM reports WHERE report_id = ?", (report["report_id"],)
    ).fetchone()[0]
    assert '"attributes": {' in stored
    stored = stored.replace(
        '"attributes": {',
        '"attributes": {"synthetic_metric": 1e999, ',
        1,
    )
    connection.execute(
        "UPDATE reports SET report_json = ? WHERE report_id = ?",
        (stored, report["report_id"]),
    )
    connection.commit()
    connection.close()

    with pytest.raises(StorageError, match="stored report contains invalid JSON content"):
        get_report(db, report["report_id"], REPORT_SCHEMA)


def test_rejects_duplicate_keys_in_stored_report_json(tmp_path):
    db = tmp_path / "history.db"
    report = make_report()
    save_report(db, report, REPORT_SCHEMA)
    connection = sqlite3.connect(db)
    duplicated = json.dumps(report, sort_keys=True)[:-1] + ', "report_id": "ambiguous"}'
    connection.execute(
        "UPDATE reports SET report_json = ? WHERE report_id = ?",
        (duplicated, report["report_id"]),
    )
    connection.commit()
    connection.close()

    with pytest.raises(StorageError, match="stored report contains invalid JSON content"):
        get_report(db, report["report_id"], REPORT_SCHEMA)


def test_rejects_report_json_stored_under_another_history_key(tmp_path):
    db = tmp_path / "history.db"
    indexed_report = make_report()
    other_report = make_report(
        base_run="other-base",
        candidate_run="other-candidate",
        base_hash="c" * 64,
        candidate_hash="d" * 64,
    )
    save_report(db, indexed_report, REPORT_SCHEMA)
    connection = sqlite3.connect(db)
    connection.execute(
        "UPDATE reports SET report_json = ? WHERE report_id = ?",
        (json.dumps(other_report, sort_keys=True), indexed_report["report_id"]),
    )
    connection.commit()
    connection.close()

    with pytest.raises(StorageError, match="does not match its history key"):
        get_report(db, indexed_report["report_id"], REPORT_SCHEMA)


def test_refuses_unversioned_database_with_existing_tables(tmp_path):
    db = tmp_path / "foreign.db"
    connection = sqlite3.connect(db)
    connection.execute("CREATE TABLE existing_data (value TEXT)")
    connection.commit()
    connection.close()
    with pytest.raises(StorageError, match="has tables but no Driftglass schema version"):
        list_reports(db)


def test_validates_history_limit_and_missing_report(tmp_path):
    db = tmp_path / "history.db"
    save_report(db, make_report(), REPORT_SCHEMA)
    assert get_report(db, "not-found", REPORT_SCHEMA) is None
    with pytest.raises(StorageError, match="limit must be an integer"):
        list_reports(db, limit=True)
    with pytest.raises(StorageError, match="limit must be an integer"):
        list_reports(db, limit=0)


def test_does_not_create_missing_parent_directory(tmp_path):
    db = tmp_path / "missing" / "history.db"
    with pytest.raises(StorageError, match="database directory does not exist"):
        list_reports(db)
    assert not db.parent.exists()



def test_delete_report_removes_indexes_and_unreferenced_runs_transactionally(tmp_path):
    db = tmp_path / "history.db"
    report = make_report()
    save_report(db, report, REPORT_SCHEMA)

    assert delete_report(db, report["report_id"]) is True
    assert get_report(db, report["report_id"], REPORT_SCHEMA) is None
    assert list_reports(db) == []

    connection = sqlite3.connect(db)
    assert connection.execute("SELECT count(*) FROM reports").fetchone()[0] == 0
    assert connection.execute("SELECT count(*) FROM report_runs").fetchone()[0] == 0
    assert connection.execute("SELECT count(*) FROM findings").fetchone()[0] == 0
    assert connection.execute("SELECT count(*) FROM runs").fetchone()[0] == 0
    connection.close()

    assert delete_report(db, report["report_id"]) is False


def test_delete_report_keeps_runs_referenced_by_another_report(tmp_path):
    db = tmp_path / "history.db"
    first = make_report(base_run="shared-base", candidate_run="candidate-one")
    second = make_report(base_run="shared-base", candidate_run="candidate-two")

    save_report(db, first, REPORT_SCHEMA)
    save_report(db, second, REPORT_SCHEMA)
    assert delete_report(db, first["report_id"]) is True

    assert list_reports(db) and [row["report_id"] for row in list_reports(db)] == [second["report_id"]]
    assert get_report(db, second["report_id"], REPORT_SCHEMA) == second

    connection = sqlite3.connect(db)
    run_ids = {row[0] for row in connection.execute("SELECT run_id FROM runs")}
    connection.close()
    assert run_ids == {"shared-base", "candidate-two"}


def test_delete_report_rejects_malformed_id_before_opening_database(tmp_path):
    db = tmp_path / "history.db"
    with pytest.raises(StorageError, match="64-character lowercase SHA-256"):
        delete_report(db, "not-a-report-id")
    assert not db.exists()



def test_history_enforces_report_count_and_payload_budget_atomically(tmp_path, monkeypatch):
    import driftglass.storage as storage

    db = tmp_path / "history.db"
    first = make_report(base_run="first-base", candidate_run="first-candidate")
    second = make_report(base_run="second-base", candidate_run="second-candidate")
    save_report(db, first, REPORT_SCHEMA)

    monkeypatch.setattr(storage, "MAX_HISTORY_REPORTS", 1)
    with pytest.raises(StorageError, match="local history is full"):
        save_report(db, second, REPORT_SCHEMA)
    assert [item["report_id"] for item in list_reports(db)] == [first["report_id"]]

    monkeypatch.setattr(storage, "MAX_HISTORY_REPORTS", 1000)
    monkeypatch.setattr(storage, "MAX_HISTORY_REPORT_PAYLOAD_BYTES", 1)
    with pytest.raises(StorageError, match="report-payload budget"):
        save_report(db, second, REPORT_SCHEMA)
    assert [item["report_id"] for item in list_reports(db)] == [first["report_id"]]

def test_rejects_symlink_database_path_without_modifying_target(tmp_path):
    target = tmp_path / "target.db"
    target.write_bytes(b"")
    alias = tmp_path / "history.db"
    try:
        alias.symlink_to(target)
    except OSError:
        pytest.skip("symlink creation is not permitted")

    original = target.read_bytes()
    with pytest.raises(StorageError, match="symlinks and special filesystem entries"):
        list_reports(alias)

    assert target.read_bytes() == original




def test_history_database_size_limit_bounds_database_growth(tmp_path, monkeypatch):
    import driftglass.storage as storage

    db = tmp_path / "history.db"
    limit = 256 * 1024
    monkeypatch.setattr(storage, "MAX_HISTORY_DATABASE_BYTES", limit)

    for index in range(100):
        report = make_report(
            base_run=f"base-{index}",
            base_version=f"1.{index}",
            base_hash=f"{index + 1:064x}",
            candidate_run=f"candidate-{index}",
            candidate_version=f"2.{index}",
            candidate_hash=f"{index + 1001:064x}",
        )
        try:
            save_report(db, report, REPORT_SCHEMA)
        except StorageError as exc:
            assert "database-size limit or available disk space" in str(exc)
            break
    else:
        pytest.fail("history database did not enforce its configured size limit")

    assert db.stat().st_size <= limit
    assert len(list_reports(db)) == index


def test_history_rejects_an_existing_database_above_its_size_limit(tmp_path, monkeypatch):
    import driftglass.storage as storage

    db = tmp_path / "history.db"
    report = make_report()
    save_report(db, report, REPORT_SCHEMA)
    actual_size = db.stat().st_size
    assert actual_size > 1

    monkeypatch.setattr(storage, "MAX_HISTORY_DATABASE_BYTES", actual_size - 1)
    with pytest.raises(StorageError, match="database exceeds the local history database-size limit"):
        list_reports(db)
