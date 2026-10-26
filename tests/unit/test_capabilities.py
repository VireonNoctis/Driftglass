import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError

from driftglass.capabilities import capabilities_report
from driftglass.cli import main

ROOT = Path(__file__).resolve().parents[2]
CAPABILITIES_DOC = ROOT / "docs" / "platform-capabilities.md"
CAPABILITIES_SCHEMA = ROOT / "schemas" / "capabilities" / "capabilities-v1.schema.json"


def test_capability_summary_matches_the_implemented_observer_scope():
    report = capabilities_report()
    collector = report["collector"]

    assert report["schema_version"] == 1
    assert collector["implementation_status"] == "experimental"
    assert collector["platform"] == "Linux"
    assert collector["mechanism"] == "procfs polling"
    assert collector["implemented_categories"] == ["process", "network"]
    assert collector["unavailable_categories"] == ["file", "system", "persistence"]
    assert report["platforms"] == [
        {
            "platform": "Linux",
            "collector_status": "experimental",
            "validation_status": "synthetic tests and paired loopback capture on Ubuntu 22.04/24.04 CI images; broader coverage unverified",
        },
        {
            "platform": "Windows",
            "collector_status": "not_implemented",
            "validation_status": "no native collector or platform coverage tests",
        },
        {
            "platform": "macOS",
            "collector_status": "not_implemented",
            "validation_status": "no native collector or platform coverage tests",
        },
        {
            "platform": "Other",
            "collector_status": "not_implemented",
            "validation_status": "no native collector or platform coverage tests",
        },
    ]


def test_capabilities_cli_emits_machine_readable_status(capsys):
    result = main(["capabilities"])
    output = capsys.readouterr()

    assert result == 0
    payload = json.loads(output.out)
    assert payload == capabilities_report()
    assert output.err == ""


def test_capability_matrix_documents_unavailable_categories_and_limits():
    report = capabilities_report()
    document = CAPABILITIES_DOC.read_text(encoding="utf-8").lower()

    for category in report["collector"]["implemented_categories"]:
        assert f"`{category}`".lower() in document
    for category in report["collector"]["unavailable_categories"]:
        assert f"`{category}`".lower() in document
    assert "real-world coverage across linux builds is unverified" in document
    assert "windows" in document and "not implemented" in document
    assert "macos" in document and "not implemented" in document



def test_capability_report_conforms_to_versioned_schema_and_rejects_false_status():
    schema = json.loads(CAPABILITIES_SCHEMA.read_text(encoding="utf-8"))
    validator = Draft202012Validator(schema)
    report = capabilities_report()

    validator.validate(report)
    invalid = json.loads(json.dumps(report))
    invalid["platforms"][1]["collector_status"] = "supported"
    with pytest.raises(ValidationError):
        validator.validate(invalid)


def test_architecture_security_and_lifecycle_documents_are_linked_from_readme():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    documents = (
        "architecture.md",
        "threat-model.md",
        "privacy-model.md",
        "experiment-lifecycle.md",
    )
    for filename in documents:
        assert f"docs/{filename}" in readme
        assert (ROOT / "docs" / filename).is_file()
