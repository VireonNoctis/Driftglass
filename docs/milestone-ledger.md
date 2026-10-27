# Driftglass 177-milestone execution ledger

This is the first expansion from the prior 60-item ledger to 160 numbered milestones; the original README roadmap was phase-based rather than a pre-existing 160-item plan. **Done** means the specific implementation, regression check, or documented decision in that row exists and its evidence is recorded. **Done (deferred)** closes a planning decision only; it does not mean the underlying capability exists. The original milestones and the new 61–160 slice both include validation items for features already present in the repository. They are not a claim that all 160 capabilities were newly implemented in this session.

This ledger includes three later implementation checks—strict RFC 3339 browser timestamp acceptance, the repeat-analysis source-count schema ceiling, and an aggregate byte budget for repeat input—plus an explicit deferral decision for local API security. Implementation checks are based on passing CI for the relevant code and tests. Many earlier rows make existing test coverage more granular; they must not be read as newly shipped features.

**Current tally: 177/177 milestones resolved: 168 implementation/validation milestones and 9 explicit deferral decisions; 0 remaining.** A deferral resolves the planning decision, not the underlying capability or security validation; deferred work remains unimplemented or unverified where applicable.

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
| 52 | Run observer tests on distinct Ubuntu runner images and record actual OS/kernel | Done | Rust workspace tests and paired-capture test passed on Ubuntu 22.04.5 LTS/kernel `6.8.0-1064-azure` and Ubuntu 24.04.5 LTS/kernel `6.17.0-1022-azure`; other builds remain unverified |
| 53 | Test PID-selected network-table path and document cross-namespace descendant limitation | Done | Synthetic Rust fixture verifies per-PID `/proc/<pid>/net` table selection; actual cross-namespace visibility remains unsupported/unverified |
| 54 | Make and document a formal file-activity collector deferral decision | Done (deferred) | `docs/decisions/file-activity-collector-deferral.md`; file activity remains unavailable and no collector is claimed |
| 55 | Make and document a formal system/persistence observation deferral decision | Done (deferred) | `docs/decisions/system-persistence-deferral.md`; system/persistence remains unavailable and no collector is claimed |
| 56 | Formally defer the Windows observer pending a supported event-source and test plan | Done (deferred) | `docs/decisions/windows-observer-deferral.md`; Windows remains `not_implemented` |
| 57 | Formally defer the native macOS observer pending entitlements, distribution and platform test readiness | Done (deferred) | Decision record cites Apple Endpoint Security/System Extension requirements and defines re-entry criteria; no macOS support is claimed |
| 58 | Add a reproducible paired-capture harness without launching unknown targets | Done | `tests/integration/test_linux_paired_capture.py` captures the already-running synthetic test process twice, checks expected listener evidence and compares reports |
| 59 | Add a detached SHA-256 export integrity manifest and verifier with honest trust semantics | Done | `report attest`/`report verify`, versioned schema and tamper tests; explicitly not a signature or author authentication |
| 60 | Bound SQLite history by report count and aggregate report JSON, and document retention/recovery limits | Done | Save rejects over 1,000 reports or 256 MiB aggregate report JSON transactionally; documentation explains SQLite file overhead and explicit deletion |

## Milestones 61–160

The new slice contains 96 implementation/validation acceptance items and four deferral decisions. Existing tests count as evidence only for the narrow behavior named in each row; CI success is not a claim of comprehensive collector coverage.

