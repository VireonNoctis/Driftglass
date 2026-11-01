# Privacy model

## Default handling

Driftglass processes capture records, comparisons, exports, and SQLite history locally. The current implementation has no telemetry upload, remote database, background service, or local HTTP API. The browser viewer reads a report selected by the operator and keeps its working data in page memory; it does not start the collector.

## Data inventory

| Data | Why it exists | Sensitivity and handling |
|---|---|---|
| Process metadata | Associate sampled activity with the selected process tree. | PID, parent PID, executable name and procfs start ticks may identify local processes; no command-line arguments or process environment are collected by this observer. |
| TCP metadata | Compare sampled socket behavior. | Local/remote addresses, ports, state and inferred owning process may reveal internal network layout. TCP only; no packet payloads, DNS or UDP. |
| Run manifest | Describe application metadata, environment, scenario, source status and declared gaps. | Scenario, OS/build, snapshot label, application version, and user-supplied artifact hash can be sensitive. Artifact hash is not verified against the running executable. |
| Reports/history | Preserve differences, evidence references, rule profiles and uncertainty. | Reports may duplicate sensitive fields; SQLite history is not tamper-proof. |
| Exports | Share a selected result. | JSON/HTML can disclose paths, hosts, addresses and executable names. Export redaction is explicit and best-effort, not proof all sensitive fields are removed. |

## Collection minimization

The Linux observer reads procfs process stat, TCP/TCP6 tables, and tracked process file-descriptor links to infer socket ownership. It does not read file contents, process command lines, process environment, packet payloads, DNS, UDP, file access events, or persistence settings. The wrapper declares file, system and persistence categories unavailable. Process and network categories also remain partial/best-effort.

## Storage and deletion

- Capture outputs must be new files. On POSIX, the Python manifest writer uses owner-only permissions; observer output files also use owner-only permissions.
- The operator selects when to export reports or save a comparison to SQLite.
- SQLite history is bounded to a 512 MiB database-file cap, a 256 MiB aggregate report-JSON payload cap, and 1,000 reports. The file cap does not include transient rollback-journal or filesystem overhead.
- SQLite report deletion requires the full report ID and matching explicit confirmation. It deletes selected local report/index data, not original JSONL, manifest, export copies, backups, or storage-media remnants.
- Redaction creates an exported copy. Local history, when requested, retains the unredacted source report.

## Network behavior

The capture wrapper starts the observer process locally and does not itself initiate network requests. The observed application may create network connections inside its test environment; these are observations, not Driftglass uploads. No feature in the current CLI sends reports to a service.

## User responsibilities and residual risks

Keep untrusted applications inside a disposable VM or equivalent isolation that you prepare and manage. Do not place personal accounts, credentials or private files in the lab. Review all report and export fields before sharing. Redaction is selective and best-effort; do not treat redacted output as anonymized or guaranteed safe to disclose.
