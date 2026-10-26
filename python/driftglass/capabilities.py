"""Machine-readable summary of the collector scope implemented in this release."""

from __future__ import annotations

from .capture import CATEGORIES, OBSERVER_CATEGORIES


def capabilities_report() -> dict[str, object]:
    """Return implemented collector scope, without implying comprehensive coverage."""
    implemented = set(OBSERVER_CATEGORIES)
    return {
        "schema_version": 1,
        "collector": {
            "name": "driftglass-agent",
            "implementation_status": "experimental",
            "platform": "Linux",
            "mechanism": "procfs polling",
            "implemented_categories": list(OBSERVER_CATEGORIES),
            "category_scope": {
                "process": (
                    "Sampled process stat metadata for the selected PID and observed descendants; "
                    "short-lived processes may be missed."
                ),
                "network": (
                    "Sampled TCP/TCP6 socket metadata associated with the observed process scope; "
                    "ownership and network-namespace visibility can be incomplete."
                ),
            },
            "unavailable_categories": [
                category for category in CATEGORIES if category not in implemented
            ],
            "not_collected": [
                "network packet payloads",
                "DNS queries",
                "UDP activity",
                "file contents",
                "file access and modification events",
                "system configuration and persistence changes",
                "command-line arguments",
            ],
            "known_limitations": [
                "Polling can miss activity that starts and stops between samples.",
                "Observation timestamps identify sampling time, not exact action time.",
                "TCP metadata is limited to the sampled network namespace and observable socket ownership.",
                "Permissions, procfs visibility, and process races can leave coverage gaps.",
                "A completed capture is not proof of comprehensive observation.",
            ],
        },
        "platforms": [
            {
                "platform": "Linux",
                "collector_status": "experimental",
                "validation_status": "synthetic tests and one live loopback smoke test; broader coverage unverified",
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
        ],
    }
