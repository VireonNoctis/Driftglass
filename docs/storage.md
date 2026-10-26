# Local SQLite report history

The storage module keeps validated comparison reports and searchable summary rows in a local SQLite database. It does not transmit records or start collectors.

## Python API

The API is library-only; there is no CLI yet.

~~~python
from driftglass.storage import get_report, list_reports, save_report

report_id = save_report("driftglass-history.db", report, "schemas/reports/report-v1.schema.json")
history = list_reports("driftglass-history.db", limit=100)
saved = get_report("driftglass-history.db", report_id, "schemas/reports/report-v1.schema.json")
~~~

The database parent directory must already exist. A new database is created with owner-only permissions where supported. The storage module uses SQLite schema version 1 and refuses to modify an unversioned database that already contains tables or a database with an unsupported version.

Saving a report verifies the report schema and deterministic report ID. Re-saving the same report is idempotent. Run IDs are unique within the local store; reusing a run ID with different experiment, application, version, artifact hash, or capture status is rejected transactionally. Reports, run associations, and finding indexes are committed in one transaction.

The history index is a local convenience, not an immutable or tamper-proof evidence store. SQLite, the filesystem, and the local machine remain in the trust boundary. Report JSON and event metadata can contain sensitive information. Review database file permissions and back up or remove local data according to the lab's retention policy.

This stage does not implement automatic cleanup, report sharing, migrations beyond schema version 1, or a command-line interface.
