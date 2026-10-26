/** @typedef {import("../types").Evidence} Evidence */
/** @typedef {import("../types").Finding} Finding */

/**
 * @typedef {Object} EvidenceTimelineItem
 * @property {Evidence} evidence
 * @property {"baseline"|"candidate"} side
 * @property {Finding} finding
 */

/**
 * Build a sorted, de-duplicated timeline from evidence explicitly linked to findings.
 * @param {Finding[]} findings
 * @returns {EvidenceTimelineItem[]}
 */
export function buildEvidenceTimeline(findings) {
  /** @type {Map<string, EvidenceTimelineItem>} */
  const unique = new Map();

  /**
   * @param {Evidence} evidence
   * @param {"baseline"|"candidate"} side
   * @param {Finding} finding
   */
  function add(evidence, side, finding) {
    const key = evidence.run_id + "\u0000" + evidence.event_id;
    if (!unique.has(key)) unique.set(key, { evidence, side, finding });
  }

  for (const finding of findings) {
    for (const evidence of finding.baseline_evidence) add(evidence, "baseline", finding);
    for (const evidence of finding.candidate_evidence) add(evidence, "candidate", finding);
  }

  return [...unique.values()].sort((left, right) => {
    const difference = Date.parse(left.evidence.timestamp) - Date.parse(right.evidence.timestamp);
    if (Number.isFinite(difference) && difference !== 0) return difference;
    const leftKey = left.evidence.run_id + ":" + left.evidence.event_id;
    const rightKey = right.evidence.run_id + ":" + right.evidence.event_id;
    return leftKey.localeCompare(rightKey);
  });
}
