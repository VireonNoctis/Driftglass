export type ChangeType = "added" | "removed" | "changed";
export type FindingCategory = "process" | "network" | "file" | "system";

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
    resource: Record<string, unknown> | null;
    outcome: string | null;
    attributes: Record<string, string | number | boolean | null>;
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

export interface ReportV1 {
  schema_version: 1;
  report_id: string;
  status: "complete" | "partial";
  experiment_id: string;
  application_name: string;
  baseline: RunSummary;
  candidate: RunSummary;
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
