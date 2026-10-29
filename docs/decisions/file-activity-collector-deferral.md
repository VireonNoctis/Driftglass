# Decision: defer file-activity collection

- **Status:** Accepted; implementation deferred
- **Date:** 2026-10-26
- **Scope:** File access/create/modify/delete events attributed to the application under observation

## Decision

Keep the `file` category explicitly unavailable. Do not add a file watcher until Driftglass selects and validates a collection source that can provide the required event semantics, source attribution, permission model and coverage-gap reporting.

This decision records a deferral; it does not implement file telemetry.

## Rationale

Linux `inotify` can report watched path changes, but its documented interface does not identify the process that triggered an event, cannot reliably count coalesced events, and misses some memory-mapped activity. A system-wide path watcher alone would therefore risk attributing unrelated host activity to the observed application. See the [Linux inotify manual](https://www.man7.org/linux/man-pages/man7/inotify.7.html).

Linux `fanotify` event metadata can include the PID responsible for an event, but choosing its permission requirements, filesystem scope, event queue behavior, and coverage/error policy still needs a separately tested design. See the [Linux fanotify manual](https://www.man7.org/linux/man-pages/man7/fanotify.7.html).

Driftglass currently has no file-event collector. Claiming file support from path diffs or an unvalidated watch would violate the project's source-attribution and coverage requirements.

## Re-entry criteria

Revisit file-activity implementation when a proposal demonstrates:

1. Defined event actions and resource identity in the versioned event schema.
2. A tested means of attributing relevant events to the selected process scope, including ambiguous or unavailable attribution.
3. Explicit permission-denied, queue-overflow, unsupported-filesystem and rename/delete behavior.
4. Synthetic fixtures plus benign live workloads with expected/observed results.
5. A minimal metadata policy that collects no file contents by default.

Until then the capability report and run manifests must continue to declare file activity unavailable.
