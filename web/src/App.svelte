<script lang="ts">
  import { validateReportV1 } from "./lib/report-validation.js";
  import {
    hasNonFiniteJsonNumber,
    MAX_JSON_NESTING_DEPTH,
    validateJsonInputSource
  } from "./lib/json-input-validation.js";
  import {
    validateRepeatAnalysisV1,
    verifyRepeatAnalysisContentId
  } from "./lib/repeat-analysis-validation.js";
  import { buildEvidenceTimeline } from "./lib/evidence-timeline.js";
  import { buildCoverageMatrix } from "./lib/coverage-matrix.js";
  import { buildProcessTree } from "./lib/process-tree.js";
  import type { Finding, ReportV1, RepeatAnalysisV1 } from "./types";

  const MAX_FILE_BYTES = 20 * 1024 * 1024;
  let report: ReportV1 | null = $state(null);
  let repeatAnalysis: RepeatAnalysisV1 | null = $state(null);
  let errorMessage = $state("");
  let fileName = $state("");
  let query = $state("");
  let categoryFilter = $state("all");
  let changeFilter = $state("all");
  let dragActive = $state(false);
  let loading = $state(false);
  let evidenceView = $state<"findings" | "timeline" | "process-tree">("findings");
  let fileInput = $state<HTMLInputElement | undefined>(undefined);

  let timelineItems = $derived.by(() => buildEvidenceTimeline(visibleFindings));
  let processTree = $derived.by(() => buildProcessTree(timelineItems));
  let coverageMatrix = $derived.by(() => buildCoverageMatrix(report?.coverage));

  let visibleFindings = $derived.by(() => {
    if (!report) return [];
    const needle = query.trim().toLocaleLowerCase();
    return report.findings.filter((finding) => {
      if (categoryFilter !== "all" && finding.category !== categoryFilter) return false;
      if (changeFilter !== "all" && finding.change_type !== changeFilter) return false;
      if (!needle) return true;
      const evidence = [...finding.baseline_evidence, ...finding.candidate_evidence]
        .map((item) => JSON.stringify(item.observed)).join(" ");
      return [finding.summary, finding.rule_id, finding.category, finding.observation,
        finding.interpretation, evidence].join(" ").toLocaleLowerCase().includes(needle);
    });
  });

  async function loadFile(file: File) {
    if (loading) {
      errorMessage = "A report is already being read. Wait for validation to finish.";
      return;
    }
    errorMessage = "";
    loading = true;
    try {
      if (file.size > MAX_FILE_BYTES) {
        errorMessage = "File exceeds the 20 MiB limit. Export a smaller report and try again.";
        return;
      }
      if (!file.name.toLocaleLowerCase().endsWith(".json") && file.type !== "application/json") {
        errorMessage = "Choose a JSON report exported by Driftglass.";
        return;
      }
      const source = await file.text();
      const sourceError = validateJsonInputSource(source);
      if (sourceError === "nesting-limit") {
        errorMessage = `The selected JSON exceeds the maximum nesting depth of ${MAX_JSON_NESTING_DEPTH}.`;
        return;
      }
      if (sourceError === "duplicate-keys") {
        errorMessage = "The selected JSON contains duplicate object keys.";
        return;
      }
      if (sourceError === "unsafe-integer") {
        errorMessage = "The selected JSON contains an integer outside the browser's safe integer range.";
        return;
      }
      const parsed: unknown = JSON.parse(source);
      if (hasNonFiniteJsonNumber(parsed)) {
        errorMessage = "The selected JSON contains a numeric value outside the browser's finite range.";
        return;
      }
      if (isRepeatAnalysisInput(parsed)) {
        const result = validateRepeatAnalysisV1(parsed);
        if (!result.ok) {
          errorMessage = result.message;
          return;
        }
        const integrity = await verifyRepeatAnalysisContentId(result.analysis);
        if (!integrity.ok) {
          errorMessage = integrity.message;
          return;
        }
        repeatAnalysis = result.analysis;
        report = null;
      } else {
        const result = validateReportV1(parsed);
        if (!result.ok) {
          errorMessage = result.message;
          return;
        }
        report = result.report;
        repeatAnalysis = null;
      }
      evidenceView = "findings";
      fileName = file.name;
      query = "";
      categoryFilter = "all";
      changeFilter = "all";
    } catch (error) {
      errorMessage = error instanceof SyntaxError ? "The selected file is not valid JSON." : "The selected file could not be read.";
    } finally {
      loading = false;
    }
  }

  function isRepeatAnalysisInput(value: unknown): boolean {
    return typeof value === "object" && value !== null &&
      !Array.isArray(value) && "analysis_id" in value;
  }

  function handleInputChange(event: Event) {
    const input = event.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    if (file) void loadFile(file);
    input.value = "";
  }
  function handleDrop(event: DragEvent) {
    event.preventDefault();
    dragActive = false;
    const file = event.dataTransfer?.files[0];
    if (file) void loadFile(file);
  }
  function resetReport() {
    report = null; repeatAnalysis = null; fileName = ""; errorMessage = ""; query = "";
    categoryFilter = "all"; changeFilter = "all"; evidenceView = "findings";
  }

  function resourceLabel(resource: Record<string, unknown> | null): string {
    if (!resource) return "No resource metadata";
    const parts = [
      typeof resource.kind === "string" ? resource.kind : "",
      typeof resource.identifier === "string" ? resource.identifier : "",
      typeof resource.address === "string" ? resource.address : "",
      typeof resource.port === "number" ? "port " + resource.port : "",
      typeof resource.protocol === "string" ? resource.protocol : "",
    ];
    return parts.filter(Boolean).join(" · ") || "Resource metadata present";
  }

  function formatCount(value: number) { return new Intl.NumberFormat("en").format(value); }
  function shortId(value: string) { return value.slice(0, 12); }
  function evidenceFor(finding: Finding) {
    return [
      ...finding.baseline_evidence.map((item) => ({ ...item, side: "baseline" })),
      ...finding.candidate_evidence.map((item) => ({ ...item, side: "candidate" }))
    ];
  }
