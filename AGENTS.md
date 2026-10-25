# Driftglass — AI Agent Instructions

These instructions apply to all work in this repository. Treat `README.md` as the project brief, but inspect the actual code before making assumptions. The repository is an early-stage project: distinguish implemented behaviour from proposals, and never claim a feature works unless you have verified it.

## 1. Mission

Build Driftglass, a local-first application-update auditor that compares observable behaviour between a baseline and candidate version, then produces transparent, reproducible, evidence-linked findings.

**Slogan:** “Every update leaves a trace. Find it.”

Driftglass reports differences; it must not label software malicious or safe without sufficient evidence. Preserve uncertainty, explain rule matches, and show collection gaps. Prioritise a small, reliable end-to-end workflow over a large collection of unfinished features.

## 2. Mandatory engineering rules

- **Write the least code that achieves the required result.** Prefer the smallest clear, maintainable implementation that fully solves the task. Do not confuse fewer lines with clever, compressed, unreadable code.
- Avoid speculative abstractions, duplicate helpers, unnecessary wrappers, excessive configuration, and dependencies that replace a few straightforward lines.
- Reuse existing patterns and utilities after inspecting the repository. Do not introduce a framework, service, database, or language without a concrete need.
- Implement only the requested scope. Do not opportunistically rewrite unrelated files or build future roadmap items into a task that does not need them.
- Do not leave dead code, placeholder success paths, fake data presented as real, empty stubs, or unimplemented buttons that appear functional.
- Prefer explicit types, clear names, predictable control flow, actionable errors, and small modules with one obvious responsibility.
- Avoid silently swallowing exceptions or converting missing telemetry into “no activity.” Report incomplete runs, dropped events, denied permissions, and unsupported event sources.
- Validate external input and file paths. Handle malformed records and unexpected states deliberately.
- Keep dependencies minimal, maintained, justified, and compatible with the repository’s license.
- Never claim tests, builds, platform support, or security properties were verified unless you ran the relevant checks and inspected the results.

## 3. Planned technology stack

Follow the established architecture unless repository evidence or a documented decision justifies changing it:

- **Rust:** native observer and platform-specific system-event collectors.
- **Python:** orchestration, event normalization, comparison rules, validation, storage integration, and report generation.
- **Svelte + TypeScript:** local web dashboard and evidence exploration.
- **CSS:** dashboard styling and small, purposeful transitions.
- **SQLite:** local run metadata, findings, indexes, and report history.
- **JSONL + versioned JSON schemas:** portable event records and explicit component contracts.
- **GitHub Actions:** automated tests, checks, and build workflows.
- **Disposable virtual machines:** preferred isolation for testing untrusted applications. Containers alone must not be represented as a strong hostile-code boundary.

Rust and Python are the core implementation languages; TypeScript is for the UI. Do not add Go, Kotlin, a remote database, cloud service, or additional frontend framework unless a specific requirement warrants them. Select dependencies when an implementation needs them, not merely because they are listed here.

## 4. Architecture and data integrity

Keep platform collection, orchestration, comparison, persistence, report generation, and presentation separate through small, explicit interfaces.

- Version event schemas and report formats. Preserve source-specific evidence needed to investigate a normalized event.
- Keep observations distinct from comparisons and interpretations. Every finding should reference the evidence and rule that produced it.
- Record the app artifact identity, environment, capture profile, collector versions, active event sources, run times, errors, and known coverage gaps where available.
- Make rules deterministic, documented, and testable. A new host, file access, or process is a change to investigate—not automatic proof of malicious intent.
- Store data locally by default. Do not transmit reports or collected telemetry without an explicit, informed user action.
- Minimise collection. Do not capture packet payloads or file contents by default. Treat paths, command-line arguments, hostnames, and reports as potentially sensitive.
- Separate the dashboard from privileged collectors. Keep local API access loopback-only by default and protect it against unauthorized requests, including cross-origin abuse.
- Never execute a candidate application automatically as a side effect of opening a report or viewing the dashboard.
- Do not add automatic blocking, deletion, remediation, or silent privilege elevation to the initial workflow.

## 5. Web dashboard design

Create a premium, technical, monochrome dashboard inspired by the **design language** of Nothing OS and the precise micro-interactions of Proton Authenticator. Use these as directional references, not as a request to copy their assets, logos, screens, or exact layouts.

