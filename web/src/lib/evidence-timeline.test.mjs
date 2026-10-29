import test from "node:test";
import assert from "node:assert/strict";
import { buildEvidenceTimeline } from "./evidence-timeline.js";

function evidence(run_id, event_id, timestamp) {
  return {
    run_id,
    event_id,
    collector: "synthetic",
    source_event_ref: "line:" + event_id,
    timestamp,
    observed: {
      category: "network",
      action: "socket_present",
      process_executable: "synthetic-app",
      resource: null,
      outcome: null,
      attributes: {}
    }
  };
}

function finding(id, baseline_evidence = [], candidate_evidence = []) {
  return {
    finding_id: id,
    rule_id: "DG-COMPARE-001",
    change_type: "added",
    category: "network",
    summary: id,
    observation: "Synthetic observation",
    baseline_evidence,
    candidate_evidence,
    interpretation: "Not proof of intent."
  };
}

test("orders evidence by actual timestamp across timezone offsets", () => {
  const result = buildEvidenceTimeline([
    finding("later", [], [evidence("candidate", "later", "2026-10-26T10:30:00+01:00")]),
    finding("earlier", [evidence("baseline", "earlier", "2026-10-26T09:15:00Z")], [])
  ]);
  assert.deepEqual(result.map(item => item.evidence.event_id), ["earlier", "later"]);
});

test("deduplicates events while preserving every finding reference", () => {
  const shared = evidence("baseline", "same-event", "2026-10-26T09:00:00Z");
  const result = buildEvidenceTimeline([
    finding("finding-b", [shared], []),
    finding("finding-a", [shared], []),
    finding("candidate-copy", [], [evidence("candidate", "same-event", "2026-10-26T09:01:00Z")])
  ]);
  assert.equal(result.length, 2);
  assert.deepEqual(
    result[0].findings.map(item => item.finding_id),
    ["finding-a", "finding-b"]
  );
  assert.deepEqual(
    result[0].findings.map(item => item.summary),
    ["finding-a", "finding-b"]
  );
  assert.equal(result[1].evidence.run_id, "candidate");
  assert.deepEqual(result[1].findings.map(item => item.finding_id), ["candidate-copy"]);
});

test("handles reports without evidence", () => {
  assert.deepEqual(buildEvidenceTimeline([finding("empty")]), []);
});
