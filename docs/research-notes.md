# Driftglass Research Notes

> Research question: how can two application versions be compared without mistaking missing telemetry for proof that nothing happened?

## Problem framing

An update can change process behaviour, file access, network destinations, startup activity, configuration, or operation timing. A changelog and a permission list are useful, but they are not a complete record of runtime behaviour. Driftglass is intended to collect bounded observations from baseline and candidate runs, normalize them into versioned events, and report differences with evidence a person can inspect.

Driftglass is an auditing aid, not a malware verdict and not proof of safety.

## Findings that shape the design

### Observation coverage is partial

Available operating-system telemetry depends on platform, privileges, kernel or OS version, security policy, and instrumentation. A polling observer may miss short-lived activity. A denied API or inaccessible process leaves a gap.

**Design consequence:** each run must record observer identity and version, platform, privileges, configuration, collection errors, sampling characteristics, and known limitations. The report must distinguish observed, not observed, unsupported, denied, and collection failed. These states cannot be collapsed into a single “no activity” result.

### Two runs may not be comparable

Cache state, account state, time, network responses, random identifiers, background services, and external dependencies can differ between runs.

**Design consequence:** record a run manifest and setup profile. Flag environmental mismatches, support repeat runs where useful, and do not silently remove noisy-looking differences.

### Evidence does not prove intent

A new destination or file write is observable. Whether it is malicious, expected, or irrelevant is an inference that needs context.

**Design consequence:** separate raw observations, normalized events, comparison rules, findings, and analyst explanations. Every finding should link to source events and state its limitations.

### Reproducibility requires provenance

A report should preserve enough context for another person to understand how it was generated.

**Design consequence:** use versioned JSON and JSONL records, stable identifiers, hashes where feasible, explicit tool versions, and documented redaction.

## Threats to validity

| Threat | Consequence | Planned response |
|---|---|---|
| Observer blind spots | Activity may be missed | Report coverage and known gaps |
| Environment drift | Differences may have an unrelated cause | Compare setup metadata and warn |
| Nondeterminism | Noise can look like a version change | Support repeats and show variance |
| Over-normalization | A meaningful difference may disappear | Version rules and preserve raw evidence |
| False attribution | Correlation is mistaken for causation | Cautious language and explicit limits |
| Sensitive fields | Paths, arguments, or hosts may leak | Local-first processing and reviewed export redaction |
| Malformed inputs | Parser errors or hostile records | Strict validation, limits, and adversarial fixtures |

## References

- Linux procfs documentation: https://docs.kernel.org/filesystems/proc.html
- JSON Schema: https://json-schema.org/
- OpenTelemetry concepts: https://opentelemetry.io/docs/concepts/
- OWASP Logging Cheat Sheet: https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html

These sources inform the design; they do not prove that Driftglass captures every event type described here.

## Open research questions

1. What event-loss rate is measured for each supported observer and workload?
2. Which OS and kernel versions have actually been tested?
3. How should uncertainty be communicated without hiding useful changes?
4. Which fields should be redacted by default when exporting?
5. Which environment differences invalidate a comparison?

## Research exit criteria

A claim should have a source or repeatable experiment, limitations should be recorded, proposed inferences should have falsification tests, and a report should distinguish absence of evidence from evidence of absence.

**Status:** research and engineering direction; not a complete coverage or security guarantee.
