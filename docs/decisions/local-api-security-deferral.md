# Decision: defer local API access-control validation

- **Status:** Accepted; implementation deferred
- **Date:** 2026-10-27
- **Scope:** Local API authorization, cross-origin protections, and data-flow validation

## Decision

Keep the roadmap item for local API access controls unchecked until a local API actually exists. Do not add a placeholder service or describe access controls as validated. The current dashboard is a read-only browser viewer for a user-selected report file; the CLI is a local command-line tool, and the implemented architecture documents no daemon or HTTP API.

## Rationale

There are no API routes, server-side request handlers, authentication checks, or API data flows to exercise in the current implementation. The Vite development server's loopback binding is a development-server setting, not evidence that a future API has safe authorization or cross-origin behavior. Claiming this milestone complete now would conflate a documented design requirement with a tested security property.

The current data flow is file-based: the operator runs the CLI, creates or exports a report, then explicitly loads that file in the browser viewer. The viewer does not upload reports or contact a Driftglass backend.

## Re-entry criteria

Reopen this milestone only after a local API is proposed and implemented. Before calling it validated, require:

1. A documented endpoint inventory, threat model, and data-flow diagram covering callers, stored data, and trust boundaries.
2. A loopback-only bind by default, with explicit configuration and safe failure behavior if the bind cannot be enforced.
3. Tests for unauthorized requests, hostile or cross-origin browser requests, malformed and oversized inputs, and unexpected HTTP methods.
4. Tests showing that the API cannot trigger candidate launches, privileged collection, deletion, remediation, or report transmission without an explicit supported workflow.
5. Documented authentication/authorization decisions, error handling, logging and sensitive-data redaction, plus verification that the API is not exposed on external interfaces by default.

This decision does not implement an API and does not establish that any future API is secure.
