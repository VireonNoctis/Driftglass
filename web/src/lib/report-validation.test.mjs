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
test("rejects non-terminal capture status in a report", () => {
  const report = validReport();
  report.candidate.capture_status = "running";
  assert.equal(validateReportV1(report).ok, false);
});

test("requires a matching uncertainty for each non-completed capture", () => {
  const report = validReport();
  report.candidate.capture_status = "failed";
  report.status = "partial";
  report.uncertainties = [{
    code: "capture_not_completed",
    message: "Synthetic capture failure.",
    run_id: report.baseline.run_id
  }];
  report.summary.uncertainty_count = 1;
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /candidate capture status requires a matching/);
});

test("accepts a partial report with a run-linked incomplete-capture uncertainty", () => {
  const report = validReport();
  report.candidate.capture_status = "incomplete";
  report.status = "partial";
  report.uncertainties = [{
    code: "capture_not_completed",
    message: "Synthetic capture was incomplete.",
    run_id: report.candidate.run_id
  }];
  report.summary.uncertainty_count = 1;
  assert.equal(validateReportV1(report).ok, true);
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



test("accepts optional process identity with a valid Linux start-time tick", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].observed.process_identity = {
    pid: 123,
    parent_pid: 1,
    executable: "synthetic-app",
    start_time_ticks: 4567
  };
  assert.equal(validateReportV1(report).ok, true);
});

test("rejects malformed process identity in report evidence", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].observed.process_identity = {
    pid: -1,
    parent_pid: 1,
    executable: "synthetic-app",
    start_time_ticks: 4567
  };
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /findings are malformed/);
});



test("accepts an optional per-run coverage snapshot in report v1", () => {
  const report = validReport();
  const runCoverage = {
    event_sources: [{ name: "network-source", state: "active", categories: ["network"] }],
    unavailable_categories: ["persistence"],
    dropped_events: 2,
    errors: ["synthetic permission note"],
    recorded_event_counts: { process: 1, network: 2, file: 0, system: 0, coverage: 1 }
  };
  report.coverage = { baseline: runCoverage, candidate: { ...runCoverage, dropped_events: 0 } };
  assert.equal(validateReportV1(report).ok, true);
});

test("rejects malformed per-run coverage counts instead of showing a guessed matrix", () => {
  const report = validReport();
  report.coverage = {
    baseline: {
      event_sources: [],
      unavailable_categories: [],
      dropped_events: 0,
      errors: [],
      recorded_event_counts: { process: 0, network: -1, file: 0, system: 0, coverage: 0 }
    },
    candidate: {
      event_sources: [],
      unavailable_categories: [],
      dropped_events: 0,
      errors: [],
      recorded_event_counts: { process: 0, network: 0, file: 0, system: 0, coverage: 0 }
    }
  };
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /coverage snapshot is malformed/);
});

test("continues to accept older report-v1 fixtures without coverage", () => {
  assert.equal(validateReportV1(validReport()).ok, true);
});



test("accepts optional comparison profile metadata", () => {
  const report = validReport();
  report.comparison_profile = {
    profile_id: "network-review",
    sha256: "a".repeat(64),
    included_categories: ["network"],
    ignored_attributes: ["socket_state"]
  };
  assert.equal(validateReportV1(report).ok, true);
});

function validCoverageSnapshot(overrides = {}) {
  return {
    event_sources: [
      { name: "process-source", state: "active", categories: ["process"] },
      { name: "network-source", state: "active", categories: ["network"] },
      { name: "file-source", state: "active", categories: ["file"] },
      { name: "system-source", state: "active", categories: ["system"] },
      { name: "persistence-source", state: "active", categories: ["persistence"] }
    ],
    unavailable_categories: [],
    dropped_events: 0,
    errors: [],
    recorded_event_counts: { process: 0, network: 0, file: 0, system: 0, coverage: 0 },
    ...overrides
  };
}

test("rejects a coverage gap without a matching uncertainty", () => {
  const report = validReport();
  report.coverage = {
    baseline: validCoverageSnapshot({ dropped_events: 1 }),
    candidate: validCoverageSnapshot()
  };
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /baseline coverage snapshot does not match uncertainty codes.*events_dropped/);
});

test("rejects a coverage uncertainty linked to the wrong run", () => {
  const report = validReport();
  report.coverage = {
    baseline: validCoverageSnapshot({ errors: ["Synthetic collector error"] }),
    candidate: validCoverageSnapshot()
  };
  report.status = "partial";
  report.uncertainties = [{
    code: "collection_errors",
    message: "Synthetic collector error.",
    run_id: report.candidate.run_id
  }];
  report.summary.uncertainty_count = 1;
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /baseline coverage snapshot does not match uncertainty codes.*collection_errors/);
});

