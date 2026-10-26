# Decision: defer system and persistence observation

- **Status:** Accepted; implementation deferred
- **Date:** 2026-10-26
- **Scope:** Startup configuration, services, scheduled tasks, system settings and other persistence mechanisms

## Decision

Keep `system` and `persistence` categories explicitly unavailable. Do not add a generic collector or imply that an empty scan means no persistence behavior occurred.

This is a scope decision, not an implementation.

## Rationale

System and persistence behavior spans distinct OS-specific sources and permission boundaries. A reliable observer must decide which sources are in scope, how to distinguish an observed value from an inaccessible or unsupported source, how changes map into the versioned event model, and what happens when a source cannot be read. The present Linux process/TCP sampler implements none of these sources and has no tests for them.

Taking arbitrary snapshots of startup paths or settings without an explicit contract would turn a partial inventory into misleading coverage claims. The current run manifest already has a way to declare these categories unavailable; that remains the truthful behavior.

## Re-entry criteria

For each source, require:

1. A narrowly defined configuration or event source and least-privilege access model.
2. Documented read-only behavior and no automatic edits/remediation.
3. Explicit coverage and permission-denied records.
4. Synthetic before/after fixtures, malformed inputs and a benign live test where appropriate.
5. An OS/build-specific capability statement before the source is described as supported.

Until then neither category may be described as monitored.
