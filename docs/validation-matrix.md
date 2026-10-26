# Driftglass Validation Matrix

Validation must establish what the tool captures, what it misses, and whether each finding follows from the recorded evidence.

## Test matrix

| Layer | Required cases | Pass condition |
|---|---|---|
| Schema | Missing fields, unknown version, invalid enum, oversized payload | Clear and deterministic rejection |
| Observer | Missing permissions, short-lived process, inaccessible PID, polling error | Gaps and errors are visible |
| Normalization | Equivalent platform events, unknown fields, repeated events | Stable output without invented values |
| Comparison | Added, removed, modified, reordered and duplicate events; volatile local TCP source ports; ambiguous events | Correct finding type and evidence link; equivalent synthetic runs have no findings for covered noise |
| Reporting | Malformed records, hostile HTML-like text, missing event IDs | Safe rendering and explicit errors |
| Storage | Insert, reopen, delete, corrupt DB, interrupted write | Documented local lifecycle |
| Privacy | Redacted paths and identifiers | Export matches configured policy and records redaction |
| Integration | Fixed fixtures and rule versions | Reproducible output |
| Platform | Each claimed OS/build | Support claims match recorded tests |

## Synthetic fixtures

Maintain fixtures for no meaningful change; new connection metadata; a file operation change; process start/exit ordering; a denied observer; environment mismatch; invalid JSONL between valid records; an unknown future event type; HTML-like hostile strings; and an export that must redact sensitive fields.

Fixtures should contain no real personal data. Include equivalent network observations with changed local ephemeral ports and a paired case where the remote destination really changes. Changes to comparison rules must explain any changed expected result. Synthetic fixture results are not a production false-positive-rate estimate.

## Acceptance gates

1. A known material change appears, while documented non-change cases do not produce unexplained findings.
2. Every finding references valid source event IDs or states that evidence is incomplete.
3. Invalid inputs and collection failures are not converted into empty successful reports.
4. Fixed fixtures and rule versions produce deterministic results.
5. No remote transmission occurs by default.
6. Integration tests use disposable environments for unknown applications.
7. Published support matches the actual platform matrix.

## Metrics

Track event-loss rate under controlled workloads, malformed-record rate, false-positive and false-negative counts on labelled fixtures, repeated-run variance, processing time, peak memory, and the percentage of findings with complete evidence links. Always publish the workload and environment with the metric.

## Manual record template

Record date; OS and kernel/build; observer version; target-fixture hash; setup; privileges; expected and actual observations; missing capabilities; manifest path; and reviewer notes.

A passing synthetic fixture is not proof of production reliability. Keep laboratory results, real-environment results, and assumptions distinct.
