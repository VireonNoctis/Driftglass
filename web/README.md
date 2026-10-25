# Driftglass report viewer

An experimental, read-only Svelte viewer for one JSON report exported by Driftglass. It displays run summaries, context differences, searchable findings, evidence references, and declared uncertainties.

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
- The Python exporter remains authoritative for full JSON Schema validation and recalculation of the deterministic report ID. Browser checks do not verify that hash.
- Report values are rendered as text; the viewer does not inject raw HTML.
- Data stays in page memory. There is no API, remote request, local storage, uploader, collector launch, or background persistence.
- Evidence paths, process names, hostnames, timestamps, and notes can be sensitive. Use a trusted machine.
- A complete report is not proof of complete OS collection or software safety.
