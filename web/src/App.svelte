<script lang="ts">
  import { validateReportV1 } from "./lib/report-validation.js";
  import type { Finding, ReportV1 } from "./types";

  const MAX_FILE_BYTES = 20 * 1024 * 1024;
  let report: ReportV1 | null = $state(null);
  let errorMessage = $state("");
  let fileName = $state("");
  let query = $state("");
  let categoryFilter = $state("all");
  let changeFilter = $state("all");
  let dragActive = $state(false);
  let loading = $state(false);
  let fileInput: HTMLInputElement;

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
      const parsed: unknown = JSON.parse(await file.text());
      const result = validateReportV1(parsed);
      if (!result.ok) {
        errorMessage = result.message;
        return;
      }
      report = result.report;
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
    report = null; fileName = ""; errorMessage = ""; query = "";
    categoryFilter = "all"; changeFilter = "all";
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
    <div class="topbar-right"><span class="eyebrow">LOCAL EVIDENCE VIEWER</span><span class="version">REPORT V1</span></div>
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

    {#if !report}
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
        <p class="muted">Choose a Driftglass JSON export. It stays in this browser tab; this viewer does not upload or persist it.</p>
        <button class="primary-button" type="button" disabled={loading} onclick={() => fileInput.click()}>{loading ? "Reading report…" : "Choose JSON report"}</button>
        <input bind:this={fileInput} class="visually-hidden" type="file" accept=".json,application/json" disabled={loading} onchange={handleInputChange} />
        {#if loading}
          <p class="loading-message" role="status" aria-live="polite">Reading and validating the selected report…</p>
        {:else}
          <p class="micro">JSON only · 20 MiB maximum · report schema v1</p>
        {/if}
        {#if errorMessage}<p class="error-message" role="alert">{errorMessage}</p>{/if}
      </section>
      <section class="privacy-note"><span aria-hidden="true">○</span><p><strong>Local-only review.</strong> Paths, process names, hostnames and evidence may be sensitive. Use this viewer on a trusted machine.</p></section>
    {:else}
      <section class="report-heading">
        <div>
          <p class="eyebrow">LOADED REPORT / {fileName}</p>
          <h2>{report.application_name}</h2>
          <p class="muted">Experiment <code>{report.experiment_id}</code> · Report <code>{shortId(report.report_id)}</code></p>
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
        {#if visibleFindings.length === 0}
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
    {/if}
  </main>
  <footer class="global-footer"><span>DRIFTGLASS / EXPERIMENTAL</span><span>LOCAL-FIRST · NO TELEMETRY UPLOAD</span></footer>
</div>
