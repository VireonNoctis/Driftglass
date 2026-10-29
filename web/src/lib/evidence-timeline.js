/** @typedef {import("../types").Evidence} Evidence */
/** @typedef {import("../types").Finding} Finding */

/**
 * @typedef {Object} EvidenceTimelineItem
 * @property {Evidence} evidence
 * @property {"baseline"|"candidate"} side
 * @property {Finding[]} findings
 */

/**
 * Encode the evidence identity as a tuple so delimiter characters in IDs cannot collide.
 * @param {Evidence} evidence
 * @returns {string}
 */
export function evidenceTimelineKey(evidence) {
  return JSON.stringify([evidence.run_id, evidence.event_id]);
}

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
    const key = evidenceTimelineKey(evidence);
    const existing = unique.get(key);
    if (!existing) {
      unique.set(key, { evidence, side, findings: [finding] });
    } else if (!existing.findings.some(item => item.finding_id === finding.finding_id)) {
      existing.findings.push(finding);
    }
  }

  for (const finding of findings) {
    for (const evidence of finding.baseline_evidence) add(evidence, "baseline", finding);
    for (const evidence of finding.candidate_evidence) add(evidence, "candidate", finding);
  }

  for (const item of unique.values()) {
    item.findings.sort((left, right) => left.finding_id.localeCompare(right.finding_id));
  }

  return [...unique.values()].sort((left, right) => {
    const difference = Date.parse(left.evidence.timestamp) - Date.parse(right.evidence.timestamp);
    if (Number.isFinite(difference) && difference !== 0) return difference;
    const leftKey = evidenceTimelineKey(left.evidence);
    const rightKey = evidenceTimelineKey(right.evidence);
    return leftKey.localeCompare(rightKey);
  });
}
