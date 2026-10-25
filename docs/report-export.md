# Local report export

The reporting module accepts a comparison report object, validates it against the versioned report schema, and verifies that the deterministic report ID matches its content before writing anything.

## Python API

The exporter is library-only; there is no CLI yet.

~~~python
from driftglass.reporting import export_html_report, export_json_report

export_json_report(report, "schemas/reports/report-v1.schema.json", "audit-report.json")
export_html_report(report, "schemas/reports/report-v1.schema.json", "audit-report.html")
~~~

Output directories must already exist. Exports refuse to overwrite existing files by default. Pass overwrite=True only when replacing a report is intended. Files created by the default path use owner-only permissions where the platform supports them. Replacements use a temporary file in the destination directory and atomic replacement where supported.

HTML output is standalone: CSS is embedded, no scripts or remote resources are added, and untrusted finding text and evidence JSON are HTML-escaped. Reports may still contain sensitive hostnames, paths, process names, and attributes. Review before sharing.

This stage does not add SQLite history, automatic export, a command-line interface, or network transmission. Report export is a local file operation initiated explicitly by the caller.
