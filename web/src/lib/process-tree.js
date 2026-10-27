/** @typedef {import("../types").ProcessIdentity} ProcessIdentity */
/** @typedef {import("./evidence-timeline.js").EvidenceTimelineItem} EvidenceTimelineItem */

/**
 * @typedef {Object} InternalNode
 * @property {string} key
 * @property {string} run_id
 * @property {"baseline"|"candidate"} side
 * @property {number} pid
 * @property {number} parent_pid
 * @property {string} executable
 * @property {number} start_time_ticks
 * @property {number} event_count
 * @property {string} last_seen
 * @property {string[]} finding_summaries
 * @property {number} depth
 * @property {"linked"|"root"|"not-in-evidence"} parent_status
 * @property {InternalNode|null} parent
 * @property {InternalNode[]} children
 * @property {Set<string>} finding_set
 * @property {number} last_seen_ms
 */

/**
 * @typedef {Omit<InternalNode, "parent"|"children"|"finding_set"|"last_seen_ms">} ProcessTreeRow
 */

/**
 * Build a conservative process relationship view from evidence linked to findings.
 * A parent link requires a matching parent PID in the same run with a strictly
 * earlier procfs start-time tick. PID-only identity is never enough to form a link.
 *
 * @param {EvidenceTimelineItem[]} items
 * @returns {{groups: Array<{run_id:string, side:"baseline"|"candidate", rows:ProcessTreeRow[], node_count:number}>, skipped_evidence_count:number, node_count:number}}
 */
export function buildProcessTree(items) {
  /** @type {Map<string, InternalNode>} */
  const nodesByKey = new Map();
  let skippedEvidenceCount = 0;

  for (const item of items) {
    /** @type {ProcessIdentity|undefined} */
    const identity = item.evidence.observed.process_identity;
    if (
      !identity ||
      !Number.isSafeInteger(identity.pid) || identity.pid < 0 ||
      !Number.isSafeInteger(identity.parent_pid) || identity.parent_pid < 0 ||
      typeof identity.start_time_ticks !== "number" ||
      !Number.isSafeInteger(identity.start_time_ticks) || identity.start_time_ticks < 0 ||
      typeof identity.executable !== "string"
    ) {
      skippedEvidenceCount += 1;
      continue;
    }

    const key = item.evidence.run_id + "\u0000" + identity.pid + "\u0000" + identity.start_time_ticks;
    let node = nodesByKey.get(key);
    if (!node) {
      const observedTime = Date.parse(item.evidence.timestamp);
      node = {
        key,
        run_id: item.evidence.run_id,
        side: item.side,
        pid: identity.pid,
        parent_pid: identity.parent_pid,
        executable: identity.executable,
        start_time_ticks: identity.start_time_ticks,
        event_count: 0,
        last_seen: item.evidence.timestamp,
        finding_summaries: [],
        depth: 0,
        parent_status: "root",
        parent: null,
        children: [],
        finding_set: new Set(),
        last_seen_ms: Number.isFinite(observedTime) ? observedTime : 0,
      };
      nodesByKey.set(key, node);
    }

    node.event_count += 1;
    const observedTime = Date.parse(item.evidence.timestamp);
    if (Number.isFinite(observedTime) && observedTime > node.last_seen_ms) {
      node.last_seen_ms = observedTime;
      node.last_seen = item.evidence.timestamp;
    }
    if (!node.finding_set.has(item.finding.summary)) {
      node.finding_set.add(item.finding.summary);
      node.finding_summaries.push(item.finding.summary);
    }
  }

  /** @type {Map<string, InternalNode[]>} */
  const byRunPid = new Map();
  for (const node of nodesByKey.values()) {
    const index = node.run_id + "\u0000" + node.pid;
    const entries = byRunPid.get(index) ?? [];
    entries.push(node);
    byRunPid.set(index, entries);
  }

  for (const node of nodesByKey.values()) {
    if (node.parent_pid === 0 || node.parent_pid === node.pid) {
      node.parent_status = "root";
      continue;
    }

    const candidates = (byRunPid.get(node.run_id + "\u0000" + node.parent_pid) ?? [])
      .filter(parent => parent.start_time_ticks < node.start_time_ticks)
      .sort((left, right) => right.start_time_ticks - left.start_time_ticks);

    const parent = candidates[0];
    if (parent) {
      node.parent = parent;
      node.parent_status = "linked";
      parent.children.push(node);
    } else {
      node.parent_status = "not-in-evidence";
    }
  }

  /** @param {InternalNode} left @param {InternalNode} right */
  function compareNodes(left, right) {
    return left.start_time_ticks - right.start_time_ticks ||
      left.pid - right.pid ||
      left.key.localeCompare(right.key);
  }
  for (const node of nodesByKey.values()) node.children.sort(compareNodes);

  /** @type {Map<string, {run_id:string, side:"baseline"|"candidate", roots:InternalNode[], node_count:number}>} */
  const groups = new Map();
  for (const node of nodesByKey.values()) {
    const groupKey = node.run_id + "\u0000" + node.side;
    let group = groups.get(groupKey);
    if (!group) {
      group = { run_id: node.run_id, side: node.side, roots: [], node_count: 0 };
      groups.set(groupKey, group);
    }
    group.node_count += 1;
    if (!node.parent) group.roots.push(node);
  }

  /**
   * Flatten the process tree without using one JavaScript call frame per level.
   * Imported reports are untrusted, and a long parent chain can exceed the browser's
   * recursion stack even when the JSON itself is within the documented input limits.
   * @param {InternalNode[]} roots
   * @param {ProcessTreeRow[]} rows
   */
  function flattenRoots(roots, rows) {
    const pending = roots.slice().reverse().map(node => ({ node, depth: 0 }));

    while (pending.length > 0) {
      const entry = pending.pop();
      if (!entry) break;
      const { node, depth } = entry;
      const { parent, children, finding_set, last_seen_ms, ...visibleNode } = node;
      rows.push({ ...visibleNode, depth, finding_summaries: [...finding_set] });

      for (let index = children.length - 1; index >= 0; index -= 1) {
        pending.push({ node: children[index], depth: depth + 1 });
      }
    }
  }

  return {
    groups: [...groups.values()]
      .sort((left, right) =>
        (left.side === right.side ? 0 : left.side === "baseline" ? -1 : 1) ||
        left.run_id.localeCompare(right.run_id)
      )
      .map(group => {
        group.roots.sort(compareNodes);
        /** @type {ProcessTreeRow[]} */
        const rows = [];
        flattenRoots(group.roots, rows);
        return { run_id: group.run_id, side: group.side, rows, node_count: group.node_count };
      }),
    skipped_evidence_count: skippedEvidenceCount,
    node_count: nodesByKey.size,
  };
}
