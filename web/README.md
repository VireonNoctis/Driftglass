# Driftglass report viewer

An experimental, read-only Svelte viewer for one JSON report exported by Driftglass. It displays run summaries, context differences, searchable findings, a chronological timeline of evidence referenced by findings, source evidence, and declared uncertainties. The timeline is not the complete raw event stream; it cannot show events absent from the report.

## Development

Requires Node.js 22 and npm.

```sh
npm install
npm test
npm run check
npm run build
npm run dev
```

Vite binds to 127.0.0.1. Do not expose the development server to an untrusted network.

## Input and trust limits

- Accepts JSON files up to 20 MiB and checks report v1 shape and evidence-link fields before rendering.
- The native C++ CLI is authoritative for full JSON Schema, cross-field and content-derived report-ID validation. Browser intake performs defensive structural and evidence-link checks; browser report-v1 validation does not recompute the report ID.
- Report values are rendered as text; the viewer does not inject raw HTML.
- Data stays in page memory. There is no API, remote request, local storage, uploader, collector launch, or background persistence.
- Evidence paths, process names, hostnames, timestamps, and notes can be sensitive. Use a trusted machine.
- A complete report is not proof of complete OS collection or software safety.
