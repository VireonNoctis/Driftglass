# Developer CLI

The CLI is a local developer interface for existing captured data and an experimental wrapper around the Linux observer. Its capture command starts only the observer for a user-supplied PID that is already running; it does not launch or stop the target application, elevate privileges, manage a VM, or initiate network requests itself.

## Install

From a repository checkout with Python 3.11+:

~~~bash
python -m pip install -e ".[test]"
python -m driftglass --help
~~~

Default schema paths resolve from a source checkout or from the packaged schema data in a wheel installation. Schema path options can be overridden when validating or comparing inputs.

## Inspect collector capabilities

~~~bash
python -m driftglass capabilities
~~~

This prints versioned JSON describing the implemented Linux procfs observer, its narrow process/TCP metadata scope, unavailable categories, and platforms without a collector. It reports implementation status, not the capabilities or health of a specific running process; real-world Linux coverage remains unverified.

## Validate input files

~~~bash
python -m driftglass report validate ./comparison.json
~~~

Report validation checks the v1 schema, cross-field evidence/run relationships, comparison-profile digest, and content-derived report ID. A valid content hash detects changes relative to the report contents; it is not a digital signature and does not prove that the original capture was complete. Imported comparison JSON is limited to 64 MiB per file. Run manifests are limited to 4 MiB and comparison rule profiles to 1 MiB.

~~~bash
python -m driftglass events validate ./run-events.jsonl
python -m driftglass manifest validate ./run-manifest.json
~~~


## Capture an already-running Linux process

Build the experimental observer first:

~~~bash
cargo build --release -p driftglass-agent
~~~

Then run the capture wrapper inside the intended test environment, supplying the PID of an application that is already running:

~~~bash
python -m driftglass capture \
  --observer ./target/release/driftglass-agent \
  --pid 1234 \
  --duration-seconds 30 \
  --interval-ms 250 \
  --experiment-id example-update \
  --role baseline \
  --application-name ExampleApp \
  --application-version 1.0 \
  --scenario "Open the main window and wait for 30 seconds" \
  --event-output ./baseline-events.jsonl \
  --manifest-output ./baseline-manifest.json
~~~

Use the same experiment ID and equivalent scenarios for the baseline and candidate runs, changing the role, application version, PID, output paths, and other metadata as appropriate. Repeat the capture command for the candidate version. Output files must be new, their parent directories must already exist, and the command refuses to overwrite an existing file.

The wrapper invokes the observer with an argument list, validates the resulting JSONL stream, and writes a schema-validated run manifest. A failed or incomplete capture writes a manifest with its status and then exits non-zero. The optional artifact hash is supplied by the user and is not verified against the running executable. The manifest records the host OS/kernel release and architecture; it does not prove that the process ran in an isolated VM.

The Linux observer samples procfs; it can miss short-lived activity and only reports process and TCP metadata. File, system, and persistence activity are declared unavailable. Inspect coverage records and manifest errors before comparing runs. A completed capture is not evidence of comprehensive collection or application safety.

## Compare captured runs and optionally persist/export

~~~bash
python -m driftglass compare \
  --baseline-manifest ./baseline-manifest.json \
  --candidate-manifest ./candidate-manifest.json \
  --baseline-events ./baseline-events.jsonl \
  --candidate-events ./candidate-events.jsonl \
  --json-output ./comparison.json \
  --html-output ./comparison.html \
  --history-db ./driftglass-history.db
~~~

When no output or history option is passed, the complete report JSON is printed to stdout. Exported files refuse to overwrite existing files unless the explicit overwrite option is supplied. A history database is local and may contain sensitive process and network metadata.


### Redact an export before sharing

The compare and history export commands accept these opt-in flags:

- --redact-paths
- --redact-network
- --redact-process-names

Use only the categories you need. Redaction returns a separate report copy, replaces repeated values with consistent tokens, and recalculates the finding/report IDs. If a report is also saved to SQLite during compare, history retains the unredacted source. Redaction flags require a JSON or HTML file export.

The report v1 JSON schema has no redaction-provenance field. The CLI reports the redacted export ID and source report ID; keep that output and label the exported file clearly. Redaction is best-effort, so inspect the result for remaining sensitive data before sharing.


## Delete one local history report

~~~bash
python -m driftglass history delete \
  --database ./driftglass-history.db \
  --report-id REPORT_ID \
  --confirm REPORT_ID
~~~

The confirmation value must exactly match the full report ID. This irreversibly removes that report and its finding/report-run index rows from the selected SQLite database, then removes only run metadata no longer referenced by another report. It does not delete source JSONL, manifest, or exported files, and does not securely erase backups or storage media. No automatic cleanup is performed.

## Browse local history

~~~bash
python -m driftglass history list --database ./driftglass-history.db
python -m driftglass history show --database ./driftglass-history.db --report-id REPORT_ID
python -m driftglass history export --database ./driftglass-history.db --report-id REPORT_ID --format html --output ./saved-report.html
~~~

The capture command is experimental and coordinates one bounded observer session; it does not create the test environment, launch the application, or automate baseline/candidate workflows. Automatic VM lifecycle management, target launch coordination, and broader platform collection remain unimplemented or unverified. The repeat command summarizes previously generated reports only; it does not schedule captures or automate test runs.
