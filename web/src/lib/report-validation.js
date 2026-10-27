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

const COVERAGE_CATEGORIES = ["process", "network", "file", "system", "persistence"];
const COVERAGE_UNCERTAINTY_CODES = new Set([
  "capture_not_completed",
  "categories_unavailable",
  "event_sources_not_active",
  "events_dropped",
  "collection_errors",
  "coverage_not_declared",
  "artifact_hash_missing"
]);
/** @param {unknown} value @returns {boolean} */
function isRun(value) {
  return isRecord(value) &&
    isText(value.run_id) &&
    isText(value.version) &&
    (value.artifact_sha256 === null ||
      (typeof value.artifact_sha256 === "string" && /^[a-f0-9]{64}$/.test(value.artifact_sha256))) &&
    ["completed", "incomplete", "failed"].includes(value.capture_status);
}
/** @param {unknown} value @returns {boolean} */
function isProcessIdentity(value) {
  return isRecord(value) &&
    isCount(value.pid) &&
    isCount(value.parent_pid) &&
    typeof value.executable === "string" &&
    value.executable.length <= 4096 &&
    (value.start_time_ticks === undefined || isCount(value.start_time_ticks));
}
const RFC3339_DATE_TIME = /^(\d{4})-(\d{2})-(\d{2})[Tt](?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d(?:\.\d+)?(?:[Zz]|[+-](?:[01]\d|2[0-3]):[0-5]\d)$/;

/** @param {unknown} value @returns {boolean} */
function isDateTime(value) {
  if (typeof value !== "string" || !RFC3339_DATE_TIME.test(value)) return false;
  const normalizedValue = value.replace("t", "T").replace(/z$/, "Z");
  if (!Number.isFinite(Date.parse(normalizedValue))) return false;

  // Date.parse can normalize impossible calendar dates, so verify the written
  // date separately before trusting the parsed timestamp.
  const datePart = value.slice(0, 10);
  const date = new Date(`${datePart}T00:00:00Z`);
  const [year, month, day] = datePart.split("-").map(Number);
  return Number.isFinite(date.getTime()) &&
    date.getUTCFullYear() === year &&
    date.getUTCMonth() + 1 === month &&
    date.getUTCDate() === day;
}

/** @param {unknown} value @returns {boolean} */
function isEvidence(value) {
  return isRecord(value) &&
    isText(value.run_id) && isText(value.event_id) &&
    isText(value.collector) && isText(value.source_event_ref) &&
    isDateTime(value.timestamp) &&
    isRecord(value.observed) &&
    isText(value.observed.category) && isText(value.observed.action) &&
    isNullableText(value.observed.process_executable) &&
    (value.observed.resource === null || isRecord(value.observed.resource)) &&
    isNullableText(value.observed.outcome) && isRecord(value.observed.attributes) &&
    (value.observed.process_identity === undefined || isProcessIdentity(value.observed.process_identity));
}
/** @param {unknown} value @returns {boolean} */
function isCoverageSource(value) {
  return isRecord(value) &&
    isText(value.name) &&
    ["active", "inactive", "failed", "unavailable", "not_configured"].includes(value.state) &&
    Array.isArray(value.categories) &&
    value.categories.every(category => COVERAGE_CATEGORIES.includes(category)) &&
    new Set(value.categories).size === value.categories.length;
}
/** @param {unknown} value @returns {boolean} */
function isRunCoverage(value) {
  if (!isRecord(value) ||
      !Array.isArray(value.event_sources) || !value.event_sources.every(isCoverageSource) ||
      !Array.isArray(value.unavailable_categories) ||
      !value.unavailable_categories.every(category => COVERAGE_CATEGORIES.includes(category)) ||
      new Set(value.unavailable_categories).size !== value.unavailable_categories.length ||
      !isCount(value.dropped_events) ||
      !Array.isArray(value.errors) || !value.errors.every(isText) ||
      !isRecord(value.recorded_event_counts)) return false;

  const sourceNames = value.event_sources.map(source => source.name);
  if (new Set(sourceNames).size !== sourceNames.length) return false;

  // Mirror the manifest invariant: a source declared inactive/failed/unavailable
  // must make each of its categories explicit in the run's coverage gaps.
  const unavailable = new Set(value.unavailable_categories);
  if (!value.event_sources.every(source =>
    source.state === "active" || source.categories.every((/** @type {string} */ category) => unavailable.has(category))
  )) return false;

  const categories = ["process", "network", "file", "system", "coverage"];
  return Object.keys(value.recorded_event_counts).length === categories.length &&
    categories.every(category => isCount(value.recorded_event_counts[category]));
}
/** @param {unknown} value @returns {boolean} */
function isReportCoverage(value) {
  return isRecord(value) && isRunCoverage(value.baseline) && isRunCoverage(value.candidate);
}

/**
 * @param {string} role
 * @param {Record<string, any>} run
 * @param {Record<string, any>} coverage
 * @param {Record<string, any>[]} uncertainties
 * @returns {string | null}
 */
function coverageUncertaintyMismatch(role, run, coverage, uncertainties) {
  const unavailable = new Set(coverage.unavailable_categories);
  const activeCategories = new Set();
  for (const source of coverage.event_sources) {
    if (source.state === "active") {
      for (const category of source.categories) activeCategories.add(category);
    }
  }
  const undeclared = COVERAGE_CATEGORIES.filter(category =>
    !activeCategories.has(category) && !unavailable.has(category)
  );
  const expected = new Set();
  if (run.capture_status !== "completed") expected.add("capture_not_completed");
  if (coverage.unavailable_categories.length > 0) expected.add("categories_unavailable");
  if (coverage.event_sources.some((/** @type {Record<string, any>} */ source) => source.state !== "active")) expected.add("event_sources_not_active");
  if (coverage.dropped_events > 0) expected.add("events_dropped");
  if (coverage.errors.length > 0) expected.add("collection_errors");
  if (undeclared.length > 0) expected.add("coverage_not_declared");
  if (run.artifact_sha256 === null) expected.add("artifact_hash_missing");

  const actual = new Set(uncertainties
    .filter(item => item.run_id === run.run_id && COVERAGE_UNCERTAINTY_CODES.has(item.code))
    .map(item => item.code));
  const missing = [...expected].filter(code => !actual.has(code)).sort();
  const unexpected = [...actual].filter(code => !expected.has(code)).sort();
  if (missing.length === 0 && unexpected.length === 0) return null;
  return `The ${role} coverage snapshot does not match uncertainty codes (missing: ${missing.join(", ") || "none"}; unexpected: ${unexpected.join(", ") || "none"}).`;
}

/** @param {unknown} value @returns {boolean} */
function isComparisonProfile(value) {
  return isRecord(value) &&
    typeof value.profile_id === "string" && /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(value.profile_id) &&
    typeof value.sha256 === "string" && /^[a-f0-9]{64}$/.test(value.sha256) &&
    Array.isArray(value.included_categories) && value.included_categories.length > 0 &&
    value.included_categories.every(category => ["process", "network", "file", "system"].includes(category)) &&
    new Set(value.included_categories).size === value.included_categories.length &&
    Array.isArray(value.ignored_attributes) && value.ignored_attributes.length <= 64 &&
    value.ignored_attributes.every(attribute => typeof attribute === "string" && /^[A-Za-z_][A-Za-z0-9_.-]{0,127}$/.test(attribute)) &&
    new Set(value.ignored_attributes).size === value.ignored_attributes.length;
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
  if (value.baseline.run_id === value.candidate.run_id) {
    return { ok: false, message: "Baseline and candidate must reference distinct runs." };
  }
  if (value.coverage !== undefined && !isReportCoverage(value.coverage)) {
    return { ok: false, message: "The declared per-run coverage snapshot is malformed." };
  }
  if (value.comparison_profile !== undefined && !isComparisonProfile(value.comparison_profile)) {
    return { ok: false, message: "The comparison-profile metadata is malformed." };
  }
  if (!Array.isArray(value.environment_differences) || !value.environment_differences.every((item) =>
    isRecord(item) && isText(item.field) &&
    isNullableText(item.baseline_value) && isNullableText(item.candidate_value))) {
    return { ok: false, message: "Environment differences are malformed." };
  }
  if (!Array.isArray(value.findings) || !value.findings.every(isFinding)) {
    return { ok: false, message: "One or more findings are malformed or have invalid evidence links." };
  }
  const findingIds = new Set();
  for (const finding of value.findings) {
    if (findingIds.has(finding.finding_id)) {
      return { ok: false, message: "Finding IDs must be unique within a report." };
    }
    findingIds.add(finding.finding_id);
    for (const [key, runId] of [
      ["baseline_evidence", value.baseline.run_id],
      ["candidate_evidence", value.candidate.run_id]
    ]) {
      for (const evidence of finding[key]) {
        if (evidence.run_id !== runId) {
          return { ok: false, message: "Finding evidence references the wrong run." };
        }
        if (evidence.observed.category !== finding.category) {
          return { ok: false, message: "Finding evidence category does not match its finding." };
        }
      }
    }
  }
  if (!Array.isArray(value.uncertainties) || !value.uncertainties.every((item) =>
    isRecord(item) && isText(item.code) && isText(item.message) && isNullableText(item.run_id))) {
    return { ok: false, message: "One or more uncertainty records are malformed." };
  }
  const knownRunIds = new Set([value.baseline.run_id, value.candidate.run_id]);
  if (value.uncertainties.some(item => item.run_id !== null && !knownRunIds.has(item.run_id))) {
    return { ok: false, message: "An uncertainty references a run outside this comparison." };
  }

  for (const [role, run] of [
    ["baseline", value.baseline],
    ["candidate", value.candidate]
  ]) {
    if (run.capture_status !== "completed" &&
        !value.uncertainties.some(item =>
          item.code === "capture_not_completed" && item.run_id === run.run_id
        )) {
      return {
        ok: false,
        message: `The ${role} capture status requires a matching capture_not_completed uncertainty.`
      };
    }
  }
  if (value.coverage !== undefined) {
    for (const [role, run] of [
      ["baseline", value.baseline],
      ["candidate", value.candidate]
    ]) {
      const mismatch = coverageUncertaintyMismatch(role, run, value.coverage[role], value.uncertainties);
      if (mismatch !== null) return { ok: false, message: mismatch };
    }
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
  if (value.coverage !== undefined) {
    for (const [role, runCoverage] of [
      ["baseline", value.coverage.baseline],
      ["candidate", value.coverage.candidate]
    ]) {
      const recordedCount = Object.values(runCoverage.recorded_event_counts)
        .reduce((total, count) => total + count, 0);
      if (recordedCount !== summary[`${role}_event_count`]) {
        return {
          ok: false,
          message: `The ${role} coverage snapshot record count does not match the report summary.`
        };
      }
    }
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
