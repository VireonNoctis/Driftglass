import test from "node:test";
import assert from "node:assert/strict";
import { validateReportV1 } from "./report-validation.js";

const sha = "a".repeat(64);
function validReport() {
  return {
    schema_version: 1, report_id: sha, status: "complete",
    experiment_id: "synthetic-experiment", application_name: "Synthetic App",
    baseline: { run_id: "base-1", version: "1.0", artifact_sha256: sha, capture_status: "completed" },
    candidate: { run_id: "candidate-1", version: "1.1", artifact_sha256: sha, capture_status: "completed" },
    environment_differences: [],
    findings: [{
      finding_id: "b".repeat(64), rule_id: "DG-COMPARE-001", change_type: "added",
      category: "network", summary: "New candidate observation",
      observation: "Synthetic evidence only", interpretation: "Investigate; this is not proof of intent.",
      baseline_evidence: [],
      candidate_evidence: [{
        run_id: "candidate-1", event_id: "event-1", collector: "fixture",
        source_event_ref: "line:1", timestamp: "2026-01-01T00:00:00Z",
        observed: {
          category: "network", action: "socket_present", process_executable: "synthetic-app",
          resource: { kind: "network", identifier: "127.0.0.1:1000", protocol: "tcp" },
          outcome: null, attributes: {}
        }
      }]
    }],
    uncertainties: [],
    summary: {
      baseline_event_count: 1, candidate_event_count: 2,
      baseline_behaviour_event_count: 1, candidate_behaviour_event_count: 2,
      added_count: 1, removed_count: 0, changed_count: 0, uncertainty_count: 0
    }
  };
}

test("accepts a well-shaped report and preserves its evidence refs", () => {
  const result = validateReportV1(validReport());
  assert.equal(result.ok, true);
  if (result.ok) assert.equal(result.report.findings[0].candidate_evidence[0].event_id, "event-1");
});
test("rejects non-objects and unsupported schema versions", () => {
  assert.equal(validateReportV1(null).ok, false);
  assert.match(validateReportV1({ schema_version: 2 }).message, /Unsupported/);
});
test("rejects missing source references", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].source_event_ref = "";
  assert.match(validateReportV1(report).message, /findings are malformed/);
});
test("rejects malformed artifact hashes", () => {
  const report = validReport();
  report.candidate.artifact_sha256 = "not-a-hash";
  assert.match(validateReportV1(report).message, /summary is malformed/);
});
test("rejects report summary counts inconsistent with findings", () => {
  const report = validReport();
  report.summary.added_count = 2;
  assert.match(validateReportV1(report).message, /counts do not match/);
});
test("rejects invalid evidence timestamps", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].timestamp = "not-a-date";
  assert.match(validateReportV1(report).message, /findings are malformed/);
});


test("rejects report status that contradicts declared uncertainty", () => {
  const report = validReport();
  report.uncertainties = [{ code: "synthetic_gap", message: "A capture gap was reported.", run_id: null }];
  report.summary.uncertainty_count = 1;
  assert.match(validateReportV1(report).message, /status does not match/);

  report.status = "partial";
  report.uncertainties = [];
  report.summary.uncertainty_count = 0;
  assert.match(validateReportV1(report).message, /status does not match/);
});
