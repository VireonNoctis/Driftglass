from pathlib import Path
import runpy


ROOT = Path(__file__).resolve().parents[2]
workflow_action_pin_issues = runpy.run_path(
    str(ROOT / ".github" / "scripts" / "repository_health.py")
)["workflow_action_pin_issues"]


def test_external_actions_require_full_commit_shas():
    workflow = """steps:
  - uses: actions/checkout@v4
  - uses: actions/setup-python@a26af69be951a213d495a4c3e4e4022e16d87065
  - uses: owner/repo/path@0123456789abcdef0123456789abcdef01234567
"""
    assert workflow_action_pin_issues(workflow) == [
        (2, "actions/checkout@v4"),
    ]


def test_local_actions_and_digest_pinned_containers_are_allowed():
    workflow = """steps:
  - uses: ./.github/actions/local-check
  - uses: docker://example/tool@sha256:0123456789abcdef
  - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4
"""
    assert workflow_action_pin_issues(workflow) == []


def test_committed_workflows_have_no_mutable_external_action_refs():
    workflow_dir = ROOT / ".github" / "workflows"
    for path in sorted(workflow_dir.glob("*.yml")):
        assert workflow_action_pin_issues(path.read_text(encoding="utf-8")) == []
