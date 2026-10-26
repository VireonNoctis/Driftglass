import json
from pathlib import Path

import pytest

from driftglass.profiles import RuleProfileError, default_rule_profile, load_rule_profile

ROOT = Path(__file__).resolve().parents[2]
PROFILE_SCHEMA = ROOT / "schemas" / "rules" / "rule-profile-v1.schema.json"


def write_profile(path: Path, **updates):
    value = {
        "schema_version": 1,
        "profile_id": "network-review",
        "included_categories": ["network"],
        "ignored_attributes": ["socket_state"],
    }
    value.update(updates)
    path.write_text(json.dumps(value) + "\n", encoding="utf-8")


def test_default_profile_is_versioned_and_stable():
    profile = default_rule_profile()
    assert profile.profile_id == "default-v1"
    assert profile.included_categories == ("file", "network", "process", "system")
    assert profile.ignored_attributes == ()
    assert len(profile.sha256) == 64
    assert profile.report_metadata()["sha256"] == profile.sha256


def test_profile_hash_is_independent_of_list_order(tmp_path):
    first_path = tmp_path / "first.json"
    second_path = tmp_path / "second.json"
    write_profile(
        first_path,
        included_categories=["process", "network"],
        ignored_attributes=["socket_state", "volatile"],
    )
    write_profile(
        second_path,
        included_categories=["network", "process"],
        ignored_attributes=["volatile", "socket_state"],
    )
    first = load_rule_profile(first_path, PROFILE_SCHEMA)
    second = load_rule_profile(second_path, PROFILE_SCHEMA)

    assert first.profile_id == second.profile_id
    assert first.sha256 == second.sha256
    assert first.included_categories == ("network", "process")
    assert first.ignored_attributes == ("socket_state", "volatile")


@pytest.mark.parametrize("updates", [
    {"included_categories": []},
    {"included_categories": ["network", "network"]},
    {"included_categories": ["persistence"]},
    {"ignored_attributes": ["not an attribute key"]},
    {"schema_version": 2},
])
def test_rejects_invalid_profiles(tmp_path, updates):
    path = tmp_path / "bad-profile.json"
    write_profile(path, **updates)
    with pytest.raises(RuleProfileError):
        load_rule_profile(path, PROFILE_SCHEMA)


def test_rejects_duplicate_json_object_keys(tmp_path):
    path = tmp_path / "duplicate-key.json"
    path.write_text(
        '{"schema_version":1,"profile_id":"first","profile_id":"second",'
        '"included_categories":["network"],"ignored_attributes":[]}\n',
        encoding="utf-8",
    )
    with pytest.raises(RuleProfileError, match="duplicate JSON object keys"):
        load_rule_profile(path, PROFILE_SCHEMA)


def test_rejects_profile_over_file_size_limit(tmp_path, monkeypatch):
    path = tmp_path / "large-profile.json"
    write_profile(path)
    monkeypatch.setattr("driftglass.profiles.MAX_RULE_PROFILE_FILE_BYTES", 8)

    with pytest.raises(RuleProfileError, match="exceeds the 8-byte file limit"):
        load_rule_profile(path, PROFILE_SCHEMA)
