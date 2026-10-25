import json

import pytest

from driftglass._schema import load_validator


def test_loads_valid_schema(tmp_path):
    path = tmp_path / "schema.json"
    path.write_text(json.dumps({"$schema": "https://json-schema.org/draft/2020-12/schema", "type": "object"}), encoding="utf-8")

    validator = load_validator(path, "test schema")

    assert validator.is_valid({})


def test_rejects_duplicate_keys_in_schema(tmp_path):
    path = tmp_path / "schema.json"
    path.write_text('{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","type":"array"}', encoding="utf-8")

    with pytest.raises(ValueError, match="duplicate JSON object keys are not valid"):
        load_validator(path, "test schema")


def test_rejects_non_finite_constants_in_schema(tmp_path):
    path = tmp_path / "schema.json"
    path.write_text('{"type":"object","minimum":NaN}', encoding="utf-8")

    with pytest.raises(ValueError, match="non-finite numeric constants are not valid JSON"):
        load_validator(path, "test schema")
