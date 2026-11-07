# Local SQLite report history

The native C++20 CLI stores validated comparison reports and searchable summary rows in a local SQLite database. It does not transmit records or start collectors.

## Browse and export history

~~~bash
build/cpp/driftglass-core history list --database ./driftglass-history.sqlite
build/cpp/driftglass-core history show --database ./driftglass-history.sqlite --report-id REPORT_ID
build/cpp/driftglass-core history export \
  --database ./driftglass-history.sqlite \
  --report-id REPORT_ID \
  --format html \
  --output ./saved-report.html
~~~

History export supports JSON and standalone HTML. Opt-in redaction flags (--paths, --network, --process-names) create a separate export copy; the stored history report is not modified. Existing outputs are preserved unless the operator supplies --overwrite.

The database parent directory must already exist. A new database is created with owner-only permissions where supported. The store uses SQLite schema version 1 and refuses to modify an unversioned database that already contains tables or a database with an unsupported version.

Saving a report verifies the native report contract and content-derived report ID. Re-saving the same report is idempotent. Run IDs are unique within the local store; reusing a run ID with different experiment, application, version, artifact hash, or capture status is rejected transactionally. Reports, run associations and finding indexes are committed in one transaction.

## Explicit local cleanup

To delete one report, repeat its full report ID as the confirmation value:

~~~bash
build/cpp/driftglass-core history delete \
  --database ./driftglass-history.sqlite \
  --report-id REPORT_ID \
  --confirm REPORT_ID
~~~

The command refuses mismatched confirmation and performs report-row deletion, finding-index cleanup, report/run-association cleanup, and removal of run rows no longer referenced by any remaining report in one transaction. A report that still shares a run with another saved report does not remove that run row.

This changes only the specified local SQLite database. It does not delete original JSONL event streams, run-manifest files, exported reports, filesystem backups, or securely erase storage media. There is no automatic retention/deletion policy. History growth is bounded by 1,000 saved reports, 256 MiB of aggregate serialized report JSON, and a 512 MiB SQLite database-file cap. The database cap is enforced with SQLite page limits plus an on-open file-size check; it does not include transient rollback-journal or filesystem overhead. A save that reaches the page limit is rolled back and reports that the database-size limit or available disk space was reached. Deletion does not automatically shrink the database file.

The history index is a local convenience, not an immutable or tamper-proof evidence store. Reports and event metadata can contain sensitive information. Review database permissions and back up or remove local data according to the lab's retention policy.