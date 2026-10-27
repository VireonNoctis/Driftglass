# Decision: defer comprehensive Linux event tracing

**Status:** Deferred; the experimental /proc polling observer remains the only Linux collector.

## Decision

Do not describe the current observer as a comprehensive event tracer, and do not add a second kernel-event collection mechanism in this milestone batch. The current collector samples process metadata and TCP/TCP6 socket state; short-lived activity can occur between samples.

## Why

A kernel-event path changes required privileges, kernel compatibility, event-loss behavior, failure modes, and the meaning of “complete.” Adding a collector without a supported-kernel matrix and observable loss/permission reporting would make coverage claims less trustworthy, not more.

## Re-entry criteria

Before implementing a kernel-event collector, document the exact source and privilege requirements; add explicit health and loss counters; test permissions-denied and unsupported-kernel cases; use controlled short-lived process and socket workloads; and publish measured limits separately for each supported runner/kernel configuration.

## Consequence

Linux support remains experimental and polling-based. The current paired-capture smoke test is not an event-loss-rate estimate.