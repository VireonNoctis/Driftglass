const SHA256 = /^[a-f0-9]{64}$/;
const CATEGORIES = ["process", "network", "file", "system"];

function isRecord(value) {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
function isHash(value) {
  return typeof value === "string" && SHA256.test(value);
}
function isNullableHash(value) {
  return value === null || isHash(value);
}
function isCount(value) {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
}
function isText(value, max = Number.MAX_SAFE_INTEGER) {
  return typeof value === "string" && value.length > 0 && value.length <= max;
}
function isCohort(value) {
  return isRecord(value) &&
    isText(value.application_name) &&
    isText(value.baseline_version) &&
    isText(value.candidate_version) &&
    isNullableHash(value.baseline_artifact_sha256) &&
    isNullableHash(value.candidate_artifact_sha256) &&
    isNullableHash(value.comparison_profile_sha256);
}
function isSourceReport(value) {
  return isRecord(value) && isHash(value.report_id) &&
    ["complete", "partial"].includes(value.status) &&
    isCount(value.added_count) && isCount(value.removed_count) &&
    isCount(value.changed_count) && isCount(value.uncertainty_count);
}
function isPatternRef(value) {
  return isRecord(value) && isHash(value.report_id) &&
    Array.isArray(value.finding_ids) && value.finding_ids.length > 0 &&
    value.finding_ids.every(isHash) &&
    new Set(value.finding_ids).size === value.finding_ids.length;
}
function isPattern(value) {
  return isRecord(value) && isHash(value.fingerprint) &&
    ["DG-COMPARE-001", "DG-COMPARE-002"].includes(value.rule_id) &&
    ["added", "removed", "changed"].includes(value.change_type) &&
    CATEGORIES.includes(value.category) &&
    isText(value.summary, 1024) && isText(value.observation, 2048) &&
    isCount(value.occurrence_count) && value.occurrence_count > 0 &&
    ["recurring", "not_consistently_observed"].includes(value.repeatability) &&
    Array.isArray(value.report_refs) && value.report_refs.length > 0 &&
    value.report_refs.every(isPatternRef);
}
function isSummary(value) {
  return isRecord(value) && isCount(value.recurring_pattern_count) &&
    isCount(value.not_consistently_observed_pattern_count) &&
    isCount(value.partial_report_count);
}

/**
 * Browser-side defensive checks for repeat-analysis-v1.
 * Python remains authoritative for schema validation and the content-derived ID.
 * @param {unknown} value
 * @returns {{ok: true, analysis: import("../types").RepeatAnalysisV1} | {ok: false, message: string}}
 */
export function validateRepeatAnalysisV1(value) {
  if (!isRecord(value)) return { ok: false, message: "The file must contain a JSON object." };
  if (value.schema_version !== 1) return { ok: false, message: "Unsupported repeat-analysis schema version." };
  if (!isHash(value.analysis_id)) return { ok: false, message: "The repeat-analysis ID is missing or malformed." };
  if (!isCohort(value.cohort)) return { ok: false, message: "The repeat-analysis cohort metadata is malformed." };
  if (!isCount(value.report_count) || value.report_count < 2 ||
      !Array.isArray(value.reports) || value.reports.length < 2 ||
      value.reports.length > 500 || value.reports.length !== value.report_count ||
      !value.reports.every(isSourceReport)) {
    return { ok: false, message: "The source-report list or report count is malformed." };
  }

  const reportIds = value.reports.map(report => report.report_id);
  const reportIdSet = new Set(reportIds);
  if (reportIdSet.size !== reportIds.length) {
    return { ok: false, message: "The repeat analysis contains duplicate source report IDs." };
  }
  if (!Array.isArray(value.partial_report_ids) ||
      !value.partial_report_ids.every(isHash) ||
      new Set(value.partial_report_ids).size !== value.partial_report_ids.length) {
    return { ok: false, message: "The partial source-report ID list is malformed." };
  }
  const expectedPartialIds = value.reports
    .filter(report => report.status === "partial")
    .map(report => report.report_id)
    .sort();
  const actualPartialIds = [...value.partial_report_ids].sort();
  if (expectedPartialIds.length !== actualPartialIds.length ||
      expectedPartialIds.some((id, index) => id !== actualPartialIds[index])) {
    return { ok: false, message: "Partial source-report IDs do not match the reported source statuses." };
  }
  if (!Array.isArray(value.patterns) || !value.patterns.every(isPattern)) {
    return { ok: false, message: "One or more repeat patterns are malformed." };
  }
  const fingerprints = value.patterns.map(pattern => pattern.fingerprint);
  if (new Set(fingerprints).size !== fingerprints.length) {
    return { ok: false, message: "The repeat analysis contains duplicate pattern fingerprints." };
  }

  for (const pattern of value.patterns) {
    const referenceIds = pattern.report_refs.map(reference => reference.report_id);
    if (new Set(referenceIds).size !== referenceIds.length) {
      return { ok: false, message: "A pattern references one source report more than once." };
    }
    if (referenceIds.some(id => !reportIdSet.has(id))) {
      return { ok: false, message: "A pattern references a report outside this analysis." };
    }
    if (pattern.occurrence_count !== referenceIds.length) {
      return { ok: false, message: "A pattern occurrence count does not match its source references." };
    }
    const expected = pattern.occurrence_count === value.report_count
      ? "recurring"
      : "not_consistently_observed";
    if (pattern.repeatability !== expected) {
      return { ok: false, message: "A pattern repeatability label does not match its occurrence count." };
    }
  }

  if (!isSummary(value.summary)) return { ok: false, message: "The repeat-analysis summary counts are malformed." };
  const recurring = value.patterns.filter(pattern => pattern.repeatability === "recurring").length;
  const inconsistent = value.patterns.filter(
    pattern => pattern.repeatability === "not_consistently_observed"
  ).length;
  if (value.summary.recurring_pattern_count !== recurring ||
      value.summary.not_consistently_observed_pattern_count !== inconsistent ||
      value.summary.partial_report_count !== expectedPartialIds.length) {
    return { ok: false, message: "Repeat-analysis summary counts do not match the report and pattern lists." };
  }

  return { ok: true, analysis: /** @type {import("../types").RepeatAnalysisV1} */ (value) };
}
