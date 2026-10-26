# Driftglass Architecture Research

## Pipeline

The intended pipeline is capture → validate → normalize → compare → explain → export. Each stage should be independently testable. Raw observations, normalized events, and findings remain separate.

## Intended stack

| Area | Technology | Responsibility |
|---|---|---|
| OS-facing observer | Rust | Bounded, explicit collection through platform adapters |
| Orchestration and analysis | Python 3.13+ | Run coordination, validation, comparison, report generation |
| Read-only report viewer | Svelte + TypeScript | Evidence-linked timeline and finding inspection |
| Local report history | SQLite | Report metadata and explicit deletion |
| Interchange | JSON, JSONL, versioned JSON Schema | Inspectable event and manifest contracts |
| Tests and CI | pytest, Cargo tests, GitHub Actions | Contract, regression, and supported-platform checks |
| Isolation | Disposable virtual machine where feasible | Keep untrusted targets away from the main host |

This describes the intended broader architecture. Existing experimental features and limitations remain as stated in the root README.

## Data flow

1. Create a run ID and manifest with artifact identity, OS metadata, observer configuration, timestamps, and collection warnings.
2. Collect structured observations; never invent fields to fill gaps.
3. Validate the manifest and events against versioned contracts.
4. Normalize platform-specific fields while preserving source references.
5. Compare baseline and candidate using explicit rules, separating additions, removals, changes, ambiguous matches, and environment mismatch.
6. Generate findings linked to event IDs, rule version, explanation, and limitations.
7. Export validated JSON and standalone HTML; redaction must be a testable transformation.
8. Store only necessary local history and provide explicit deletion.

## Contract requirements

Events should identify schema version, run ID, timestamp semantics, type, source observer, and collection status. The run manifest should describe tool version, platform/build where available, privileges, sampling settings, run duration, failures, known limitations, and artifact hashes where feasible. Unknown is an allowed value.

## Trust boundaries

- The target application and all its output are untrusted.
- Imported events and reports are untrusted input.
- Observer, validator, comparison engine, and renderer have narrow interfaces.
- Imported text should be rendered as text, not interpreted as scripts or HTML.
- The default workflow should not require a network connection.

## Performance and failure behaviour

Stream large JSONL inputs, enforce size limits, and avoid unbounded memory growth. If either run is invalid, do not produce a normal comparison as if it succeeded. Partial results must be clearly marked partial.

## Decisions to record

Use architecture decision records for event identity, matching, normalization, retention, redaction defaults, compatibility, observer APIs, and isolation. Each record should list options, consequences, and evidence needed to revisit it.

## Deferred work

Cross-platform collectors, managed VM orchestration, and deeper filesystem/network coverage require measured experiments. They must not be presented as implemented until the associated tests and coverage claims exist.