| # | Milestone | Status | Evidence / acceptance |
|---:|---|---|---|
| 61 | Reject identical baseline and candidate run IDs | Done | Browser regression test in web/src/lib/report-validation.test.mjs |
| 62 | Require baseline evidence to reference the baseline run | Done | Evidence-side run relationship validation and regression test |
| 63 | Require candidate evidence to reference the candidate run | Done | Evidence-side run relationship validation and regression test |
| 64 | Require evidence category to match its finding category | Done | Browser report validator and contradictory-category regression test |
| 65 | Reject duplicate finding IDs within a report | Done | Browser report validator and duplicate-ID regression test |
| 66 | Reject uncertainty references to unknown runs | Done | Browser report validator and unknown-run regression test |
| 67 | Reject duplicate names in the coverage source list | Done | Browser coverage validator and regression test |
| 68 | Require failed or inactive sources to declare coverage gaps | Done | Browser coverage validator mirrors manifest invariant; regression test |
| 69 | Preserve compatibility with report-v1 files lacking optional coverage | Done | Legacy-report regression test in web/src/lib/report-validation.test.mjs |
| 70 | Validate optional process identity and start-time metadata | Done | Browser report validation tests for valid and malformed process identities |
| 71 | Validate comparison-profile metadata and digest shape | Done | Browser report validation tests for valid, malformed and absent profile metadata |
| 72 | Check report summary counts against its finding list | Done | Browser report-validation regression test |
| 73 | Require uncertainty count to match uncertainty records | Done | Browser report-validation regression test |
| 74 | Require report complete/partial status to agree with uncertainty declaration | Done | Browser report-validation regression test |
| 75 | Reject unsupported report schema versions before viewer rendering | Done | Browser report-validation regression test |
| 76 | Require RFC 3339 evidence timestamps with a timezone and valid calendar date | Done | Browser validator rejects date-only, local-time, impossible-date and invalid-offset values; accepts UTC and explicit offsets |
| 77 | Require repeat analysis to contain at least two reports | Done | tests/unit/test_repeatability.py |
| 78 | Reject duplicate report IDs in repeat-analysis inputs | Done | Repeat-analysis regression tests |
| 79 | Reject repeat reports from incompatible version cohorts | Done | Repeat-analysis version-cohort regression test |
| 80 | Include baseline and candidate artifact hashes in cohort matching | Done | Repeat-analysis cohort construction and compatibility regression tests |
| 81 | Include comparison-profile fingerprint in cohort matching | Done | Repeat-analysis profile cohort logic and profile tests |
| 82 | Make repeat-analysis independent of input ordering | Done | Repeat-analysis determinism regression test |
| 83 | Label a pattern recurring only when present in every source report | Done | Repeat-analysis recurrence regression test |
| 84 | Preserve incomplete or partial report status in repeat analysis | Done | Repeat-analysis partial-report accounting tests |
| 85 | Link aggregate pattern occurrences to original report and finding IDs | Done | Repeat-analysis source-reference tests |
| 86 | Reject corrupted repeat-analysis content IDs | Done | Repeat-analysis export and validation tests |
| 87 | Redact paths using stable replacement tokens in exported copies | Done | tests/unit/test_redaction.py |
| 88 | Redact network hosts and addresses when explicitly requested | Done | Redaction regression tests |
| 89 | Redact process names when explicitly requested | Done | Redaction regression tests |
| 90 | Reuse the same token for repeated sensitive values | Done | Redaction consistency regression test |
| 91 | Keep unselected redaction categories unchanged | Done | Selective-redaction regression test |
| 92 | Ensure redaction does not mutate the source report | Done | Source immutability regression test |
| 93 | Reject an empty redaction selection | Done | Redaction validation regression test |
| 94 | Recalculate redacted report and finding IDs | Done | Redaction implementation plus schema-valid export test |
| 95 | Keep local history unredacted when exporting a redacted copy | Done | CLI redacted-export integration test |
| 96 | Require file export when redaction is requested | Done | CLI rejects redaction without supported file output |
| 97 | Validate JSON export before writing the report | Done | tests/unit/test_reporting.py |
| 98 | Refuse JSON export overwrite by default | Done | JSON output path regression test |
| 99 | Allow overwrite only through the explicit flag | Done | Explicit-overwrite regression test |
| 100 | Escape untrusted event text in standalone HTML | Done | HTML injection regression test |
| 101 | Keep standalone HTML independent of remote assets | Done | HTML export test checks for remote references |
| 102 | Escape collection coverage notes in HTML output | Done | HTML coverage rendering and escaped-note regression test |
| 103 | Render a useful coverage note for older reports without a coverage snapshot | Done | Legacy HTML report regression test |
| 104 | Validate report content-derived ID before rendering/export | Done | Report-ID validation regression test |
| 105 | Refuse to silently create a missing report-output directory | Done | Missing-output-directory regression test |
| 106 | Remove a partially written new report after a write failure | Done | Synthetic fsync-failure regression test |
| 107 | Persist reports and searchable finding indexes in SQLite | Done | tests/unit/test_storage.py |
| 108 | Make saving an identical report idempotent | Done | Storage idempotency regression test |
| 109 | Reject run-ID collisions with conflicting metadata atomically | Done | Storage transaction regression test |
| 110 | Reject invalid report IDs before database creation | Done | Storage report-ID validation regression test |
| 111 | Reject unknown SQLite schema versions | Done | Versioned database compatibility regression test |
| 112 | Reject incomplete versioned SQLite schemas | Done | Required-table/column validation regression test |
| 113 | Reject unversioned databases that already contain tables | Done | Storage non-destructive-open regression test |
| 114 | Reject duplicate JSON keys in stored report payloads | Done | Stored-payload parsing regression test |
| 115 | Reject non-finite or overflowed numeric values in stored payloads | Done | Stored-payload numeric validation regression test |
| 116 | Reject report JSON stored under the wrong history key | Done | Stored report ID consistency regression test |
| 117 | Refuse to create a missing SQLite parent directory | Done | Storage path regression test |
| 118 | Delete report indexes and unreferenced run rows transactionally | Done | History deletion transaction tests |
| 119 | Preserve run metadata still referenced by another report | Done | Shared-run deletion regression test |
| 120 | Enforce logical report-count and aggregate JSON-payload history limits | Done | Storage history-budget regression test; not a physical DB-file cap |
| 121 | Invoke only the configured observer with an argument vector | Done | tests/unit/test_capture.py verifies the wrapper does not launch the target |
| 122 | Reject invalid PID values before launching the observer | Done | Capture PID validation regression test |
| 123 | Reject output-path collisions before starting capture | Done | Capture path-collision regression test |
| 124 | Validate successful capture event stream and generated manifest | Done | Capture happy-path regression test |
| 125 | Keep declared collector gaps visible on otherwise completed capture | Done | Capture coverage regression test |
| 126 | Return failure status for a non-zero observer exit | Done | Failed-observer regression test |
| 127 | Treat missing capture-finished boundary as incomplete | Done | Missing-boundary regression test |
| 128 | Let explicit incomplete events override a zero process exit | Done | Capture-incomplete event regression test |
| 129 | Mark timeouts with partial telemetry as incomplete | Done | Capture timeout regression test |
| 130 | Reject malformed observer JSONL and preserve failure state | Done | Capture malformed-stream regression test |
| 131 | Include a useful line location for malformed event JSON | Done | tests/unit/test_events.py malformed-line diagnostic test |
| 132 | Reject duplicate JSON object keys at the event boundary | Done | Event parsing duplicate-key regression test |
| 133 | Reject non-standard and non-finite event numeric values | Done | Event parsing numeric validation tests |
| 134 | Reject blank event-stream lines rather than skipping records | Done | Event stream blank-line regression test |
| 135 | Reject duplicate event IDs within a stream | Done | Event ID uniqueness regression test |
| 136 | Reject empty event streams as insufficient evidence | Done | Empty-stream rejection regression test |
| 137 | Bound total event-stream bytes before unbounded ingestion | Done | 64 MiB stream limit test |
| 138 | Bound per-record JSONL line size | Done | 1 MiB line limit test |
| 139 | Bound event record count | Done | 250,000-record cap test |
| 140 | Enforce manifest time and terminal-status invariants | Done | tests/unit/test_manifests.py timestamp/status regression tests |
| 141 | Enforce manifest source-name uniqueness and explicit unavailable categories | Done | Manifest invariant regression tests |
| 142 | Normalize rule-profile list order before fingerprinting | Done | tests/unit/test_profiles.py profile hash order test |
| 143 | Reject malformed, duplicated, or unsupported rule-profile fields | Done | Rule-profile schema and invalid-profile regression tests |
| 144 | Enforce the rule-profile file-size limit | Done | Rule-profile input size regression test |
| 145 | Ignore volatile local TCP source ports while preserving destination differences | Done | tests/unit/test_comparison.py network identity regression tests |
| 146 | Keep equivalent comparisons stable despite event-order noise | Done | Comparison order-independence regression test |
| 147 | Detect a genuine destination change amid unrelated noise | Done | Comparison destination-change regression test |
| 148 | Link changed observations to baseline and candidate evidence | Done | Comparison evidence-link regression test |
| 149 | Classify action differences as added/removed observations | Done | Comparison action-change regression test |
| 150 | Match duplicate observations as a multiset | Done | Duplicate-observation comparison regression test |
| 151 | Exclude coverage-control records from behaviour findings | Done | Coverage-event comparison regression test |
| 152 | Make declared gaps produce partial status and uncertainty | Done | Comparison coverage-gap regression test |
| 153 | Turn environment mismatch into context/uncertainty rather than a verdict | Done | Comparison context-difference regression test |
| 154 | Preserve deterministic report identity across normalized source ordering | Done | Comparison determinism and coverage-order tests |
| 155 | Render findings search/filter in the read-only viewer | Done | Svelte viewer implementation and web tests |
| 156 | Render a chronological evidence timeline and conservative process relationships | Done | Evidence timeline and process-tree modules and tests |
| 157 | Formally defer comprehensive Linux kernel event tracing | Done (deferred) | docs/decisions/linux-event-tracing-deferral.md; polling remains experimental |
| 158 | Formally defer VM/snapshot lifecycle and target-launch automation | Done (deferred) | docs/decisions/vm-lifecycle-management-deferral.md; operator-managed launch remains required |
| 159 | Formally defer signed report attestations until a key-trust model exists | Done (deferred) | docs/decisions/signed-report-attestations-deferral.md; SHA-256 sidecar is unsigned |
| 160 | Formally defer namespace-aware socket attribution pending controlled validation | Done (deferred) | docs/decisions/network-namespace-attribution-deferral.md; no broader visibility claim |

