# Implemented architecture

This document describes what exists in the repository today. It is not a target-state diagram.

## Current components

| Component | Location | Actual responsibility | Current boundary |
|---|---|---|---|
| Linux observer | `crates/driftglass-agent` | Samples `/proc` process stat and TCP/TCP6/UDP/UDP6 socket metadata for an already-running PID and observed descendants. | Experimental polling; no file-event collector or comprehensive kernel tracing. |
| C++ application core and CLI | `cpp/` | v1 event/manifest/report validation, deterministic comparison, repeat analysis, integrity, redaction, HTML export, bounded SQLite history, capabilities reporting, and capture coordination using an explicit observer executable path. | Python compatibility modules remain until schema-override and packaging parity are completed. C++ capture currently supports POSIX process spawning on Linux/macOS; Windows observer spawning is not implemented. |
| macOS process observer | `macos/` | Experimental Swift/libproc sampler for an already-running PID and visible descendants; emits process and coverage JSONL records. The C++ coordinator can invoke it when its executable path is explicitly supplied. | Process-only snapshots; no Endpoint Security, file, network, system or persistence event source; no signed System Extension. |
| Capture coordinator | `cpp/src/capture.cpp` | Invokes an explicitly selected observer with an argument vector, enforces time/output/boundary checks, validates the JSONL stream, and writes a v1 manifest even for incomplete observations when possible. | POSIX process spawning on Linux/macOS; does not launch/stop the observed application, create a VM, or verify a supplied artifact hash. Python capture remains compatibility code. |
| Validation and comparison | `events.py`, `manifests.py`, `comparison.py`, `profiles.py` | Validates records and manifests, compares behavior with deterministic profiles, preserves source evidence and marks declared gaps. | Input and rule semantics are versioned but not universally semantically equivalent. |
| Repeat analysis | `repeatability.py` | Aggregates previously created reports and records source report/finding references. | Does not schedule or capture repeated runs; recurrence is not proof of intent. |
| Reports and local history | `reporting.py`, `storage.py` | Exports JSON/standalone HTML and stores validated reports in local SQLite. | SQLite history is local and editable, not tamper-proof evidence storage. |
| CLI | `cli.py` | Validates inputs, captures a currently running PID, compares runs, exports reports, redacts selected export fields and browses/deletes local history with explicit confirmation. | No daemon, local HTTP API, remote backend, or automatic orchestration exists. |
| Report viewer | `web/` | Opens selected report files in the browser, displays findings/evidence and repeat-analysis data, timeline and partial process relationships. | Read-only; imported reports remain untrusted; no collector control or local API is present. |

## Data flow

1. The operator manually prepares an isolated environment and starts the target program.
2. The CLI launches the observer only, passing the existing PID and bounded sampling settings.
3. The observer emits JSONL with schema version, source reference and collection quality.
4. The coordinator validates the stream and emits a manifest, or writes a failure/incomplete manifest when possible and exits non-zero.
5. Comparison validates both runs, normalizes matching identity through a documented profile, emits evidence-linked findings and uncertainty, and hashes deterministic report content.
6. The operator reviews results, optionally exports JSON/HTML or writes local SQLite history.
7. The browser viewer only reads an explicitly selected file; it never starts an observed application.

## Not implemented

There is no local API, service daemon, VM manager, application launcher, event scheduler, Windows collector, complete macOS Endpoint Security collector, file-system event collector, packet payload capture, automatic remediation, or report upload. The Swift process sampler is experimental and process-only. The C++ coordinator can invoke it when explicitly selected, but that does not supply missing macOS event sources. Directory names or architectural diagrams must not be treated as evidence that these features exist.

See the [capability matrix](platform-capabilities.md), [experiment lifecycle](experiment-lifecycle.md), and [threat model](threat-model.md) for coverage and trust boundaries.
