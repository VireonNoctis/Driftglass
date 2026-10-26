import test from "node:test";
import assert from "node:assert/strict";
import { buildCoverageMatrix } from "./coverage-matrix.js";

function runCoverage(overrides = {}) {
  return {
    event_sources: [{ name: "process-source", state: "active", categories: ["process"] }],
    unavailable_categories: ["persistence"],
    dropped_events: 0,
    errors: [],
    recorded_event_counts: { process: 2, network: 0, file: 0, system: 0, coverage: 1 },
    ...overrides
  };
}

test("does not equate zero input records with unavailable coverage", () => {
  const matrix = buildCoverageMatrix({
    baseline: runCoverage(),
    candidate: runCoverage({ event_sources: [{ name: "network-source", state: "active", categories: ["network"] }] })
  });
  const network = matrix.rows.find(row => row.category === "network");
  assert.equal(network.baseline.status, "Not declared");
  assert.equal(network.baseline.record_count, 0);
  assert.equal(network.candidate.status, "Active source declared");
  assert.equal(network.candidate.record_count, 0);
});

test("shows contradictory or mixed declarations instead of choosing a status", () => {
  const run = runCoverage({
    event_sources: [
      { name: "active", state: "active", categories: ["network"] },
      { name: "failed", state: "failed", categories: ["network"] }
    ],
    unavailable_categories: ["network"]
  });
  const matrix = buildCoverageMatrix({ baseline: run, candidate: run });
  assert.equal(matrix.rows.find(row => row.category === "network").baseline.status, "Conflicting declarations");
  assert.equal(matrix.rows.find(row => row.category === "network").candidate.other_source_count, 1);
});

test("retains declarations and errors per side and handles absent old reports", () => {
  const baseline = runCoverage({ errors: ["synthetic permission issue"], dropped_events: 3 });
  const matrix = buildCoverageMatrix({ baseline, candidate: runCoverage() });
  assert.equal(matrix.baseline.errors[0], "synthetic permission issue");
  assert.equal(matrix.baseline.dropped_events, 3);
  assert.equal(buildCoverageMatrix(undefined), null);
});
