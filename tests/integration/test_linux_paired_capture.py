"""Controlled live paired-capture smoke test for the Linux procfs observer.

The target is this test process itself, which holds a loopback-only listener.
No third-party or user-supplied application is started.
"""

import json
import os
import platform
import socket
from pathlib import Path

import pytest

from driftglass.capture import capture_run
from driftglass.comparison import compare_runs

ROOT = Path(__file__).resolve().parents[2]
OBSERVER = ROOT / "target" / "debug" / "driftglass-agent"
EVENT_SCHEMA = ROOT / "schemas" / "events" / "event-v1.schema.json"
MANIFEST_SCHEMA = ROOT / "schemas" / "manifests" / "run-manifest-v1.schema.json"
REPORT_SCHEMA = ROOT / "schemas" / "reports" / "report-v1.schema.json"


@pytest.mark.skipif(platform.system() != "Linux", reason="Linux procfs collector only")
@pytest.mark.skipif(not OBSERVER.is_file(), reason="build driftglass-agent to run this integration test")
def test_paired_captures_observe_a_stable_loopback_listener(tmp_path):
    experiment_id = "paired-loopback-smoke-v1"
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    local_port = listener.getsockname()[1]

    event_paths = {}
    manifest_paths = {}
    manifests = {}
    try:
        for role, version in (("baseline", "fixture-1"), ("candidate", "fixture-2")):
            event_path = tmp_path / f"{role}-events.jsonl"
            manifest_path = tmp_path / f"{role}-manifest.json"
            result = capture_run(
                observer_path=OBSERVER,
                pid=os.getpid(),
                duration_seconds=1,
                interval_ms=100,
                experiment_id=experiment_id,
                role=role,
                application_name="Driftglass loopback fixture",
                application_version=version,
                event_output_path=event_path,
                manifest_output_path=manifest_path,
                event_schema_path=EVENT_SCHEMA,
                manifest_schema_path=MANIFEST_SCHEMA,
                scenario="keep one loopback TCP listener open during bounded capture",
                package_id="org.driftglass.synthetic-loopback",
            )
            assert result.manifest["capture"]["status"] == "completed"
            events = [
                json.loads(line)
                for line in event_path.read_text(encoding="utf-8").splitlines()
            ]
            observed_listener = [
                event for event in events
                if event["category"] == "network"
                and event["action"] == "socket_present"
                and (event.get("attributes") or {}).get("local_port") == local_port
            ]
            # One long-lived listener should be present at the first sample.
            assert len(observed_listener) == 1
            assert observed_listener[0]["collection_quality"] == "complete"
            event_paths[role] = event_path
            manifest_paths[role] = manifest_path
            manifests[role] = result.manifest

        report = compare_runs(
            manifest_paths["baseline"],
            manifest_paths["candidate"],
            event_paths["baseline"],
            event_paths["candidate"],
            MANIFEST_SCHEMA,
            EVENT_SCHEMA,
        )
        assert report["findings"] == []
        assert report["summary"]["added_count"] == 0
        assert report["summary"]["removed_count"] == 0
        assert report["summary"]["changed_count"] == 0
        # The report may still be partial for known unavailable categories or
        # missing artifact hashes. This fixture is not a broad loss-rate test.
        assert report["coverage"]["baseline"]["recorded_event_counts"]["network"] >= 1
        assert report["coverage"]["candidate"]["recorded_event_counts"]["network"] >= 1
    finally:
        listener.close()
