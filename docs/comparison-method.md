# Deterministic comparison method v1

The comparison prototype consumes two already-captured inputs: a baseline run manifest and JSONL event stream, and a candidate manifest and stream. It validates inputs with the event and run-manifest contracts before comparison. It does not launch applications, collect OS events, persist results, or export files.

## Matching rules

- Coverage-category records are not compared as application behaviour. They feed the uncertainty section.
- The comparison identity uses event category, action, process executable name, resource kind, and protocol. Resource identifiers distinguish resources for non-network events. The current Linux procfs observer places the local socket address and source port in network `resource.identifier`; that field is retained in evidence but excluded from network matching identity and detail signatures because local ports can change between equivalent runs. Remote network address and port remain in normalized details, so a destination change still produces a changed finding. Run-specific event IDs, timestamps, PIDs, and source references remain evidence pointers but do not determine whether behaviours match.
- Within a matching identity, equal normalized details are multiset-matched. Remaining records are paired as changed details; excess baseline records become removed observations, and excess candidate records become added observations.
- Normalized detail comparison includes action, process executable name, resource details, outcome, and attributes. This can identify changed destination or state metadata without requiring run-local PIDs to match.
- `process_start_time_ticks` is excluded from behavior details because it identifies a process instance within a run, not application behavior. Evidence records keep PID, parent PID, executable, and available start ticks separately as optional `observed.process_identity` metadata.
- Finding IDs and report IDs are SHA-256 hashes of canonical JSON payloads. No wall-clock generation timestamp is included, so equal inputs produce equal report content.

These rules are a first documented baseline, not a universal semantic equivalence algorithm. Different applications can produce similar names and metadata, events may be absent due to sampling, and broad identity groups can produce changed pairings that need human review.

### Synthetic noise regression scope

The comparison tests cover equivalent observations whose timestamps, event IDs, source references, process IDs/start ticks, local TCP source ports, and ordering vary, plus a labelled remote-destination change that must remain visible. These fixtures provide deterministic regression cases, not an empirical false-positive rate: they do not measure real-world observer coverage, workload variance, or the frequency of benign changes.

## Input compatibility and uncertainty

The loader refuses runs with incorrect baseline/candidate roles, different experiment IDs, reused run IDs, mismatched application names, conflicting non-empty package IDs, or active captures. Different OS/build/architecture/snapshot, scenario, or capture profile produces a visible context difference and makes the report partial.

Reports also become partial when a manifest marks a capture incomplete/failed, reports unavailable categories or inactive sources, records errors or dropped events, does not declare coverage for a relevant category, contains reduced-quality events, or identifies a missing artifact hash. Coverage event records are summarized as uncertainty, not misreported as application activity.

A report status of complete means only that the validated input manifests and records did not declare any of these gaps. It does not mean every application action was observed and never means the application is safe.

## Findings

Every finding names a deterministic rule ID, change type, observation, cautious interpretation, and source-linked evidence from the baseline and/or candidate. Added and removed mean a record was present in one input but not matched in the other; removed does not prove the behaviour ceased. Changed means records with the same matching identity had different normalized details. No severity score or maliciousness verdict is inferred.

The report v1 contract lives in the schemas/reports/report-v1.schema.json file. The implementation remains experimental until its tests and review pass on the feature branch.


## Versioned comparison profiles

Every newly generated report records comparison_profile metadata containing the profile ID, normalized category selection, ignored attribute names, and a digest of the complete v1 profile configuration. The built-in default-v1 profile compares all supported behaviour categories and ignores no custom attributes. User profiles are strict JSON validated by schemas/rules/rule-profile-v1.schema.json.

A profile can exclude event categories from finding generation or omit selected attribute keys from normalized matching signatures. These settings do not suppress coverage uncertainties. Ignored attribute values remain present in the evidence payload attached to a finding; the setting changes matching, not evidence collection or privacy redaction. Review the profile digest and evidence before interpreting a finding.

## Repeat-run analysis

Repeat analysis consumes two or more previously exported comparison reports. It requires the same application, baseline/candidate versions and hashes, and comparison-profile digest. A stable pattern fingerprint uses rule, change type, category, and normalized observed evidence; process identity and run-specific start ticks are excluded. Patterns observed in every source report are labelled recurring. Others are labelled not consistently observed, not “false positive.”

The repeat-analysis-v1 output records source report IDs, source finding IDs, cohort information, pattern counts, and partial-report IDs. Partial or differing-coverage source reports remain visible; a pattern missing from a report can be caused by collection gaps or run variability. The aggregate is a review aid, not a safety verdict.
