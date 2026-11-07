if(NOT DEFINED DRIFTGLASS_CLI OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "DRIFTGLASS_CLI and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(BASELINE_MANIFEST "${WORK_DIR}/baseline-manifest.json")
set(CANDIDATE_MANIFEST "${WORK_DIR}/candidate-manifest.json")
set(BASELINE_EVENTS "${WORK_DIR}/baseline-events.jsonl")
set(CANDIDATE_EVENTS "${WORK_DIR}/candidate-events.jsonl")
set(DATABASE "${WORK_DIR}/history.sqlite")
set(JSON_EXPORT "${WORK_DIR}/report.json")
set(HTML_EXPORT "${WORK_DIR}/report.html")
set(REDACTED_EXPORT "${WORK_DIR}/report-redacted.json")
set(JSON_EXPORT_AGAIN "${WORK_DIR}/report-again.json")

file(WRITE "${BASELINE_MANIFEST}" [=[
{
  "schema_version": 1,
  "run_id": "history-export-baseline",
  "experiment_id": "history-export-smoke",
  "role": "baseline",
  "application": {"name": "Fixture App", "version": "1.0", "artifact_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "package_id": null},
  "environment": {"os": "Linux", "os_version": "fixture", "architecture": "x86_64", "snapshot_id": "snapshot-1"},
  "capture": {"status": "completed", "started_at": "2026-01-01T00:00:00Z", "ended_at": "2026-01-01T00:00:05Z", "scenario": "idle", "profile": "fixture"},
  "coverage": {
    "event_sources": [
      {"name": "fixture-process", "state": "active", "categories": ["process"]},
      {"name": "fixture-network", "state": "active", "categories": ["network"]}
    ],
    "unavailable_categories": ["file", "system", "persistence"],
    "dropped_events": 0,
    "errors": []
  }
}
]=])

file(WRITE "${CANDIDATE_MANIFEST}" [=[
{
  "schema_version": 1,
  "run_id": "history-export-candidate",
  "experiment_id": "history-export-smoke",
  "role": "candidate",
  "application": {"name": "Fixture App", "version": "2.0", "artifact_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "package_id": null},
  "environment": {"os": "Linux", "os_version": "fixture", "architecture": "x86_64", "snapshot_id": "snapshot-1"},
  "capture": {"status": "completed", "started_at": "2026-01-01T00:00:00Z", "ended_at": "2026-01-01T00:00:05Z", "scenario": "idle", "profile": "fixture"},
  "coverage": {
    "event_sources": [
      {"name": "fixture-process", "state": "active", "categories": ["process"]},
      {"name": "fixture-network", "state": "active", "categories": ["network"]}
    ],
    "unavailable_categories": ["file", "system", "persistence"],
    "dropped_events": 0,
    "errors": []
  }
}
]=])

file(WRITE "${BASELINE_EVENTS}" [=[
{"schema_version":1,"event_id":"history-before","timestamp":"2026-01-01T00:00:01Z","category":"network","action":"socket_observed","source":{"collector":"fixture","event_ref":"fixture:before"},"process":{"pid":10,"parent_pid":1,"executable":"/opt/fixture"},"resource":{"kind":"network","identifier":"127.0.0.1:53000","address":"198.51.100.10","port":443,"protocol":"tcp"},"collection_quality":"complete","attributes":{"local_port":53000,"socket_inode":12345}}
]=])
file(WRITE "${CANDIDATE_EVENTS}" [=[
{"schema_version":1,"event_id":"history-after","timestamp":"2026-01-01T00:00:01Z","category":"network","action":"socket_observed","source":{"collector":"fixture","event_ref":"fixture:after"},"process":{"pid":10,"parent_pid":1,"executable":"/opt/fixture"},"resource":{"kind":"network","identifier":"127.0.0.1:54000","address":"198.51.100.11","port":443,"protocol":"tcp"},"collection_quality":"complete","attributes":{"local_port":54000,"socket_inode":67890}}
]=])

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" compare
        --baseline-manifest "${BASELINE_MANIFEST}"
        --candidate-manifest "${CANDIDATE_MANIFEST}"
        --baseline-events "${BASELINE_EVENTS}"
        --candidate-events "${CANDIDATE_EVENTS}"
        --history-db "${DATABASE}"
    RESULT_VARIABLE compare_result
    OUTPUT_VARIABLE compare_output
    ERROR_VARIABLE compare_error
)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "compare fixture failed: ${compare_error}")
endif()

string(JSON report_id ERROR_VARIABLE report_id_error GET "${compare_output}" report_id)
if(report_id_error OR NOT report_id MATCHES "^[0-9a-f]{64}$")
    message(FATAL_ERROR "compare did not return a valid report ID: ${report_id_error}")
endif()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" history export
        --database "${DATABASE}" --report-id "${report_id}"
        --format json --output "${JSON_EXPORT}"
    RESULT_VARIABLE json_result
    ERROR_VARIABLE json_error
)
if(NOT json_result EQUAL 0)
    message(FATAL_ERROR "JSON history export failed: ${json_error}")