test("accepts a coverage gap when its uncertainty references the same run", () => {
  const report = validReport();
  report.coverage = {
    baseline: validCoverageSnapshot({ dropped_events: 1 }),
    candidate: validCoverageSnapshot()
  };
  report.status = "partial";
  report.uncertainties = [{
    code: "events_dropped",
    message: "The baseline reports dropped events.",
    run_id: report.baseline.run_id
  }];
  report.summary.uncertainty_count = 1;
  assert.equal(validateReportV1(report).ok, true);
});

test("rejects malformed comparison profile metadata", () => {
  const report = validReport();
  report.comparison_profile = {
    profile_id: "network review",
    sha256: "not-a-digest",
    included_categories: ["network", "network"],
    ignored_attributes: []
  };
  const result = validateReportV1(report);
  assert.equal(result.ok, false);
  assert.match(result.message, /comparison-profile metadata is malformed/);
});

test("continues to accept legacy reports without comparison profile metadata", () => {
  assert.equal(validateReportV1(validReport()).ok, true);
});


test("rejects identical baseline and candidate run IDs", () => {
  const report = validReport();
  report.candidate.run_id = report.baseline.run_id;
  assert.match(validateReportV1(report).message, /distinct runs/);
});

test("rejects evidence whose run ID belongs to the opposite side", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].run_id = report.baseline.run_id;
  assert.match(validateReportV1(report).message, /wrong run/);
});

test("rejects evidence whose observed category contradicts its finding", () => {
  const report = validReport();
  report.findings[0].candidate_evidence[0].observed.category = "process";
  assert.match(validateReportV1(report).message, /category does not match/);
});

test("rejects duplicate finding IDs", () => {
  const report = validReport();
  report.findings.push({ ...report.findings[0], summary: "Duplicate identity" });
  report.summary.added_count = 2;
  assert.match(validateReportV1(report).message, /Finding IDs must be unique/);
});

test("rejects uncertainty references outside the compared runs", () => {
  const report = validReport();
  report.uncertainties = [{ code: "gap", message: "Synthetic gap", run_id: "unknown-run" }];
  report.summary.uncertainty_count = 1;
  report.status = "partial";
  assert.match(validateReportV1(report).message, /outside this comparison/);
});

test("rejects duplicate coverage source names", () => {
  const report = validReport();
  const runCoverage = {
    event_sources: [
      { name: "same-source", state: "active", categories: ["network"] },
      { name: "same-source", state: "active", categories: ["process"] }
    ],
    unavailable_categories: [],
    dropped_events: 0,
    errors: [],
    recorded_event_counts: { process: 0, network: 1, file: 0, system: 0, coverage: 0 }
  };
  report.coverage = { baseline: runCoverage, candidate: runCoverage };
  assert.match(validateReportV1(report).message, /coverage snapshot is malformed/);
});

test("requires non-active coverage sources to declare their categories unavailable", () => {
  const report = validReport();
  const failedCoverage = {
    event_sources: [{ name: "failed-network", state: "failed", categories: ["network"] }],
    unavailable_categories: [],
    dropped_events: 0,
    errors: ["Synthetic source failure"],
    recorded_event_counts: { process: 0, network: 0, file: 0, system: 0, coverage: 0 }
  };
  const activeCoverage = {
    event_sources: [],
    unavailable_categories: [],
    dropped_events: 0,
    errors: [],
    recorded_event_counts: { process: 0, network: 0, file: 0, system: 0, coverage: 0 }
  };
  report.coverage = { baseline: failedCoverage, candidate: activeCoverage };
  assert.match(validateReportV1(report).message, /coverage snapshot is malformed/);
});


test("requires RFC 3339 timestamps with timezone and valid calendar dates", () => {
  for (const timestamp of [
    "2026-01-01",
    "2026-01-01T00:00:00",
    "2026-02-30T00:00:00Z",
    "2026-01-01T24:00:00Z",
    "2026-01-01T00:00:00+25:00"
  ]) {
    const report = validReport();
    report.findings[0].candidate_evidence[0].timestamp = timestamp;
    assert.equal(validateReportV1(report).ok, false, timestamp);
  }
});

test("accepts RFC 3339 timestamps with UTC and explicit offsets", () => {
  for (const timestamp of ["2026-01-01T00:00:00Z", "2026-01-01T08:30:00.123+08:00"]) {
    const report = validReport();
    report.findings[0].candidate_evidence[0].timestamp = timestamp;
    assert.equal(validateReportV1(report).ok, true, timestamp);
  }
});

test("accepts RFC 3339 timestamps with lowercase T and Z designators", () => {
  for (const timestamp of [
    "2026-01-01t00:00:00z",
    "2026-01-01T00:00:00z",
    "2026-01-01t08:30:00.123+08:00"
  ]) {
    const report = validReport();
    report.findings[0].candidate_evidence[0].timestamp = timestamp;
    assert.equal(validateReportV1(report).ok, true, timestamp);
  }
});
