import test from "node:test";
import assert from "node:assert/strict";
import { validateRepeatAnalysisV1 } from "./repeat-analysis-validation.js";

const hash = letter => letter.repeat(64);

function validAnalysis() {
  const reports = [
    { report_id: hash("a"), status: "complete", added_count: 1, removed_count: 0, changed_count: 0, uncertainty_count: 0 },
    { report_id: hash("b"), status: "complete", added_count: 1, removed_count: 0, changed_count: 0, uncertainty_count: 0 },
    { report_id: hash("c"), status: "partial", added_count: 0, removed_count: 0, changed_count: 0, uncertainty_count: 1 }
  ];
  return {
    schema_version: 1,
    analysis_id: hash("4"),
    cohort: {
      application_name: "Synthetic App",
      baseline_version: "1.0",
      candidate_version: "2.0",
      baseline_artifact_sha256: hash("5"),
      candidate_artifact_sha256: hash("6"),
      comparison_profile_sha256: null
    },
    report_count: 3,
    reports,
    partial_report_ids: [hash("c")],
    patterns: [
      {
        fingerprint: hash("d"),
        rule_id: "DG-COMPARE-001",
        change_type: "added",
        category: "network",
        summary: "Candidate network observation",
        observation: "A synthetic new connection was observed.",
        occurrence_count: 3,
        repeatability: "recurring",
        report_refs: reports.map((source, index) => ({
          report_id: source.report_id,
          finding_ids: [hash(["e", "f", "0"][index])]
        }))
      },
      {
        fingerprint: hash("8"),
        rule_id: "DG-COMPARE-002",
        change_type: "changed",
        category: "process",
        summary: "Candidate process metadata changed",
        observation: "A synthetic attribute differed in two runs.",
        occurrence_count: 2,
        repeatability: "not_consistently_observed",
        report_refs: reports.slice(0, 2).map((source, index) => ({
          report_id: source.report_id,
          finding_ids: [hash(["9", "0"][index])]
        }))
      }
    ],
    summary: {
      recurring_pattern_count: 1,
      not_consistently_observed_pattern_count: 1,
      partial_report_count: 1
    }
  };
}

test("accepts a valid repeat-analysis-v1 result with recurring and inconsistent patterns", () => {
  const result = validateRepeatAnalysisV1(validAnalysis());
  assert.equal(result.ok, true);
  if (result.ok) {
    assert.equal(result.analysis.report_count, 3);
    assert.equal(result.analysis.patterns[0].repeatability, "recurring");
  }
});

test("rejects summary counts that do not match the patterns", () => {
  const value = validAnalysis();
  value.summary.recurring_pattern_count = 2;
  const result = validateRepeatAnalysisV1(value);
  assert.equal(result.ok, false);
  assert.match(result.message, /summary counts do not match/);
});

test("rejects a recurring label when not every input report is referenced", () => {
  const value = validAnalysis();
  value.patterns[0].report_refs.pop();
  value.patterns[0].occurrence_count = 2;
  const result = validateRepeatAnalysisV1(value);
  assert.equal(result.ok, false);
  assert.match(result.message, /repeatability label does not match/);
});

test("rejects references to reports outside the analysis", () => {
  const value = validAnalysis();
  value.patterns[0].report_refs[0].report_id = hash("1");
  const result = validateRepeatAnalysisV1(value);
  assert.equal(result.ok, false);
  assert.match(result.message, /outside this analysis/);
});

test("rejects inconsistent partial report IDs", () => {
  const value = validAnalysis();
  value.partial_report_ids = [];
  const result = validateRepeatAnalysisV1(value);
  assert.equal(result.ok, false);
  assert.match(result.message, /Partial source-report IDs/);
});

test("rejects duplicate source report IDs", () => {
  const value = validAnalysis();
  value.reports[1].report_id = value.reports[0].report_id;
  const result = validateRepeatAnalysisV1(value);
  assert.equal(result.ok, false);
  assert.match(result.message, /duplicate source report IDs/);
});
