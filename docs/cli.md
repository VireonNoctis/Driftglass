# Developer CLI

The CLI is a local developer interface for existing captured data. It does not launch applications, start collectors, or initiate network requests.

## Install

From a repository checkout with Python 3.11+:

~~~bash
python -m pip install -e ".[test]"
python -m driftglass --help
~~~

Default schema paths resolve from a source checkout or from the packaged schema data in a wheel installation. Schema path options can be overridden when validating or comparing inputs.

## Validate input files

~~~bash
python -m driftglass events validate ./run-events.jsonl
python -m driftglass manifest validate ./run-manifest.json
~~~

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

## Browse local history

~~~bash
python -m driftglass history list --database ./driftglass-history.db
python -m driftglass history show --database ./driftglass-history.db --report-id REPORT_ID
python -m driftglass history export --database ./driftglass-history.db --report-id REPORT_ID --format html --output ./saved-report.html
~~~

The CLI currently consumes supplied manifests and JSONL files. Capture orchestration, automatic isolation management, automatic collection, and the dashboard are not implemented.
