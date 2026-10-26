# Local report export

The reporting module accepts a comparison report object, validates it against the versioned report schema, and verifies that the deterministic report ID matches its content before writing anything.

## Python API

The exporter is available as a Python library and through the local developer CLI (driftglass compare and driftglass history export).

~~~python
from driftglass.reporting import export_html_report, export_json_report

export_json_report(report, "schemas/reports/report-v1.schema.json", "audit-report.json")
export_html_report(report, "schemas/reports/report-v1.schema.json", "audit-report.html")
~~~

Output directories must already exist. Exports refuse to overwrite existing files by default. Pass overwrite=True only when replacing a report is intended. Files created by the default path use owner-only permissions where the platform supports them. Replacements use a temporary file in the destination directory and atomic replacement where supported.

HTML output is standalone: CSS is embedded, no scripts or remote resources are added, and untrusted finding text and evidence JSON are HTML-escaped. Reports may still contain sensitive hostnames, paths, process names, and attributes. Review before sharing.

Report export is a local file operation initiated explicitly by the caller; it does not transmit data over a network.

## Per-run coverage snapshot

New comparison reports include an optional per-run coverage snapshot: source names/states and declared categories, explicitly unavailable categories, the collector-reported dropped-event count, declared errors, and the number of records actually supplied to comparison by event category. The standalone HTML exporter displays the same source declarations and input counts. Older report-v1 files without this optional property remain valid.

A zero input record count does not establish that no activity occurred. An active source declaration is not proof that all relevant activity was observed; inspect errors, dropped counts, unavailable categories, and the limits of each collector. A zero dropped-event count means only that the manifest reported zero drops, not that no observations were missed.

## Explicit privacy redaction

For exports only, the CLI accepts optional --redact-paths, --redact-network, and --redact-process-names flags. For example:

~~~bash
python -m driftglass compare \
  --baseline-manifest ./baseline-manifest.json \
  --candidate-manifest ./candidate-manifest.json \
  --baseline-events ./baseline-events.jsonl \
  --candidate-events ./candidate-events.jsonl \
  --json-output ./comparison-redacted.json \
  --html-output ./comparison-redacted.html \
  --redact-paths \
  --redact-network \
  --redact-process-names
~~~

Redaction replaces detected absolute paths, values associated with network evidence, and executable names with stable tokens within one exported report. It recomputes finding and report IDs so the redacted JSON remains valid under report schema v1. It does not modify the in-memory source report or a report already stored in SQLite; when comparison and history storage are used together, the database retains the unredacted source.

Redaction is a best-effort privacy aid, not proof that every sensitive value has been removed. Report v1 does not carry a redaction-provenance field, so keep the CLI output and label the exported file clearly. Inspect the copy before sharing.


