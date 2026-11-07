# Implemented technology stack

This inventory reflects implementation files and build manifests currently in this repository. It is not a product support or platform-coverage claim.

## Core and native components

- **C++20** — local CLI, schema validation, capture coordination, deterministic comparison, report generation, redaction, integrity, repeat-run analysis, and SQLite report history.
- **Rust** — experimental Linux `/proc` observer for process and TCP/UDP socket metadata.
- **Swift** — experimental macOS process-tree sampler using libproc.
- **CMake** — native build, install rules, CTest integration, and tarball packaging.
- **Cargo** — Rust workspace and reproducible observer build.

## Dashboard

- **Svelte + TypeScript** — read-only report and evidence viewer.
- **CSS** — responsive monochrome dashboard styling and reduced-motion support.
- **npm / Vite** — frontend dependency and build tooling.

## Data and contracts

- **SQLite** — local report history.
- **JSONL** — event interchange.
- **JSON Schema** — versioned event, manifest, report, repeat-analysis, profile, capability, and export-integrity contracts.

## Automation

- **GitHub Actions** — native, Rust, Swift, frontend, reproducible-build, and repository-quality checks.

## Explicit non-claims

The presence of a language, build target, or schema does not establish collector coverage. Linux sampling remains experimental, macOS sampling is process-only, and Windows, file-system, persistence, VM lifecycle, and local API functionality remain unavailable or deferred.
