# Experimental local report viewer

The Svelte viewer under web/ displays a report v1 JSON file that a user explicitly chooses or drops into the page. Before parsing, the browser rejects duplicate object keys (including equivalent escaped spellings) for both report-v1 and repeat-analysis-v1 files, matching the strict Python JSON loader. It provides run summaries, context differences, finding search and filters, a chronological timeline of evidence referenced by findings, source-evidence details, and declared uncertainty. The timeline is not the full event stream; unchanged events and standalone coverage records may not appear.

## Validation boundary

For report-v1 comparison files, the browser performs defensive structural checks and verifies evidence-link fields. It does not implement the full JSON Schema or recompute the deterministic report ID. The Python exporter and CLI remain authoritative validators. A structurally valid browser report is not a cryptographically verified export.

## Privacy and security boundaries

The viewer has no backend, history, uploader, local storage, or collector controls. It reads one user-selected file into page memory and caps accepted files at 20 MiB. It renders report values as text rather than HTML. Vite is configured to bind to loopback only.

The dashboard is a review surface, not an isolation boundary. Hostnames, process metadata, paths, and evidence references can be sensitive. A complete report means only that supported input gaps were not declared; it does not prove the app is safe or that every action was observed.

There is no local HTTP API or backend to validate in this prototype. API access-control validation is explicitly deferred until an API is proposed; see the [decision record](decisions/local-api-security-deferral.md).


## Partial process relationship view

The report viewer can draw a partial process relationship view from evidence records that include a validated PID, parent PID, executable, and Linux procfs start-time tick. A parent is linked only if evidence in the same run includes that parent PID with an earlier start tick. Evidence without a start tick is omitted from the tree rather than joined by PID alone. The view includes only process identities referenced by visible findings, not every process observed in the run. Older report files remain valid and continue to open; they may not contain enough identity metadata to populate this view.


## Per-run coverage matrix

The dashboard shows the collection-source declarations and actual input record counts copied into newer report-v1 comparison reports. Rows distinguish a source declared active, declared unavailable, inactive/failed sources, conflicting declarations, and categories not declared. Each run's source list, dropped-event count, and errors can be expanded. Older report-v1 files without this optional snapshot remain readable and show an explicit unavailable state instead of an inferred matrix.

The event counts represent records in the input JSONL presented to the comparator. A zero count does not mean no activity occurred. A declared active source does not prove comprehensive collection.


## Repeat-analysis viewer

The same read-only page can open an exported repeat-analysis-v1 JSON file. It validates the cohort shape, source report IDs/statuses, partial-report list, pattern-to-report references, recurrence labels, and summary counts before displaying the result. It shows recurring and not-consistently-observed patterns separately and preserves source report/finding IDs for manual lookup in the original comparison files.

The browser verifies the repeat-analysis content-derived `analysis_id` using the same canonical JSON rules as the Python exporter before displaying the file. This detects content/ID mismatches but is not a signature: a party can change the file and recalculate its ID. The Python exporter and repeat command remain authoritative for full JSON Schema validation. Source references are not embedded evidence, and the viewer does not load other files automatically. A recurring label means the normalized pattern was present in every supplied report; it is not a maliciousness or safety verdict. Partial runs, sampling and differences in actual environment conditions can affect repeatability.
