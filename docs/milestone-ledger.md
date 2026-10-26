# Driftglass 60-milestone execution ledger

This is the first numbered 60-item ledger for the project; the prior README roadmap was phase-based and did not contain a pre-existing 60-item plan. The status column is intentionally honest: a milestone is marked **Done** only where code/docs exist and the relevant automated check ran, or where it is a documented capability verified from the implementation. **Remaining** means it has not been completed or validated yet.

**Current tally: 56 done, 4 remaining.** This does not mean 49 milestones were completed in the latest session. Items 1–36 are existing capabilities in the repository; items 37–49 record the comparison fix and the work completed across the latest commits. The remaining platform and real-workload milestones require implementation or experiments, not more checkboxes.

| # | Milestone | Status | Evidence / acceptance |
|---:|---|---|---|
| 1 | Versioned event-record v1 schema | Done | `schemas/events/event-v1.schema.json`; schema tests |
| 2 | Versioned run-manifest v1 schema | Done | `schemas/manifests/run-manifest-v1.schema.json` |
| 3 | Versioned comparison-report v1 schema | Done | `schemas/reports/report-v1.schema.json` |
| 4 | Versioned repeat-analysis v1 schema | Done | `schemas/reports/repeat-analysis-v1.schema.json` |
| 5 | Strict versioned comparison rule-profile schema | Done | `schemas/rules/rule-profile-v1.schema.json` |
| 6 | Validate event records against the JSON Schema | Done | `python/driftglass/events.py`; unit tests |
| 7 | Reject malformed JSON, duplicate JSON keys/IDs, and non-finite values | Done | `tests/unit/test_events.py` |
| 8 | Reject an empty event stream instead of calling it quiet activity | Done | `tests/unit/test_events.py` |
| 9 | Enforce run-manifest cross-field invariants and unique source names | Done | `python/driftglass/manifests.py`; manifest tests |
| 10 | Deterministic baseline/candidate comparison | Done | `python/driftglass/comparison.py` |
| 11 | Classify added, removed, and changed observations | Done | Comparison regression tests |
| 12 | Link findings to source event IDs and run evidence | Done | Report v1 evidence fields and tests |
| 13 | Surface collection gaps and uncertain input coverage | Done | Coverage summaries and uncertainty tests |
| 14 | Report environment mismatches | Done | Comparison context-difference tests |
| 15 | Support deterministic custom rule profiles with fingerprints | Done | `python/driftglass/profiles.py`; profile tests |
| 16 | Aggregate findings across compatible repeat reports | Done | `python/driftglass/repeatability.py`; repeat tests |
| 17 | Validate repeat-analysis source references and derived content ID | Done | Repeat-analysis validator tests |
| 18 | Export structured JSON reports | Done | `python/driftglass/reporting.py` |
| 19 | Export standalone HTML with escaped report text | Done | Reporting tests; no remote assets required |
| 20 | Protect report output paths and use safe writes | Done | Reporting and CLI path-collision tests |
| 21 | Store local reports and finding/run indexes in SQLite | Done | `python/driftglass/storage.py`; storage tests |
| 22 | Require exact confirmation for local history deletion | Done | `history delete` CLI and cleanup tests |
| 23 | Provide explicit, opt-in export redaction | Done | Redaction unit/CLI tests; source history remains unredacted |
| 24 | Validate selected report JSON before browser rendering | Done | `web/src/lib/report-validation.js` and tests |
| 25 | Search/filter findings in the read-only viewer | Done | Svelte viewer and web tests |
| 26 | Inspect evidence attached to findings | Done | Svelte viewer evidence UI |
| 27 | Show a chronological timeline of finding-linked evidence | Done | `web/src/lib/evidence-timeline.js` and tests |
| 28 | Show a conservative partial process relationship view | Done | `web/src/lib/process-tree.js` and tests |
| 29 | View repeat-analysis v1 data in the dashboard | Done | Repeat-analysis viewer validation and web tests |
| 30 | Sample process stat metadata for a supplied Linux PID tree | Done | Rust procfs observer and synthetic tests |
| 31 | Sample TCP/TCP6 socket metadata | Done | Rust parser tests; explicitly TCP-only |
| 32 | Emit collection gaps for procfs/read/ownership errors | Done | Rust observer coverage handling and tests |
| 33 | Detect reuse of the requested target PID | Done | Rust target identity tests |
| 34 | Refuse observer output overwrite and use owner-only file mode on Unix | Done | Rust observer tests |
| 35 | Coordinate a bounded capture of an already-running PID | Done | `python/driftglass/capture.py`; capture tests |
| 36 | Return non-zero and preserve incomplete status on explicit observer failure | Done | Capture coordinator tests; CI history |
| 37 | Ignore volatile local TCP source ports for behavior matching while retaining evidence | Done | `4e69839`; comparison regressions |
| 38 | Publish implemented/unavailable collector capability matrix | Done | `docs/platform-capabilities.md` |
| 39 | Expose machine-readable collector scope with `driftglass capabilities` | Done | CLI and capability tests |
| 40 | Version and package the capability report schema; validate installed CLI output in CI | Done | `schemas/capabilities/capabilities-v1.schema.json`; wheel smoke check |
| 41 | Document implemented architecture separately from target architecture | Done | `docs/architecture.md` |
| 42 | Document trust boundaries and threat assumptions | Done | `docs/threat-model.md` |
| 43 | Document collected data, storage, redaction and privacy limits | Done | `docs/privacy-model.md` |
| 44 | Define manual experiment lifecycle and operator-managed isolation | Done | `docs/experiment-lifecycle.md`; Phase 0 roadmap |
| 45 | Bound JSONL event input by total bytes, line size and record count | Done | 64 MiB stream, 1 MiB line, 250,000 records; limit tests |
| 46 | Bound imported comparison-report JSON files | Done | 64 MiB limit in `python/driftglass/repeatability.py`; test |
| 47 | Bound run-manifest and rule-profile file inputs | Done | 4 MiB manifest, 1 MiB profile; limit tests |
| 48 | Add `driftglass report validate` for schema, evidence relationships and content hash | Done | CLI validation success/tamper tests |
| 49 | Mark socket events partial when PID ownership is ambiguous | Done | `5f4c8ae`; Rust quality regression test |
| 50 | Smoke-test live Linux procfs process and loopback TCP listener visibility | Done | Rust test uses a loopback-only listener and checks process metadata, TCP table entry and socket-owner attribution; does not measure event loss |
| 51 | Record expected versus observed listener events in a controlled Linux workload | Done | Integration test expects one known loopback listener event and observes exactly one in each capture; this is not a general event-loss-rate estimate |
| 52 | Validate collector behavior across distinct Linux kernel/distribution builds | Remaining | Publish only after experiments record the actual kernel/build and coverage |
| 53 | Test PID-selected network-table path and document cross-namespace descendant limitation | Done | Synthetic Rust fixture verifies per-PID `/proc/<pid>/net` table selection; actual cross-namespace visibility remains unsupported/unverified |
| 54 | Add file-system activity collection with explicit coverage and tests | Remaining | No file-event collector currently exists |
| 55 | Add system/persistence observation only after defined supported sources and tests | Remaining | Currently declared unavailable |
| 56 | Implement and validate a native Windows observer | Remaining | No Windows collector exists |
| 57 | Formally defer the native macOS observer pending entitlements, distribution and platform test readiness | Done | Decision record cites Apple Endpoint Security/System Extension requirements and defines re-entry criteria; no macOS support is claimed |
| 58 | Add a reproducible paired-capture harness without launching unknown targets | Done | `tests/integration/test_linux_paired_capture.py` captures the already-running synthetic test process twice, checks expected listener evidence and compares reports |
| 59 | Add a detached SHA-256 export integrity manifest and verifier with honest trust semantics | Done | `report attest`/`report verify`, versioned schema and tamper tests; explicitly not a signature or author authentication |
| 60 | Bound SQLite history by report count and aggregate report JSON, and document retention/recovery limits | Done | Save rejects over 1,000 reports or 256 MiB aggregate report JSON transactionally; documentation explains SQLite file overhead and explicit deletion |

## CI and evidence

The latest source change should be considered complete only when the corresponding GitHub Actions run finishes successfully. CI runs Python unit tests, wheel/schema packaging and smoke checks, locked Rust workspace tests, and web tests/type checks/build. These checks do not replace controlled real-world observer measurements required by milestones 51–53.

Do not change any **Remaining** item to **Done** solely because a type, directory, schema, or placeholder was added. Require implementation and the acceptance evidence described above.
