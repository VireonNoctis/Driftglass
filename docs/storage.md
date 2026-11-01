# Local SQLite report history

The storage module keeps validated comparison reports and searchable summary rows in a local SQLite database. It does not transmit records or start collectors.

## Python API

The Python API is available to local code, and the developer CLI exposes the same store through `history list`, `history show`, and `history export` (see [Developer CLI](cli.md)).

~~~python
from driftglass.storage import get_report, list_reports, save_report

report_id = save_report("driftglass-history.db", report, "schemas/reports/report-v1.schema.json")
history = list_reports("driftglass-history.db", limit=100)
saved = get_report("driftglass-history.db", report_id, "schemas/reports/report-v1.schema.json")
~~~

The database parent directory must already exist. A new database is created with owner-only permissions where supported. The storage module uses SQLite schema version 1 and refuses to modify an unversioned database that already contains tables or a database with an unsupported version.

Saving a report verifies the report schema and deterministic report ID. Re-saving the same report is idempotent. Run IDs are unique within the local store; reusing a run ID with different experiment, application, version, artifact hash, or capture status is rejected transactionally. Reports, run associations, and finding indexes are committed in one transaction.


## Explicit local cleanup

To delete one report from the SQLite history, repeat its full report ID as the confirmation value:

~~~bash
python -m driftglass history delete \
  --database ./driftglass-history.db \
  --report-id REPORT_ID \
  --confirm REPORT_ID
~~~

The command refuses mismatched confirmation and performs the report-row deletion, finding-index cleanup, report/run-association cleanup, and removal of run rows no longer referenced by any remaining report in one transaction. A report that still shares a run with another saved report does not remove that run row.

This only changes the specified local SQLite database. It does not delete the original JSONL event streams, run-manifest files, exported reports, filesystem backups, or securely erase storage media. There is no automatic retention/deletion policy. History growth is bounded by three limits: at most 1,000 saved reports, 256 MiB of aggregate serialized report JSON, and a 512 MiB SQLite database-file cap. The database cap is enforced with SQLite's `max_page_count` plus an on-open file-size check, so it also accounts for allocated free pages and index/page overhead. An existing database above the cap is rejected rather than silently truncated. A save that reaches the page limit is rolled back and reports that the database-size limit or available disk space was reached. Delete selected reports to make their pages reusable; deletion does not automatically shrink the database file. The cap applies to the SQLite database file, not transient rollback-journal or filesystem overhead.

If a limit rejects a save, transaction rollback preserves previous reports. Back up the database if recovery from host or disk failure is required. The history index is a local convenience, not an immutable or tamper-proof evidence store. SQLite, the filesystem, and the local machine remain in the trust boundary. Report JSON and event metadata can contain sensitive information. Review database file permissions and back up or remove local data according to the lab's retention policy.

The store supports explicit, confirmation-gated deletion of one history report. It does not implement automatic retention/cleanup, report sharing, or migrations beyond schema version 1.
