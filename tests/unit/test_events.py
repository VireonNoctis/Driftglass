import json
from pathlib import Path

import pytest

from driftglass.events import EventValidationError, load_jsonl


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"


def event(**overrides):
    record = {
        "schema_version": 1,
        "event_id": "evt-001",
        "timestamp": "2026-10-25T10:15:30Z",
        "category": "network",
        "action": "connection_attempt",
        "source": {"collector": "synthetic-fixture/1", "event_ref": "source-record-7"},
        "collection_quality": "complete",
        "resource": {
            "kind": "network",
            "address": "192.0.2.10",
            "port": 443,
            "protocol": "tcp",
        },
    }
    record.update(overrides)
    return record


def write_records(path: Path, records: list[dict]) -> None:
    path.write_text("".join(json.dumps(record) + "\n" for record in records), encoding="utf-8")


def test_loads_valid_records_and_preserves_source_reference(tmp_path):
    path = tmp_path / "events.jsonl"
    write_records(path, [event(), event(event_id="evt-002", action="connection_established")])

    result = load_jsonl(path, SCHEMA)

    assert len(result) == 2
    assert result[0]["source"]["event_ref"] == "source-record-7"
    assert result[1]["event_id"] == "evt-002"


def test_rejects_malformed_json_with_line_number(tmp_path):
    path = tmp_path / "events.jsonl"
    path.write_text(json.dumps(event()) + "\n{not-json}\n", encoding="utf-8")

    with pytest.raises(EventValidationError, match=r":2: malformed JSON"):
        load_jsonl(path, SCHEMA)


def test_rejects_non_finite_numbers(tmp_path):
    path = tmp_path / "events.jsonl"
    write_records(path, [event(attributes={"metric": float("nan")})])

    with pytest.raises(EventValidationError, match="non-finite numeric constants are not valid JSON"):
        load_jsonl(path, SCHEMA)


def test_rejects_duplicate_json_object_keys(tmp_path):
    path = tmp_path / "events.jsonl"
    line = json.dumps(event()).replace(
        '"action": "connection_attempt"',
        '"action": "connection_attempt", "action": "overridden"',
    )
    path.write_text(line + "\n", encoding="utf-8")

    with pytest.raises(EventValidationError, match="duplicate JSON object keys are not valid"):
        load_jsonl(path, SCHEMA)


def test_rejects_blank_lines_instead_of_silently_skipping_them(tmp_path):
    path = tmp_path / "events.jsonl"
    path.write_text(json.dumps(event()) + "\n\n", encoding="utf-8")

    with pytest.raises(EventValidationError, match="blank lines are not valid"):
        load_jsonl(path, SCHEMA)


def test_rejects_missing_required_field_without_echoing_record_values(tmp_path):
    path = tmp_path / "events.jsonl"
    record = event()
    del record["source"]
    record["resource"]["address"] = "sensitive-host.example"
    write_records(path, [record])

    with pytest.raises(EventValidationError, match="missing required field 'source'") as exc:
        load_jsonl(path, SCHEMA)

    assert "sensitive-host.example" not in str(exc.value)


def test_rejects_invalid_timestamp(tmp_path):
    path = tmp_path / "events.jsonl"
    write_records(path, [event(timestamp="not-a-timestamp")])

    with pytest.raises(EventValidationError, match="expected format 'date-time'"):
        load_jsonl(path, SCHEMA)


def test_rejects_duplicate_event_ids(tmp_path):
    path = tmp_path / "events.jsonl"
    write_records(path, [event(), event(action="another_action")])

    with pytest.raises(EventValidationError, match=r"duplicate event_id \(first seen on line 1\)"):
        load_jsonl(path, SCHEMA)


def test_rejects_unknown_categories(tmp_path):
    path = tmp_path / "events.jsonl"
    write_records(path, [event(category="untracked")])

    with pytest.raises(EventValidationError, match=r"\$\.category: value is not in the allowed set"):
        load_jsonl(path, SCHEMA)


def test_rejects_empty_stream_to_avoid_confusing_it_with_no_activity(tmp_path):
    path = tmp_path / "events.jsonl"
    path.write_text("", encoding="utf-8")

    with pytest.raises(EventValidationError, match="empty telemetry is not evidence of no activity"):
        load_jsonl(path, SCHEMA)


def test_rejects_missing_event_file(tmp_path):
    with pytest.raises(EventValidationError, match="could not read event file"):
        load_jsonl(tmp_path / "missing.jsonl", SCHEMA)


def test_rejects_invalid_schema(tmp_path):
    schema = tmp_path / "invalid.schema.json"
    schema.write_text('{"type": "not-a-valid-json-schema-type"}', encoding="utf-8")
    events = tmp_path / "events.jsonl"
    write_records(events, [event()])

    with pytest.raises(EventValidationError, match="event schema.*is invalid"):
        load_jsonl(events, schema)
