/** @typedef {import("../types").ReportV1} ReportV1 */

/** @param {unknown} value @returns {value is Record<string, any>} */
function isRecord(value) {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
/** @param {unknown} value @returns {value is string} */
function isText(value) {
  return typeof value === "string" && value.length > 0;
}
/** @param {unknown} value @returns {value is string | null} */
function isNullableText(value) {
  return value === null || typeof value === "string";
}
/** @param {unknown} value @returns {value is number} */
function isCount(value) {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
}
/** @param {unknown} value @returns {boolean} */
function isRun(value) {
  return isRecord(value) &&
    isText(value.run_id) &&
    isText(value.version) &&
    (value.artifact_sha256 === null ||
      (typeof value.artifact_sha256 === "string" && /^[a-f0-9]{64}$/.test(value.artifact_sha256))) &&
    ["running", "completed", "incomplete", "failed"].includes(value.capture_status);
}
/** @param {unknown} value @returns {boolean} */
function isEvidence(value) {
  return isRecord(value) &&
    isText(value.run_id) && isText(value.event_id) &&
    isText(value.collector) && isText(value.source_event_ref) &&
    typeof value.timestamp === "string" && Number.isFinite(Date.parse(value.timestamp)) &&
    isRecord(value.observed) &&
    isText(value.observed.category) && isText(value.observed.action) &&
    isNullableText(value.observed.process_executable) &&
    (value.observed.resource === null || isRecord(value.observed.resource)) &&
    isNullableText(value.observed.outcome) && isRecord(value.observed.attributes);
}
/** @param {unknown} value @returns {boolean} */
function isFinding(value) {
  if (!isRecord(value)) return false;
  if (typeof value.finding_id !== "string" || !/^[a-f0-9]{64}$/.test(value.finding_id)) return false;
  if (!["DG-COMPARE-001", "DG-COMPARE-002"].includes(value.rule_id)) return false;
  if (!["added", "removed", "changed"].includes(value.change_type)) return false;
  if (!["process", "network", "file", "system"].includes(value.category)) return false;
  if (!isText(value.summary) || !isText(value.observation) || !isText(value.interpretation)) return false;
  if (!Array.isArray(value.baseline_evidence) || !value.baseline_evidence.every(isEvidence)) return false;
  if (!Array.isArray(value.candidate_evidence) || !value.candidate_evidence.every(isEvidence)) return false;
  if (value.change_type === "added" && (value.baseline_evidence.length || !value.candidate_evidence.length)) return false;
  if (value.change_type === "removed" && (!value.baseline_evidence.length || value.candidate_evidence.length)) return false;
  if (value.change_type === "changed" && (!value.baseline_evidence.length || !value.candidate_evidence.length)) return false;
  return true;
}

/**
 * Defensive browser-side shape validation, not a replacement for the Python schema validator.
 * @param {unknown} value
 * @returns {{ok: true, report: ReportV1} | {ok: false, message: string}}
 */
export function validateReportV1(value) {
  if (!isRecord(value)) return { ok: false, message: "The file must contain a JSON object." };
  if (value.schema_version !== 1) return { ok: false, message: "Unsupported report schema version." };
  if (typeof value.report_id !== "string" || !/^[a-f0-9]{64}$/.test(value.report_id)) {
    return { ok: false, message: "The report ID is missing or malformed." };
  }
  if (!["complete", "partial"].includes(value.status)) return { ok: false, message: "The report status is invalid." };
  if (!isText(value.experiment_id) || !isText(value.application_name)) {
    return { ok: false, message: "The report is missing its experiment or application identity." };
  }
  if (!isRun(value.baseline) || !isRun(value.candidate)) {
    return { ok: false, message: "The baseline or candidate summary is malformed." };
  }
  if (!Array.isArray(value.environment_differences) || !value.environment_differences.every((item) =>
    isRecord(item) && isText(item.field) &&
    isNullableText(item.baseline_value) && isNullableText(item.candidate_value))) {
    return { ok: false, message: "Environment differences are malformed." };
  }
  if (!Array.isArray(value.findings) || !value.findings.every(isFinding)) {
    return { ok: false, message: "One or more findings are malformed or have invalid evidence links." };
  }
  if (!Array.isArray(value.uncertainties) || !value.uncertainties.every((item) =>
    isRecord(item) && isText(item.code) && isText(item.message) && isNullableText(item.run_id))) {
    return { ok: false, message: "One or more uncertainty records are malformed." };
  }
  const summary = value.summary;
  const keys = [
    "baseline_event_count", "candidate_event_count",
    "baseline_behaviour_event_count", "candidate_behaviour_event_count",
    "added_count", "removed_count", "changed_count", "uncertainty_count"
  ];
  if (!isRecord(summary) || !keys.every((key) => isCount(summary[key]))) {
    return { ok: false, message: "The report summary contains invalid counts." };
  }
  for (const type of ["added", "removed", "changed"]) {
    if (summary[type + "_count"] !== value.findings.filter((finding) => finding.change_type === type).length) {
      return { ok: false, message: "Finding counts do not match the report summary." };
    }
  }
  if (summary.uncertainty_count !== value.uncertainties.length) {
    return { ok: false, message: "Uncertainty count does not match the report summary." };
  }
  if ((value.status === "partial") !== (value.uncertainties.length > 0)) {
    return { ok: false, message: "Report status does not match declared uncertainty." };
  }
  return { ok: true, report: /** @type {ReportV1} */ (value) };
}
