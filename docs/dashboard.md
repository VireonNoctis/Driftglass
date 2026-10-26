# Experimental local report viewer

The Svelte viewer under web/ displays a report v1 JSON file that a user explicitly chooses or drops into the page. It provides run summaries, context differences, finding search and filters, a chronological timeline of evidence referenced by findings, source-evidence details, and declared uncertainty. The timeline is not the full event stream; unchanged events and standalone coverage records may not appear.

## Validation boundary

The browser performs defensive structural checks and verifies evidence-link fields. It does not implement the full JSON Schema or recompute the deterministic report ID. The Python exporter and CLI remain authoritative validators. A structurally valid browser report is not a cryptographically verified export.

## Privacy and security boundaries

The viewer has no backend, history, uploader, local storage, or collector controls. It reads one user-selected file into page memory and caps accepted files at 20 MiB. It renders report values as text rather than HTML. Vite is configured to bind to loopback only.

The dashboard is a review surface, not an isolation boundary. Hostnames, process metadata, paths, and evidence references can be sensitive. A complete report means only that supported input gaps were not declared; it does not prove the app is safe or that every action was observed.


## Partial process relationship view

The report viewer can draw a partial process relationship view from evidence records that include a validated PID, parent PID, executable, and Linux procfs start-time tick. A parent is linked only if evidence in the same run includes that parent PID with an earlier start tick. Evidence without a start tick is omitted from the tree rather than joined by PID alone. The view includes only process identities referenced by visible findings, not every process observed in the run. Older report files remain valid and continue to open; they may not contain enough identity metadata to populate this view.