### Visual language
- Use a black, off-black, white, and cool-grey palette. Keep contrast high and colour rare; reserve a restrained semantic accent for meaningful states only.
- Use disciplined grids, clean alignment, thin dividers, generous negative space, compact technical labels, and strong information hierarchy.
- A subtle dot-matrix or pixel-grid motif may appear in limited places. Keep it quiet so data remains the focus.
- Prefer crisp system typography and monospaced text for hashes, timestamps, IDs, ports, and diagnostic metadata.
- Treat evidence as the main content. Summaries should lead into searchable findings, side-by-side version differences, event timelines, process trees, and clear details.
- Avoid ornamental glassmorphism, gradients everywhere, oversized cards, constant glow, excessive rounded pills, decorative charts without useful data, or cluttered “hacker” clichés.
- Ensure empty, loading, error, incomplete-run, and no-permission states are designed—not just successful results.

### Motion and Proton-inspired interactions
- Motion must clarify a state change, never delay or distract from the work.
- For changed counts, status values, and compact numeric indicators, use a refined digit-roll or vertical number-switch transition inspired by Proton Authenticator. Keep numerals aligned, transitions brief, and updates stable; do not animate every re-render.
- Use short opacity/position transitions for opening details, switching comparison views, and filtering events. Avoid springy, bouncy, or theatrical effects.
- Respect `prefers-reduced-motion`; provide an immediate, non-animated state change when motion is reduced.
- Avoid continuous animations, flashing, excessive motion, and unnecessary animation dependencies. Implement small effects in CSS/Svelte where practical.
- Animation must not misrepresent live security status, change the meaning of data, or hide a result while it is updating.

### Functional expectations
- Prioritise comparison clarity: baseline vs. candidate, added/removed/changed observations, rule explanation, evidence links, collector coverage, and uncertainty.
- Keep filters and navigation predictable. Preserve context when a user opens a finding and returns to the event list.
- Make severity and confidence distinct concepts. Never rely on colour alone to convey status.
- Design responsively, support keyboard navigation, show visible focus, label controls, and maintain accessible contrast.
- Do not invent data for a production dashboard. If fixtures are used in development, label them as sample data.

## 6. Security and privacy

Driftglass is an investigative aid, not a guarantee that software is safe or a substitute for isolation.

- Assume tested applications may be untrusted. Prefer documented disposable VM workflows and keep personal accounts, secrets, and sensitive networks out of the test environment.
- Never test suspicious binaries on the developer’s everyday host just because Driftglass is installed.
- Document collector privileges, operating-system limitations, event-source coverage, and ways instrumentation may be bypassed.
- Treat reports and fixtures as sensitive. Use synthetic data in tests and offer redaction before export.
- Keep secrets, tokens, credentials, private reports, and personal telemetry out of logs, commits, screenshots, and test fixtures.
- Avoid unsafe shell invocation and unvalidated command construction. Make subprocess arguments explicit and handle timeouts and failures.
- Report security limitations honestly. A clean report means “no changes detected within this run’s coverage,” not “safe.”

## 7. Workflow for every task

1. Inspect the relevant files, repository state, and existing conventions before editing.
2. Identify the smallest complete change and the tests needed to verify it.
3. Implement only that scope, reusing existing dependencies and patterns where sensible.
4. Add or update tests for logic, failures, malformed inputs, and regressions. Use synthetic fixtures.
5. Run the relevant formatter, linter, tests, and build checks available in the repo. Do not claim checks that could not run.
6. Review the diff for accidental changes, duplicated logic, secrets, unsupported claims, and dead code.
7. Update documentation when behaviour, schemas, configuration, permissions, or supported platforms change.
8. Summarise what changed, the checks actually run, any limitations, and any follow-up required.

If requirements are unclear, inspect existing project documentation and code first. Do not invent behaviour to fill gaps; choose a conservative, documented default.

## 8. Testing and completion criteria

A task is complete only when the implementation and its explanation agree.

- Add focused unit tests for parsing, normalization, comparisons, redaction, and rule logic as applicable.
- Add schema and integration tests at component boundaries; include malformed, missing, duplicate, and incomplete-event cases where relevant.
- Verify permission failures and unsupported event sources are surfaced instead of silently ignored.
- Use benign, controlled test programs and synthetic records. Never include real personal or network data in committed fixtures.
- For UI changes, verify loading, empty, error, reduced-motion, keyboard-focus, and narrow-layout states when relevant.
- Keep tests proportional to the change; do not build an elaborate test framework for a small function.
- Do not mark roadmap items complete merely because a directory, interface, or placeholder exists.

## 9. Documentation and collaboration

Keep `README.md`, schemas, platform coverage notes, and architecture decisions accurate. Mark proposed designs as planned and distinguish experimental support from verified support. Explain why any major deviation from this file is necessary.

Prefer focused diffs and descriptive commits. Do not reformat or rename unrelated files. Do not add badges, statistics, or marketing claims that imply maturity, coverage, or test results that have not been established.

**Guiding rule:** build the smallest trustworthy version first, prove what it observes, explain what it misses, and only then expand it.