endif()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" report validate "${JSON_EXPORT}"
    RESULT_VARIABLE validation_result
    ERROR_VARIABLE validation_error
)
if(NOT validation_result EQUAL 0)
    message(FATAL_ERROR "exported JSON report did not validate: ${validation_error}")
endif()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" history export
        --database "${DATABASE}" --report-id "${report_id}"
        --format html --output "${HTML_EXPORT}"
    RESULT_VARIABLE html_result
    ERROR_VARIABLE html_error
)
if(NOT html_result EQUAL 0)
    message(FATAL_ERROR "HTML history export failed: ${html_error}")
endif()
file(READ "${HTML_EXPORT}" html_content)
string(FIND "${html_content}" "<html" html_marker)
if(html_marker EQUAL -1)
    message(FATAL_ERROR "HTML history export is not a standalone HTML document")
endif()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" history export
        --database "${DATABASE}" --report-id "${report_id}"
        --format json --output "${REDACTED_EXPORT}"
        --paths --network --process-names
    RESULT_VARIABLE redaction_result
    ERROR_VARIABLE redaction_error
)
if(NOT redaction_result EQUAL 0)
    message(FATAL_ERROR "redacted history export failed: ${redaction_error}")
endif()

file(READ "${JSON_EXPORT}" original_report)
file(READ "${REDACTED_EXPORT}" redacted_report)
foreach(sensitive_value IN ITEMS "/opt/fixture" "198.51.100.10" "198.51.100.11")
    string(FIND "${redacted_report}" "${sensitive_value}" leaked_position)
    if(NOT leaked_position EQUAL -1)
        message(FATAL_ERROR "redacted export still contains ${sensitive_value}")
    endif()
endforeach()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" history export
        --database "${DATABASE}" --report-id "${report_id}"
        --format json --output "${JSON_EXPORT_AGAIN}"
    RESULT_VARIABLE repeat_export_result
    ERROR_VARIABLE repeat_export_error
)
if(NOT repeat_export_result EQUAL 0)
    message(FATAL_ERROR "re-exporting the source report failed: ${repeat_export_error}")
endif()
file(READ "${JSON_EXPORT_AGAIN}" source_after_redaction)
if(NOT original_report STREQUAL source_after_redaction)
    message(FATAL_ERROR "redacted export mutated the report stored in local history")
endif()

execute_process(
    COMMAND "${DRIFTGLASS_CLI}" history export
        --database "${DATABASE}" --report-id "${report_id}"
        --format json --output "${JSON_EXPORT}"
    RESULT_VARIABLE overwrite_result
    ERROR_VARIABLE overwrite_error
)
if(overwrite_result EQUAL 0)
    message(FATAL_ERROR "history export unexpectedly overwrote an existing file")
endif()
file(READ "${JSON_EXPORT}" source_after_overwrite_attempt)
if(NOT original_report STREQUAL source_after_overwrite_attempt)
    message(FATAL_ERROR "a refused overwrite changed the existing export")
endif()

message(STATUS "native history export, redaction and overwrite checks passed")
