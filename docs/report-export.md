# Local report export

The native C++20 CLI validates the report-v1 JSON Schema and cross-field invariants before writing JSON or standalone HTML. A report can only describe stopped captures: running is not a valid final-report status. A run marked incomplete or failed makes the report partial and must have a capture_not_completed uncertainty linked to that same run ID.

When present, the optional per-run coverage snapshot must remain consistent with declared unavailable categories, inactive sources, dropped events, collection errors, undeclared coverage categories, missing artifact hashes, recorded input counts and the comparison profile. Older report-v1 files without the optional coverage snapshot remain readable.

## Native CLI

~~~bash
build/cpp/driftglass-core compare \
  --baseline-manifest ./baseline-manifest.json \
  --candidate-manifest ./candidate-manifest.json \
  --baseline-events ./baseline-events.jsonl \
  --candidate-events ./candidate-events.jsonl \
  --output ./comparison.json \
  --html-output ./comparison.html
~~~

The C++ exporter refuses to overwrite existing files unless --overwrite is explicitly supplied. HTML output is standalone: CSS is embedded, no scripts or remote resources are added, and untrusted finding text and evidence JSON are HTML-escaped.

## Per-run coverage snapshot

New comparison reports include each run's source names/states and categories, unavailable categories, collector-reported dropped-event count, declared errors, and the number of records actually supplied to comparison by event category. The standalone HTML exporter displays the same declarations and input counts.

A zero input record count does not establish that no activity occurred. An active source declaration is not proof that all relevant activity was observed; inspect errors, dropped counts, unavailable categories and collector limitations. A zero dropped-event count means only that the manifest reported zero drops, not that no observations were missed.

## Explicit privacy redaction

For exported copies, use the opt-in --paths, --network, and --process-names flags:

~~~bash
build/cpp/driftglass-core compare \
  --baseline-manifest ./baseline-manifest.json \
  --candidate-manifest ./candidate-manifest.json \
  --baseline-events ./baseline-events.jsonl \
  --candidate-events ./candidate-events.jsonl \
  --output ./comparison-redacted.json \
  --html-output ./comparison-redacted.html \
  --paths --network --process-names
~~~

Redaction replaces detected absolute paths, values associated with network evidence, and executable names with stable tokens within one exported report. It recomputes finding and report IDs so the redacted JSON remains valid under report schema v1. It does not modify the stored or source report; when comparison and history storage are used together, SQLite retains the unredacted source.

Redaction is a best-effort privacy aid, not proof that every sensitive value has been removed. Report v1 does not carry a redaction-provenance field, so keep the CLI output and label the exported file clearly. Inspect the copy before sharing.