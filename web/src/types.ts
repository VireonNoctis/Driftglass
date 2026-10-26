export type ChangeType = "added" | "removed" | "changed";
export type FindingCategory = "process" | "network" | "file" | "system";

export interface ProcessIdentity {
  pid: number;
  parent_pid: number;
  executable: string;
  start_time_ticks?: number;
}

export interface Evidence {
  run_id: string;
  event_id: string;
  collector: string;
  source_event_ref: string;
  timestamp: string;
  observed: {
    category: string;
    action: string;
    process_executable: string | null;
    process_identity?: ProcessIdentity;
    resource: Record<string, unknown> | null;
    outcome: string | null;
    attributes: Record<string, unknown>;
  };
}

export interface Finding {
  finding_id: string;
  rule_id: string;
  change_type: ChangeType;
  category: FindingCategory;
  summary: string;
  observation: string;
  baseline_evidence: Evidence[];
  candidate_evidence: Evidence[];
  interpretation: string;
}

export interface RunSummary {
  run_id: string;
  version: string;
  artifact_sha256: string | null;
  capture_status: "running" | "completed" | "incomplete" | "failed";
}

export type CoverageCategory = "process" | "network" | "file" | "system" | "persistence";
export type CoverageEventCategory = "process" | "network" | "file" | "system" | "coverage";
export type EventSourceState = "active" | "inactive" | "failed" | "unavailable" | "not_configured";
export interface CoverageSource {
  name: string;
  state: EventSourceState;
  categories: CoverageCategory[];
}
export interface RunCoverage {
  event_sources: CoverageSource[];
  unavailable_categories: CoverageCategory[];
  dropped_events: number;
  errors: string[];
  recorded_event_counts: Record<CoverageEventCategory, number>;
}
export interface ReportCoverage {
  baseline: RunCoverage;
  candidate: RunCoverage;
}
export interface ReportV1 {
  schema_version: 1;
  report_id: string;
  status: "complete" | "partial";
  experiment_id: string;
  application_name: string;
  baseline: RunSummary;
  candidate: RunSummary;
  coverage?: ReportCoverage;
  environment_differences: Array<{
    field: string;
    baseline_value: string | null;
    candidate_value: string | null;
  }>;
  findings: Finding[];
  uncertainties: Array<{ code: string; message: string; run_id: string | null }>;
  summary: {
    baseline_event_count: number;
    candidate_event_count: number;
    baseline_behaviour_event_count: number;
    candidate_behaviour_event_count: number;
    added_count: number;
    removed_count: number;
    changed_count: number;
    uncertainty_count: number;
  };
}