| 161 | Accept RFC 3339 lowercase `t`/`z` timestamp designators in browser evidence | Done | Browser validator and lower-case/mixed-case timestamp regression tests |
| 162 | Enforce the 500-source-report repeat-analysis ceiling in JSON Schema as well as code | Done | `schemas/reports/repeat-analysis-v1.schema.json`; `test_repeat_schema_caps_source_reports_at_500`; Python/browser limits already exist |
| 163 | Bound the aggregate bytes of repeat-analysis source reports | Done | 64 MiB per-file limit plus 256 MiB aggregate loader cap; CLI regression test and documented limits |

| 164 | Formally defer local API access-control validation until an API exists | Done (deferred) | `docs/decisions/local-api-security-deferral.md`; architecture and dashboard documents confirm the current product is file-based and has no local HTTP API |

| 165 | Require incomplete/failed capture status to remain visible as run-linked report uncertainty | Done | Report schema, Python report validator, browser validator, and cross-boundary regression tests; CI run 161 passed |

| 166 | Require report coverage snapshots to match their run-linked uncertainty codes | Done | Python authoritative validator, browser validator, synthetic matching/missing/wrong-run cases, report-export documentation; CI run 167 passed |

| 167 | Require coverage snapshot record counts to match report summary event counts | Done | Python and browser cross-field validation, synthetic mismatch tests, and report-export documentation; CI run 172 passed |