</script>

<svelte:head>
  <title>Driftglass / Evidence Review</title>
  <meta name="description" content="Local report viewer for Driftglass comparison evidence." />
</svelte:head>

<div class="app-shell">
  <header class="topbar">
    <a class="wordmark" href="#top" aria-label="Driftglass home"><span class="mark" aria-hidden="true">D</span><span>DRIFTGLASS</span></a>
    <div class="topbar-right"><span class="eyebrow">LOCAL EVIDENCE VIEWER</span><span class="version">REPORT V1 / REPEAT V1</span></div>
  </header>

  <main id="top">
    <section class="intro">
      <div>
        <p class="eyebrow">OBSERVABLE BEHAVIOUR / REVIEW</p>
        <h1>Every update<br /><span>leaves a trace.</span></h1>
        <p class="lede">Compare what was observed. Inspect the evidence. Keep uncertainty visible.</p>
      </div>
      <div class="intro-stamp" aria-hidden="true"><span>DG</span><span>01 — LOCAL</span><span>NO UPLOAD</span></div>
    </section>

    {#if !report && !repeatAnalysis}
      <section
        class:drag-active={dragActive}
        class="dropzone"
        ondragover={(event) => { event.preventDefault(); dragActive = true; }}
        ondragleave={() => { dragActive = false; }}
        ondrop={handleDrop}
        aria-label="Load a report file"
      >
        <div class="drop-symbol" aria-hidden="true">↧</div>
        <p class="eyebrow">LOAD EVIDENCE REPORT</p>
        <h2>Select a report to inspect</h2>
        <p class="muted">Choose a Driftglass comparison report or repeat-analysis JSON export. The file stays in this browser tab; this viewer does not upload or persist it.</p>
        <button class="primary-button" type="button" disabled={loading} onclick={() => fileInput?.click()}>{loading ? "Reading file…" : "Choose JSON file"}</button>
        <input bind:this={fileInput} class="visually-hidden" type="file" accept=".json,application/json" disabled={loading} onchange={handleInputChange} />
        {#if loading}
          <p class="loading-message" role="status" aria-live="polite">Reading and validating the selected report…</p>
        {:else}
          <p class="micro">JSON only · 20 MiB maximum · report v1 or repeat-analysis v1</p>
        {/if}
        {#if errorMessage}<p class="error-message" role="alert">{errorMessage}</p>{/if}
      </section>
      <section class="privacy-note"><span aria-hidden="true">○</span><p><strong>Local-only review.</strong> Paths, process names, hostnames and evidence may be sensitive. Use this viewer on a trusted machine.</p></section>
    {:else if report}
      <section class="report-heading">
        <div>
          <p class="eyebrow">LOADED REPORT / {fileName}</p>
          <h2>{report.application_name}</h2>
          <p class="muted">Experiment <code>{report.experiment_id}</code> · Report <code>{shortId(report.report_id)}</code></p>
          <p class="micro muted">Browser checks report structure and evidence links, but does not recompute the report ID. For content-integrity validation, run <code>python -m driftglass report validate ./comparison.json</code> on the file locally.</p>
          {#if report.comparison_profile}
            <div class="profile-strip">
              <span class="eyebrow">COMPARISON PROFILE</span>
              <strong>{report.comparison_profile.profile_id}</strong>
              <code>SHA-256 {shortId(report.comparison_profile.sha256)}</code>
              <p>Included: {report.comparison_profile.included_categories.join(", ")} · Ignored for matching: {report.comparison_profile.ignored_attributes.join(", ") || "none"}</p>
            </div>
          {:else}
            <p class="micro muted">Comparison profile was not recorded in this older report.</p>
          {/if}
        </div>
        <div class="heading-actions"><span class:partial={report.status === "partial"} class="status">{report.status.toUpperCase()}</span><button class="quiet-button" type="button" onclick={resetReport}>Close report</button></div>
      </section>

      <section class="run-grid" aria-label="Baseline and candidate summaries">
        {#each [{name: "BASELINE", run: report.baseline, count: report.summary.baseline_event_count}, {name: "CANDIDATE", run: report.candidate, count: report.summary.candidate_event_count}] as item, i}
          <article class:candidate-panel={i === 1} class="run-panel">
            <div class="panel-label"><span class="dot"></span>{item.name}</div>
            <div class="run-version">{item.run.version}</div>
            <code>{item.run.run_id}</code>
            <div class="run-meta"><span>Capture: {item.run.capture_status}</span><span>Events: {formatCount(item.count)}</span></div>
            {#if item.run.artifact_sha256}
              <details><summary>Artifact SHA-256</summary><code class="hash">{item.run.artifact_sha256}</code></details>
            {:else}
              <p class="micro warning-text">Artifact hash unavailable</p>
            {/if}
          </article>
          {#if i === 0}<div class="comparison-divider" aria-hidden="true"><span>VS</span></div>{/if}
        {/each}
      </section>

      <section class="count-grid" aria-label="Finding counts">
        <div><span>ADDED</span><strong>{formatCount(report.summary.added_count)}</strong></div>
        <div><span>REMOVED</span><strong>{formatCount(report.summary.removed_count)}</strong></div>
        <div><span>CHANGED</span><strong>{formatCount(report.summary.changed_count)}</strong></div>
        <div><span>UNCERTAINTIES</span><strong>{formatCount(report.summary.uncertainty_count)}</strong></div>
      </section>

      {#if report.environment_differences.length > 0}
        <section class="section-block">
          <div class="section-heading"><div><p class="eyebrow">CONTEXT</p><h3>Environment differences</h3></div><span class="count">{report.environment_differences.length} FIELDS</span></div>
          <div class="difference-list">
            {#each report.environment_differences as difference (difference.field)}
              <div class="difference-row"><code>{difference.field}</code><div><span class="micro">BASELINE</span><p>{difference.baseline_value ?? "—"}</p></div><div><span class="micro">CANDIDATE</span><p>{difference.candidate_value ?? "—"}</p></div></div>
            {/each}
          </div>
        </section>
      {/if}

      <section class="section-block findings-section">
        <div class="section-heading"><div><p class="eyebrow">OBSERVATIONS / EVIDENCE LINKS</p><h3>Findings</h3></div><span class="count">{visibleFindings.length} / {report.findings.length}</span></div>
        <div class="filters">
          <label class="search-label"><span class="micro">SEARCH REPORT</span><input type="search" bind:value={query} placeholder="Search summaries, evidence, rule IDs…" /></label>
          <label><span class="micro">CATEGORY</span><select bind:value={categoryFilter}><option value="all">All categories</option><option value="process">Process</option><option value="network">Network</option><option value="file">File</option><option value="system">System</option></select></label>
          <label><span class="micro">CHANGE</span><select bind:value={changeFilter}><option value="all">All changes</option><option value="added">Added</option><option value="removed">Removed</option><option value="changed">Changed</option></select></label>
        </div>
        <div class="evidence-view-switch" role="group" aria-label="Evidence display">
          <button type="button" aria-pressed={evidenceView === "findings"} onclick={() => evidenceView = "findings"}>Findings</button>
          <button type="button" aria-pressed={evidenceView === "timeline"} onclick={() => evidenceView = "timeline"}>Evidence timeline <span>{formatCount(timelineItems.length)}</span></button>
          <button type="button" aria-pressed={evidenceView === "process-tree"} onclick={() => evidenceView = "process-tree"}>Process tree <span>{formatCount(processTree.node_count)}</span></button>
        </div>
        {#if evidenceView === "timeline"}
          {#if timelineItems.length === 0}
            <div class="empty-state"><p class="eyebrow">NO FINDING EVIDENCE</p><p>No evidence referenced by the current finding filters is available for the timeline.</p></div>
          {:else}
            <p class="timeline-note">This timeline contains only events referenced by findings in this report—not the complete raw event stream. Unchanged events and standalone coverage records may not appear.</p>
            <ol class="evidence-timeline" aria-label="Finding evidence timeline">
              {#each timelineItems as item (item.evidence.run_id + ":" + item.evidence.event_id)}
                <li class="timeline-event">
                  <span class="timeline-point" aria-hidden="true"></span>
                  <article class="timeline-entry">
                    <div class="timeline-topline"><span class="side-label">{item.side.toUpperCase()}</span><code>{item.evidence.timestamp}</code><code>{item.evidence.event_id}</code></div>
                    <p class="timeline-action"><strong>{item.evidence.observed.action}</strong> · {item.evidence.observed.category}</p>
                    <div class="timeline-meta">
                      <span>{item.evidence.observed.process_executable ?? "Process metadata unavailable"}</span>
                      <span>{resourceLabel(item.evidence.observed.resource)}</span>
                    </div>
                    <p class="timeline-finding">{item.finding.summary}</p>
                    <details><summary>Inspect observed fields</summary><pre>{JSON.stringify(item.evidence.observed, null, 2)}</pre></details>
                  </article>
                </li>
              {/each}
            </ol>
          {/if}
        {:else if evidenceView === "process-tree"}
          <p class="timeline-note">This partial relationship view includes only process identities referenced by the current finding filters and carrying a Linux procfs start-time tick. A parent link is drawn only when the same run contains that parent PID with an earlier start tick. It is not a complete process tree.</p>
          {#if processTree.skipped_evidence_count > 0}
            <p class="timeline-note">{formatCount(processTree.skipped_evidence_count)} evidence reference(s) have no validated process identity with a start-time tick and are omitted from the tree. Older reports remain viewable.</p>
          {/if}
          {#if processTree.groups.length === 0}
            <div class="empty-state"><p class="eyebrow">PROCESS IDENTITY UNAVAILABLE</p><p>The visible finding evidence does not contain enough validated PID, parent PID, executable, and start-time metadata to build process relationships.</p></div>
          {:else}
            <div class="process-tree-groups">
              {#each processTree.groups as group (group.run_id + ":" + group.side)}
                <section class="process-tree-group">
                  <div class="process-tree-heading">
                    <span class="side-label">{group.side.toUpperCase()}</span>
                    <code>{group.run_id}</code>
                    <span class="count">{formatCount(group.node_count)} PROCESSES</span>
                  </div>
                  <ol class="process-tree-list" aria-label={group.side + " process relationships for " + group.run_id}>
                    {#each group.rows as node (node.key)}
                      <li class="process-tree-row" style={"padding-left: " + (12 + node.depth * 20) + "px"}>
                        <span class="tree-guide" aria-hidden="true">{node.depth > 0 ? "↳" : "●"}</span>
                        <div class="process-tree-body">
                          <div class="process-tree-main"><strong>{node.executable || "Executable unavailable"}</strong><code>PID {node.pid}</code><code>PPID {node.parent_pid}</code></div>
                          <p class="process-tree-meta">Procfs start tick {node.start_time_ticks} · {formatCount(node.event_count)} referenced event(s) · Last seen {node.last_seen}</p>
                          {#if node.parent_status === "not-in-evidence"}
                            <p class="process-tree-warning">Parent PID {node.parent_pid} has no earlier process identity in this run's visible finding evidence; no parent link was inferred.</p>
                          {:else if node.parent_status === "root" && node.parent_pid !== 0 && node.parent_pid !== node.pid}
                            <p class="process-tree-warning">No parent link was established from the evidence available.</p>
                          {/if}
                          {#if node.finding_summaries.length > 0}
                            <p class="process-tree-findings">{node.finding_summaries.join(" · ")}</p>
                          {/if}
                        </div>
                      </li>
                    {/each}
                  </ol>
                </section>
              {/each}
            </div>
          {/if}
        {:else if visibleFindings.length === 0}
          <div class="empty-state"><p class="eyebrow">NO MATCHES</p><p>No findings match these filters. The report contains {report.findings.length} finding(s) total.</p><button class="quiet-button" type="button" onclick={() => { query = ""; categoryFilter = "all"; changeFilter = "all"; }}>Reset filters</button></div>
        {:else}
          <div class="finding-list">
            {#each visibleFindings as finding (finding.finding_id)}
              <article class="finding">
                <div class="finding-topline"><div class="tag-row"><span class="tag">{finding.change_type.toUpperCase()}</span><span class="tag secondary-tag">{finding.category.toUpperCase()}</span><code>{finding.rule_id}</code></div><code class="finding-id">{shortId(finding.finding_id)}</code></div>
                <h4>{finding.summary}</h4><p>{finding.observation}</p>
                <details class="evidence-details">
                  <summary>Inspect evidence ({finding.baseline_evidence.length + finding.candidate_evidence.length})</summary>
                  <div class="evidence-list">
                    {#each evidenceFor(finding) as evidence, i (evidence.event_id + "-" + i)}
                      <div class="evidence-item">
                        <div class="evidence-header"><span class="side-label">{evidence.side.toUpperCase()}</span><code>{evidence.event_id}</code></div>
                        <p>{evidence.observed.action} · {evidence.observed.category}</p>
                        <div class="evidence-meta"><span>{evidence.collector}</span><span>{evidence.timestamp}</span><span>Source: <code>{evidence.source_event_ref}</code></span></div>
                        <pre>{JSON.stringify(evidence.observed, null, 2)}</pre>
                      </div>
                    {/each}
                  </div>
                </details>
                <details class="interpretation-details"><summary>Rule interpretation and limits</summary><p>{finding.interpretation}</p></details>
              </article>
            {/each}
          </div>
        {/if}
      </section>

      <section class="section-block coverage-section">
        <div class="section-heading">
          <div><p class="eyebrow">COLLECTOR DECLARATIONS / INPUT COUNTS</p><h3>Coverage matrix</h3></div>
          <span class="count">{report.coverage ? "REPORT V1 SNAPSHOT" : "LEGACY REPORT"}</span>
        </div>
        <p class="coverage-explainer">Declared source states, unavailable categories, and records received by the comparator are separate signals. A zero record count does not establish that no activity occurred. Source declarations do not prove that every relevant event was observed.</p>
        {#if !coverageMatrix}
          <p class="muted empty-uncertainty">This older report-v1 file does not contain a per-run coverage snapshot. Its declared uncertainty items remain below; coverage has not been inferred.</p>
        {:else}
          <div class="coverage-table-wrap">
            <table class="coverage-table">
              <thead><tr><th scope="col">Category</th><th scope="col">Baseline declaration</th><th scope="col">Candidate declaration</th><th scope="col">Input records (B / C)</th></tr></thead>
              <tbody>
                {#each coverageMatrix.rows as row (row.category)}
                  <tr>
                    <th scope="row"><code>{row.category}</code></th>
                    <td>
                      <span class="coverage-state" class:coverage-positive={row.baseline.status === "Active source declared"} class:coverage-warning={row.baseline.status !== "Active source declared"}>{row.baseline.status}</span>
                      <p class="coverage-source-names">{row.baseline.source_names.length ? row.baseline.source_names.join(", ") : "No source declares this category"}</p>
                    </td>
                    <td>
                      <span class="coverage-state" class:coverage-positive={row.candidate.status === "Active source declared"} class:coverage-warning={row.candidate.status !== "Active source declared"}>{row.candidate.status}</span>
                      <p class="coverage-source-names">{row.candidate.source_names.length ? row.candidate.source_names.join(", ") : "No source declares this category"}</p>
                    </td>
                    <td class="coverage-counts">
                      <span>{row.baseline.record_count === null ? "Not represented" : formatCount(row.baseline.record_count)}</span>
                      <span>{row.candidate.record_count === null ? "Not represented" : formatCount(row.candidate.record_count)}</span>
                    </td>
                  </tr>
                {/each}
              </tbody>
            </table>
          </div>
          <div class="coverage-run-details">
            {#each [{label: "Baseline", run: coverageMatrix.baseline}, {label: "Candidate", run: coverageMatrix.candidate}] as item (item.label)}
              <details>
                <summary>{item.label} source declarations, dropped events, and errors</summary>
                <div class="coverage-detail-body">
                  <p><strong>Dropped-event count reported:</strong> {formatCount(item.run.dropped_events)}</p>
                  <p><strong>Declared unavailable categories:</strong> {item.run.unavailable_categories.length ? item.run.unavailable_categories.join(", ") : "None"}</p>
                  <h4>Event sources</h4>
                  {#if item.run.event_sources.length}
                    <ul>
                      {#each item.run.event_sources as source, i (source.name + ":" + source.state + ":" + i)}
                        <li><code>{source.name}</code> — {source.state}; categories: {source.categories.join(", ") || "none declared"}</li>
                      {/each}
                    </ul>
                  {:else}
                    <p>No event sources were declared.</p>
                  {/if}
                  <h4>Declared errors</h4>
                  {#if item.run.errors.length}
                    <ul>{#each item.run.errors as error, i (i + ":" + error)}<li>{error}</li>{/each}</ul>
                  {:else}
                    <p>No collection errors declared in the run manifest.</p>
                  {/if}
                </div>
              </details>
            {/each}
          </div>
        {/if}
      </section>

      <section class="section-block uncertainty-section">
        <div class="section-heading"><div><p class="eyebrow">COLLECTION LIMITS</p><h3>Uncertainty</h3></div><span class="count">{report.uncertainties.length} ITEMS</span></div>
        {#if report.uncertainties.length === 0}
          <p class="muted empty-uncertainty">No uncertainty was declared in the report inputs. This does not prove comprehensive collection or application safety.</p>
        {:else}
          <div class="uncertainty-list">
            {#each report.uncertainties as item, i (item.code + "-" + i)}
              <article><span class="tag warning-tag">{item.code}</span><p>{item.message}</p><code>{item.run_id ?? "Both runs / not run-specific"}</code></article>
            {/each}
          </div>
        {/if}
      </section>
      <footer class="report-footer"><p>Differences are observations, not verdicts. Missing events are not proof that an action did not occur.</p><button class="quiet-button" type="button" onclick={resetReport}>Close report</button></footer>
    {:else if repeatAnalysis}
      <section class="report-heading">
        <div>
          <p class="eyebrow">REPEAT ANALYSIS / {fileName}</p>
          <h2>{repeatAnalysis.cohort.application_name}</h2>
          <p class="muted">Baseline <code>{repeatAnalysis.cohort.baseline_version}</code> → Candidate <code>{repeatAnalysis.cohort.candidate_version}</code></p>
          <p class="micro muted">Analysis ID <code>{repeatAnalysis.analysis_id}</code></p>
        </div>
        <div class="heading-actions">
          <span class="count">{formatCount(repeatAnalysis.report_count)} REPORTS</span>
          <button class="quiet-button" type="button" onclick={resetReport}>Close analysis</button>
        </div>
      </section>

      <section class="repeat-summary-grid" aria-label="Repeat-analysis summary">
        <article class="repeat-summary-card">
          <p class="eyebrow">RECURRING</p>
          <strong>{formatCount(repeatAnalysis.summary.recurring_pattern_count)}</strong>
          <p>Patterns observed in every supplied report</p>
        </article>
        <article class="repeat-summary-card">
          <p class="eyebrow">NOT CONSISTENTLY OBSERVED</p>
          <strong>{formatCount(repeatAnalysis.summary.not_consistently_observed_pattern_count)}</strong>
          <p>Patterns present in only some supplied reports</p>
        </article>
        <article class="repeat-summary-card">
          <p class="eyebrow">PARTIAL INPUTS</p>
          <strong>{formatCount(repeatAnalysis.summary.partial_report_count)}</strong>
          <p>Source reports with declared uncertainty</p>
        </article>
      </section>

      <section class="section-block repeat-cohort">
        <div class="section-heading">
          <div><p class="eyebrow">COMPARISON COHORT</p><h3>Version and profile identity</h3></div>
          <span class="count">V1</span>
        </div>
        <div class="repeat-cohort-grid">
          <div><span>Application</span><strong>{repeatAnalysis.cohort.application_name}</strong></div>
          <div><span>Baseline artifact SHA-256</span><code>{repeatAnalysis.cohort.baseline_artifact_sha256 ?? "Not supplied"}</code></div>
          <div><span>Candidate artifact SHA-256</span><code>{repeatAnalysis.cohort.candidate_artifact_sha256 ?? "Not supplied"}</code></div>
          <div><span>Comparison profile SHA-256</span><code>{repeatAnalysis.cohort.comparison_profile_sha256 ?? "Not recorded in source reports"}</code></div>
        </div>
        <p class="timeline-note">This analysis groups reports by application, versions, artifact hashes, and comparison-profile fingerprint. It does not establish that every run used identical environment conditions or achieved comprehensive collection.</p>
      </section>

      {#if repeatAnalysis.partial_report_ids.length > 0}
        <section class="repeat-partial-notice" aria-labelledby="partial-repeat-heading">
          <p class="eyebrow">COLLECTION UNCERTAINTY</p>
          <h3 id="partial-repeat-heading">Partial source reports are included</h3>
          <p>Patterns can be absent from a partial run because collection gaps or run variability affected what was observed. Do not treat absence from these reports as proof that an activity did not occur.</p>
          <ul>
            {#each repeatAnalysis.partial_report_ids as reportId (reportId)}
              <li><code>{reportId}</code></li>
            {/each}
          </ul>
        </section>
      {/if}

      <section class="section-block repeat-pattern-section">
        <div class="section-heading">
          <div><p class="eyebrow">NORMALIZED FINDING PATTERNS</p><h3>Repeatability findings</h3></div>
          <span class="count">{formatCount(repeatAnalysis.patterns.length)} PATTERNS</span>
        </div>
        <p class="timeline-note">“Recurring” means this normalized pattern appears in every supplied comparison report. “Not consistently observed” means it appeared in only some reports. Neither label is a verdict about intent or safety.</p>
        {#if repeatAnalysis.patterns.length === 0}
          <div class="empty-state">
            <p class="eyebrow">NO FINDING PATTERNS</p>
            <p>The supplied reports contain no findings to aggregate. This is bounded by those reports and their collection coverage; it is not a claim that the application had no relevant activity.</p>
          </div>
        {:else}
          <div class="repeat-pattern-list">
            {#each repeatAnalysis.patterns as pattern (pattern.fingerprint)}
              <article class="repeat-pattern">
                <div class="repeat-pattern-topline">
                  <div class="tag-row">
                    <span class="tag secondary-tag">{pattern.change_type.toUpperCase()}</span>
                    <span class="tag secondary-tag">{pattern.category.toUpperCase()}</span>
                    <code>{pattern.rule_id}</code>
                  </div>
                  <span class="repeatability-tag" class:repeatable={pattern.repeatability === "recurring"} class:inconsistent={pattern.repeatability === "not_consistently_observed"}>
                    {pattern.repeatability === "recurring" ? "RECURRING" : "NOT CONSISTENTLY OBSERVED"}
                  </span>
                </div>
                <h4>{pattern.summary}</h4>
                <p>{pattern.observation}</p>
                <div class="repeat-pattern-meta">
                  <span>Observed in <strong>{formatCount(pattern.occurrence_count)} of {formatCount(repeatAnalysis.report_count)}</strong> reports</span>
                  <code>Fingerprint {pattern.fingerprint}</code>
                </div>
                <details class="repeat-reference-details">
                  <summary>Source references ({pattern.report_refs.length} reports)</summary>
                  <div class="repeat-reference-list">
                    {#each pattern.report_refs as reference (reference.report_id)}
                      <div class="repeat-reference">
                        <p><span class="side-label">SOURCE REPORT</span><code>{reference.report_id}</code></p>
                        <p class="muted">Finding ID reference(s) — open the source comparison report to inspect the original evidence.</p>
                        <ul>
                          {#each reference.finding_ids as findingId (findingId)}
                            <li><code>{findingId}</code></li>
                          {/each}
                        </ul>
                      </div>
                    {/each}
                  </div>
                </details>
              </article>
            {/each}
          </div>
        {/if}
      </section>

      <section class="section-block repeat-source-section">
        <div class="section-heading">
          <div><p class="eyebrow">INPUT REPORTS</p><h3>Source comparison reports</h3></div>
          <span class="count">{formatCount(repeatAnalysis.reports.length)} SOURCES</span>
        </div>
        <div class="repeat-source-list">
          {#each repeatAnalysis.reports as source (source.report_id)}
            <article class:partial-source={source.status === "partial"}>
              <div>
                <code class="repeat-source-id">{source.report_id}</code>
                <p class="micro muted">{formatCount(source.added_count)} added · {formatCount(source.removed_count)} removed · {formatCount(source.changed_count)} changed · {formatCount(source.uncertainty_count)} uncertainty item(s)</p>
              </div>
              <span class="repeatability-tag" class:repeatable={source.status === "complete"} class:inconsistent={source.status === "partial"}>{source.status.toUpperCase()}</span>
            </article>
          {/each}
        </div>
        <p class="micro muted">Source report IDs and finding IDs are references, not embedded evidence. The viewer does not open or load any additional files automatically.</p>
      </section>
      <footer class="report-footer"><p>Repeatability describes patterns in the supplied reports only. Partial collection, sampling, environment differences and run variability can affect what appears.</p><button class="quiet-button" type="button" onclick={resetReport}>Close analysis</button></footer>
    {/if}
  </main>
  <footer class="global-footer"><span>DRIFTGLASS / EXPERIMENTAL</span><span>LOCAL-FIRST · NO TELEMETRY UPLOAD</span></footer>
</div>
