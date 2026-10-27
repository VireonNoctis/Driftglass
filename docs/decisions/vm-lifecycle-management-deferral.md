# Decision: defer VM and target lifecycle automation

**Status:** Deferred; experiment setup and target launch remain operator-managed.

## Decision

Do not add an automatic VM manager, snapshot controller, or target-application launcher to the current workflow.

## Why

A comparison report must never launch untrusted software merely because it is opened or inspected. VM lifecycle automation also needs a clearly defined isolation backend, failure recovery, snapshot identity, resource limits, and a test boundary that cannot accidentally run a candidate on a developer host. Those safeguards are not established by the current observer.

## Re-entry criteria

Specify supported VM backends and threat boundaries; require explicit user action to launch a candidate; prove the VM identity and snapshot metadata are recorded; test cancellation, timeout, cleanup failure, and isolation failure; and keep report viewing independent from execution.

## Consequence

Users must prepare equivalent isolated environments and start the application themselves. Driftglass does not prove that a VM or other isolation boundary is active.