# Decision: defer the native Windows observer

- **Status:** Accepted; implementation deferred
- **Date:** 2026-10-26
- **Scope:** Native Windows process, network, file and persistence collection

## Decision

Keep Windows marked `not_implemented`. Do not add a placeholder collector or claim support until the project can build and test a specific event-source design on Windows.

## Rationale

Windows provides Event Tracing for Windows (ETW), which supports provider-based tracing and consuming events, including documented kernel logger scenarios. The API surface requires a deliberate choice of providers, event fields, permissions, buffer/trace lifecycle and loss reporting; it is not a drop-in equivalent to the current Linux procfs sampler. See [Microsoft's ETW overview](https://learn.microsoft.com/en-us/windows/win32/etw/event-tracing-portal) and [ETW tracing sessions](https://learn.microsoft.com/en-us/windows/win32/etw/event-tracing-sessions).

The repository currently has no Windows observer, Windows CI runner, Windows fixture harness or platform-specific coverage matrix. Implementing an untested collector on the current Linux-only build/test path would provide no trustworthy evidence that it sees events or reports its gaps correctly.

## Re-entry criteria

Revisit Windows observer implementation when all of the following are available:

1. A supported Windows build/test environment and a documented event-provider selection.
2. A minimal privilege and data-minimization model, including whether elevated service components are required.
3. Explicit dropped-event, permission and unsupported-source reporting.
4. Synthetic fixtures and benign live workload tests with recorded Windows build and expected/actual observations.
5. A published capability matrix separating implemented event families from unobserved activity.

This decision does not implement Windows collection. The capability report remains `not_implemented`.