| 168 | Require behaviour-event counts to match coverage counts for included profile categories | Done | Python and browser cross-field validation, synthetic mismatch regression, and documentation; CI run 176 passed |

| 169 | Verify repeat-analysis content-derived IDs in the browser before display | Done | Python-compatible canonical JSON SHA-256 verifier, Unicode golden test, tamper test, and visible failure on unavailable crypto; CI run 180 passed |

| 170 | Reject duplicate JSON object keys before browser report parsing | Done | Shared browser intake scanner; duplicate, escaped-equivalence and nested-scope tests wired into `npm test`; dashboard and validation-matrix docs; CI run 185 passed all four jobs |

| 171 | Reject JSON numbers that overflow to non-finite browser values | Done | Shared intake check after native parsing; nested `1e400` and finite-number tests wired into `npm test`; dashboard and validation-matrix docs; CI run 190 passed all four jobs |

| 172 | Reject unsafe integer-form JSON numbers before browser parsing | Done | Source-token scan compares decimal digits before `JSON.parse`; tests cover safe boundaries, positive/negative overflow, escaped strings, and decimal/exponent exclusions; dashboard and validation-matrix docs; CI run 193 passed all four jobs |

| 173 | Show the browser's report-ID validation boundary when displaying report-v1 | Done | Visible neutral disclosure beside loaded report metadata, local CLI validation command, dashboard and validation-matrix docs; CI run 196 passed all four jobs |

| 174 | Bound JSON nesting depth in browser imports | Done | Shared pre-parse scan rejects nesting beyond 256 levels; tests cover exact boundary and escaped string contents; dashboard and validation-matrix docs; CI run 199 passed all four jobs |

| 175 | Run the browser nesting guard before duplicate-key scanning | Done | Shared pre-parse validation reports an over-depth error before key-set allocation; regression uses a deeply nested document containing duplicate members; dashboard and validation-matrix docs; CI run 202 passed all four jobs |

| 176 | Bound resource use when checking oversized integer JSON tokens | Done | Replace unbounded `BigInt` conversion with digit-length and lexicographic comparison; add a 100,000-digit synthetic token regression; dashboard and validation-matrix docs; CI run 205 passed all four jobs |

| 177 | Bound aggregate JSON token count before browser parsing | Done | 500,000-token preflight runs before duplicate-key scanning; tests cover exact limit and an over-limit document containing duplicate keys; dashboard and validation-matrix docs; CI run 208 passed all four jobs |

## CI and evidence

The latest source change should be considered complete only when the corresponding GitHub Actions run finishes successfully. CI runs Python unit tests, wheel/schema packaging and smoke checks, locked Rust workspace tests, and web tests/type checks/build. These checks do not replace controlled real-world observer measurements required by milestones 51–53.

Do not change any **Remaining** item to **Done** solely because a type, directory, schema, or placeholder was added. Require implementation and the acceptance evidence described above.
