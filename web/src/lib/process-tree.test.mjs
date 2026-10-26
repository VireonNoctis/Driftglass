import test from "node:test";
import assert from "node:assert/strict";
import { buildProcessTree } from "./process-tree.js";

function item(run_id, side, pid, parent_pid, start_time_ticks, event_id, summary = event_id) {
  return {
    side,
    finding: { summary },
    evidence: {
      run_id,
      event_id,
      timestamp: "2026-10-26T10:00:00Z",
      observed: {
        process_identity: {
          pid,
          parent_pid,
          executable: "process-" + pid,
          start_time_ticks
        }
      }
    }
  };
}

test("links only same-run parents whose observed start tick is earlier", () => {
  const result = buildProcessTree([
    item("base-run", "baseline", 200, 100, 50, "child-event"),
    item("base-run", "baseline", 100, 1, 20, "parent-event"),
  ]);
  assert.equal(result.node_count, 2);
  const rows = result.groups[0].rows;
  assert.deepEqual(rows.map(row => [row.pid, row.depth, row.parent_status]), [
    [100, 0, "not-in-evidence"],
    [200, 1, "linked"]
  ]);
});

test("does not link across runs or infer a parent when start ticks are ambiguous", () => {
  const result = buildProcessTree([
    item("base-run", "baseline", 200, 100, 20, "child"),
    item("other-run", "candidate", 100, 1, 10, "parent-other-run"),
    item("base-run", "baseline", 100, 1, 20, "same-tick-parent"),
  ]);
  const child = result.groups.find(group => group.run_id === "base-run").rows.find(row => row.pid === 200);
  assert.equal(child.parent_status, "not-in-evidence");
  assert.equal(child.depth, 0);
});

test("omits evidence without start-time identity and keeps run groups separate", () => {
  const result = buildProcessTree([
    item("base-run", "baseline", 200, 100, 20, "base-child"),
    {
      side: "candidate",
      finding: { summary: "old report" },
      evidence: { run_id: "candidate-run", event_id: "legacy", timestamp: "2026-10-26T10:00:00Z", observed: {} }
    },
  ]);
  assert.equal(result.skipped_evidence_count, 1);
  assert.equal(result.node_count, 1);
  assert.equal(result.groups.length, 1);
  assert.equal(result.groups[0].run_id, "base-run");
});

test("deduplicates process nodes while counting each evidence event", () => {
  const first = item("base-run", "baseline", 100, 1, 10, "event-a", "first finding");
  const duplicateProcess = item("base-run", "baseline", 100, 1, 10, "event-b", "second finding");
  const result = buildProcessTree([first, duplicateProcess]);
  assert.equal(result.node_count, 1);
  assert.equal(result.groups[0].rows[0].event_count, 2);
  assert.deepEqual(result.groups[0].rows[0].finding_summaries, ["first finding", "second finding"]);
});
