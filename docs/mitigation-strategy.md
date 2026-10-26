# Driftglass Mitigation Strategy

Driftglass reduces avoidable blind spots and makes uncertainty visible. It cannot eliminate every risk.

## Risk-to-control map

| Risk | Controls | Validation evidence | Residual risk |
|---|---|---|---|
| Gaps look like inactivity | Explicit coverage states, observer-health events, capability list, collection-error counters | Tests inject denied permissions, unavailable sources, and sampling errors | Unobservable kernel or firmware behaviour remains out of scope |
| Baseline differs from candidate environment | Run manifests, setup fingerprints, mismatch warnings | Change one setup property and verify the warning | Hidden external state may remain unknown |
| Noisy events overwhelm review | Deterministic normalization, grouping, configurable rules, raw evidence retention | Fixtures include expected noise and meaningful differences | A rule may still hide a meaningful change |
| Conclusions overstate intent | Evidence-linked findings and separate fact/inference fields | Reject findings with missing evidence references or rationale | Human interpretation can remain wrong |
| Malformed event data | Versioned schemas, strict enums, size limits, safe parser errors | Fuzz and malformed-input tests | Parser dependencies may have vulnerabilities |
| Reports leak sensitive data | Local-first design, export preview, opt-in redaction, no upload by default | Golden tests check each redaction rule | Redaction can be incomplete |
| Target damages host | Prefer disposable VMs, snapshots, least privilege, explicit cleanup | Isolated integration tests | No isolation boundary is perfect |
| Behaviour varies across runs | Repeat-run support and variance summaries | Same-version repeat experiments | External services and time can affect behaviour |

## Operating rules

- A new event is not, by itself, evidence of malicious intent.
- Activity cannot be declared absent unless the observer could see it in the relevant interval.
- Raw evidence and derived conclusions must remain distinguishable.
- The tool must not launch an application automatically merely because it is selected.
- A container alone should not be treated as a sufficient boundary for actively hostile software; prefer disposable VMs and reduced privileges.
- Captured data and reports must not be transmitted off-device without explicit user action.
- Collection failure must remain visible rather than becoming an empty successful report.

## Release gates

Do not claim a platform is supported until its observer has fixture coverage and a documented environment test matrix. Schema changes require versioning and compatibility tests. Comparison-rule changes require regression fixtures that demonstrate what changed and what must remain unchanged. Export functionality requires redaction and accidental-inclusion tests.

## Residual risk

Driftglass can help inspect behavioural changes. It cannot prove an application safe, reliably infer intent from telemetry alone, guarantee complete observation, or replace independent security review.
