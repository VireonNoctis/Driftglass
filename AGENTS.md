# Driftglass — AI Agent Instructions

These rules apply to all work in this repository. Read `README.md` and inspect the current code before making assumptions. Driftglass is early-stage: never describe planned or stubbed features as implemented.

## Mission

Build a local-first auditor that compares observable behaviour between a baseline and candidate application version and produces reproducible, evidence-linked findings.

**Slogan:** “Every update leaves a trace. Find it.”

Report differences, not unsupported verdicts. A new host, file access, or process is something to investigate—not automatic proof of malicious intent. Always show uncertainty and collection gaps.

## Engineering rules

- **Write the least amount of code that achieves the same result.** Prefer the smallest clear, maintainable solution that fully meets the requirement. Do not use unreadable compression just to reduce line count.
- Inspect and reuse existing patterns before introducing new abstractions, helpers, frameworks, dependencies, or configuration.
- Implement only the requested scope. Avoid unrelated rewrites, speculative features, overengineering, duplicate logic, and unused code.
- Keep functions focused, names clear, and control flow easy to follow.
- Never ship fake data as real, placeholder success paths, empty stubs, or controls that appear functional but do nothing.
- Validate external inputs and paths. Handle failures explicitly; do not silently swallow errors or treat missing telemetry as “no activity.”
- Never claim a test, build, feature, platform, or security property was verified unless you ran the relevant check and examined its result.

## Technology direction

Follow this stack unless code or a documented decision gives a concrete reason to change it:

- **Rust:** native observer and OS-specific event collectors.
- **Python:** orchestration, normalization, comparison rules, validation, and reporting.
- **Svelte + TypeScript:** local dashboard and evidence exploration.
- **SQLite:** local run metadata and findings.
- **JSONL + versioned JSON schemas:** portable event records and component contracts.
- **CSS and GitHub Actions:** purposeful UI motion, styling, tests, checks, and builds.
- **Disposable VMs:** preferred isolation for untrusted software. Containers alone are not a strong hostile-code boundary.

Rust and Python are the core languages; TypeScript is for the UI. Do not add Go, Kotlin, cloud services, remote databases, or another frontend framework without a clear need.

## Architecture and evidence

Keep collection, orchestration, comparison, storage, reporting, and UI separate through simple, explicit interfaces.

- Version event schemas and report formats. Preserve source references for normalized events.
- Each finding must explain the rule, link to evidence, distinguish observation from interpretation, and show uncertainty.
- Rules should be deterministic, documented, and testable. Never claim a report proves an app is safe.
- Store data locally by default. Never transmit reports or telemetry without explicit, informed user action.
- Minimise collection: do not capture packet payloads or file contents by default. Paths, command lines, hostnames, and reports can be sensitive.
- Separate dashboard code from privileged collectors. Keep local API access loopback-only by default and protect against unauthorized and cross-origin requests.
- Never start a candidate app merely because a report is opened. Do not add automatic blocking, deletion, remediation, or silent privilege elevation to the initial workflow.

## Dashboard design: Nothing OS × Proton-inspired

Create a premium, technical, monochrome interface inspired by Nothing OS’s restrained visual language and Proton Authenticator’s precise number-switch motion. Use them as inspiration; do not copy branding, assets, or exact screens.

**Visuals**
- Black, off-black, white, and cool greys; strong contrast, rare semantic accent colour.
- Precise grid, clean alignment, thin dividers, negative space, compact labels, clear hierarchy.
- Use subtle dot-matrix/pixel details sparingly. Use monospaced text for hashes, timestamps, IDs, ports, and diagnostics.
- Make evidence the focus: baseline/candidate comparison, searchable findings, event timeline, process tree, rule explanation, coverage, and evidence details.
- Avoid constant glow, excessive gradients, ornamental glass effects, oversize cards, decorative charts, and generic “hacker” clichés.
- Design empty, loading, error, incomplete-run, and permission-denied states as carefully as successful results.

**Motion**
- Motion must explain a change, never delay or distract.
- For changing counts and compact status values, use subtle, brief digit-roll/vertical number-switch animation inspired by Proton Authenticator. Keep digits aligned and avoid replaying animation on every render.
- Use short opacity/position transitions for panels and filters; avoid bouncy, flashing, or continuous effects.
- Respect `prefers-reduced-motion` with immediate state changes. Avoid adding an animation library for effects CSS/Svelte can handle.
- Never let animation obscure, delay, or misrepresent security-relevant data.

**Usability**
- Responsive, keyboard-friendly UI with visible focus, labelled controls, and accessible contrast.

## Security and privacy

- Assume tested applications may be untrusted. Use disposable VM snapshots; keep personal accounts, secrets, and sensitive networks out of the lab.
- Use synthetic fixtures; never commit secrets, private reports, or real personal/network data.
- Avoid unsafe shell construction. Validate arguments and paths; handle subprocess failures and timeouts.
- “No changes detected” means no changes were observed within this run’s coverage—not that the app is safe.

## Workflow for every task

1. Inspect the relevant files, repository state, and conventions.
2. Define the smallest complete change and its appropriate tests.
3. Implement only that scope using existing patterns where reasonable.
4. Test normal behaviour, errors, malformed input, and relevant regressions using synthetic data.
5. Run available formatters, linters, tests, and builds; state clearly what could not run.
6. Review the diff for unrelated edits, duplicate logic, dead code, secrets, and unsupported claims.
7. Update docs when behaviour, schemas, configuration, permissions, or platform coverage change.
8. Summarise changes, checks performed, limitations, and follow-up work.

If details are unclear, inspect existing docs/code first. Never invent behaviour to fill a gap; choose a conservative, documented default.

## Testing and completion

- Add focused unit tests for parsing, normalization, comparison, rules, redaction, and serialization as applicable.
- Test schema/component boundaries, including missing, malformed, duplicate, and incomplete events where relevant.
- Ensure permission failures and unsupported event sources are visible, not silently ignored.
- For UI changes, check loading, empty, error, reduced-motion, keyboard focus, and narrow layouts as relevant.
- Keep testing proportional. A passing unit test does not prove an OS collector sees every event.
- A folder, interface, or placeholder does not complete a roadmap item.

## Documentation and collaboration

Keep `README.md`, schemas, platform notes, and architecture decisions accurate. Mark planned and experimental features clearly; only claim support that has been tested. Keep diffs focused, commits descriptive, and avoid reformatting unrelated files.

**Guiding rule:** build the smallest trustworthy version first, prove what it observes, explain what it misses, then expand it.
