/** @typedef {import("../types").ReportCoverage} ReportCoverage */
/** @typedef {import("../types").RunCoverage} RunCoverage */

const CATEGORIES = ["process", "network", "file", "system", "persistence"];

/**
 * @param {RunCoverage} coverage
 * @param {string} category
 */
function categoryDeclaration(coverage, category) {
  const sources = coverage.event_sources.filter(source => source.categories.includes(category));
  const active = sources.filter(source => source.state === "active");
  const other = sources.filter(source => source.state !== "active");
  const unavailable = coverage.unavailable_categories.includes(category);
  let status;
  if (unavailable && active.length > 0) status = "Conflicting declarations";
  else if (unavailable) status = "Declared unavailable";
  else if (active.length > 0 && other.length > 0) status = "Mixed source states";
  else if (active.length > 0) status = "Active source declared";
  else if (sources.length === 0) status = "Not declared";
  else {
    const states = [...new Set(sources.map(source => source.state))];
    status = states.length === 1
      ? ({
          inactive: "Inactive source",
          failed: "Source failed",
          unavailable: "Source unavailable",
          not_configured: "Not configured"
        }[states[0]] ?? "Unknown source state")
      : "Mixed source states";
  }

  return {
    status,
    source_names: sources.map(source => source.name),
    active_source_count: active.length,
    other_source_count: other.length,
    unavailable_declared: unavailable,
    record_count: Object.hasOwn(coverage.recorded_event_counts, category)
      ? coverage.recorded_event_counts[category]
      : null
  };
}

/**
 * @param {ReportCoverage | undefined} coverage
 * @returns {null | {rows: Array<{category:string,baseline:ReturnType<typeof categoryDeclaration>,candidate:ReturnType<typeof categoryDeclaration>}>, baseline:RunCoverage, candidate:RunCoverage}}
 */
export function buildCoverageMatrix(coverage) {
  if (!coverage) return null;
  return {
    rows: CATEGORIES.map(category => ({
      category,
      baseline: categoryDeclaration(coverage.baseline, category),
      candidate: categoryDeclaration(coverage.candidate, category)
    })),
    baseline: coverage.baseline,
    candidate: coverage.candidate
  };
}
