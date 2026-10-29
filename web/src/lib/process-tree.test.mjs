import test from "node:test";
import assert from "node:assert/strict";
import {
  buildProcessTree,
  MAX_PROCESS_TREE_INDENT_DEPTH,
  MAX_PROCESS_TREE_ROWS_PER_GROUP,
  prepareProcessTreeForDisplay,
  processTreeIndentPixels
} from "./process-tree.js";

function item(run_id, side, pid, parent_pid, start_time_ticks, event_id, summary = event_id) {
  return {
    side,
    findings: [{ finding_id: event_id, summary }],
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

test("links a child to the nearest earlier identity when a PID is reused", () => {
  const result = buildProcessTree([
    item("base-run", "baseline", 300, 100, 50, "child"),
    item("base-run", "baseline", 100, 1, 10, "older-parent"),
    item("base-run", "baseline", 100, 1, 40, "nearest-parent"),
    item("base-run", "baseline", 100, 1, 60, "future-parent"),
  ]);
  const rows = result.groups[0].rows;
  const childIndex = rows.findIndex(row => row.pid === 300);
  assert.ok(childIndex > 0);
  assert.deepEqual(
    [rows[childIndex - 1].pid, rows[childIndex - 1].start_time_ticks],
    [100, 40]
  );
  assert.equal(rows[childIndex].depth, 1);
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

test("preserves every finding summary linked to one process event", () => {
  const shared = item("base-run", "baseline", 100, 1, 10, "shared-event", "first finding");
  shared.findings.push({ finding_id: "second-finding", summary: "second finding" });

  const result = buildProcessTree([shared]);
  assert.equal(result.node_count, 1);
  assert.equal(result.groups[0].rows[0].event_count, 1);
  assert.deepEqual(
    result.groups[0].rows[0].finding_summaries,
    ["first finding", "second finding"]
  );
});

test("deduplicates process nodes while counting each evidence event", () => {
  const first = item("base-run", "baseline", 100, 1, 10, "event-a", "first finding");
  const duplicateProcess = item("base-run", "baseline", 100, 1, 10, "event-b", "second finding");
  const result = buildProcessTree([first, duplicateProcess]);
  assert.equal(result.node_count, 1);
  assert.equal(result.groups[0].rows[0].event_count, 2);
  assert.deepEqual(result.groups[0].rows[0].finding_summaries, ["first finding", "second finding"]);
});


test("caps display indentation without truncating measured process depth", () => {
  assert.equal(processTreeIndentPixels(0), 12);
  assert.equal(processTreeIndentPixels(1), 32);
  assert.equal(processTreeIndentPixels(MAX_PROCESS_TREE_INDENT_DEPTH), 172);
  assert.equal(processTreeIndentPixels(MAX_PROCESS_TREE_INDENT_DEPTH + 1), 172);
  assert.equal(processTreeIndentPixels(7_999), 172);
  assert.equal(processTreeIndentPixels(-1), 12);
  assert.equal(processTreeIndentPixels(Number.NaN), 12);
});

test("flattens a long linked process chain without overflowing the call stack", () => {
  const count = 8_000;
  const items = Array.from({ length: count }, (_, index) =>
    item("deep-run", "baseline", index + 2, index + 1, index + 2, "event-" + index)
  );

  const result = buildProcessTree(items);
  const rows = result.groups[0].rows;

  assert.equal(result.node_count, count);
  assert.equal(rows.length, count);
  assert.equal(rows[0].pid, 2);
  assert.equal(rows[0].depth, 0);
  assert.equal(rows[count - 1].pid, count + 1);
  assert.equal(rows[count - 1].depth, count - 1);
  assert.equal(rows[count - 1].parent_status, "linked");

  const display = prepareProcessTreeForDisplay(result.groups[0]);
  assert.equal(display.rows.length, MAX_PROCESS_TREE_ROWS_PER_GROUP);
  assert.equal(display.rows[0].pid, 2);
  assert.equal(display.rows[MAX_PROCESS_TREE_ROWS_PER_GROUP - 1].depth, MAX_PROCESS_TREE_ROWS_PER_GROUP - 1);
  assert.equal(display.node_count, count);
  assert.equal(display.omitted_node_count, count - MAX_PROCESS_TREE_ROWS_PER_GROUP);
});
