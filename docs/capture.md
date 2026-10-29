# Experimental capture coordination

The command described here wraps the experimental Linux procfs observer for a bounded capture of an application that is already running. It does not launch or stop the application, elevate privileges, create a VM, or coordinate a full baseline/candidate workflow.

## Prepare the environment

Build the observer from the repository root:

~~~bash
cargo build --release -p driftglass-agent
~~~

Run untrusted software only inside a disposable VM or other appropriately isolated environment. Launch the test application yourself, then supply its PID to the capture command. Keep personal accounts, credentials, private files, and sensitive networks out of the test environment.

## Capture one run

~~~bash
python -m driftglass capture \
  --observer ./target/release/driftglass-agent \
  --pid 1234 \
  --duration-seconds 30 \
  --interval-ms 250 \
  --experiment-id example-update \
  --role baseline \
  --application-name ExampleApp \
  --application-version 1.0 \
  --scenario "Open the main window and wait for 30 seconds" \
  --event-output ./baseline-events.jsonl \
  --manifest-output ./baseline-manifest.json
~~~

Use the same experiment ID and equivalent scenarios for the candidate run. Change the role, version, PID, and output paths, and record any important environment differences. Artifact SHA-256 is optional and user-supplied; the coordinator does not verify that it corresponds to the executable running under the supplied PID.

Output paths must be distinct, must not already exist, and must have an existing parent directory. New manifest files are created with owner-only permissions on POSIX. The observer independently refuses to overwrite an existing JSONL path. The wrapper passes arguments as an argv list rather than constructing a shell command. Manifest-write cleanup records the identity of the file it created and removes a partial file only while that path still refers to that file; a pre-existing or concurrently replaced path is preserved.

## Manifest and failure handling

The wrapper validates the event stream against the event schema and validates the generated run manifest against manifest v1. A normal observer exit without both capture boundary records is treated as incomplete. An explicit `capture_incomplete` event also invalidates the capture, even if the process exits zero and emits both boundary records. A failed start, non-zero exit, timeout, missing stream, or invalid JSONL produces a failed/incomplete manifest when the schema and output path allow it, then returns a non-zero command status.

The Linux observer samples process and TCP metadata. File, system, and persistence categories are declared unavailable. It can miss short-lived activity, and permission/read errors or ambiguous socket ownership can produce partial-quality records and coverage notes. A capture status of completed means the sampling interval ended and its boundary records were present; it does not mean collection was comprehensive.

The dropped_events field is zero because this observer does not report an event-queue drop count. It is not proof that no relevant activity was missed. Review coverage records, manifest errors, and environment metadata before comparing runs. The observer and wrapper are experimental; the coordinator does not verify actual VM isolation or broad Linux event coverage.


## Controlled paired-capture CI test

CI builds the observer and runs `tests/integration/test_linux_paired_capture.py`. The test uses a loopback-only TCP listener held open by the test process itself; it does not launch a third-party or user-supplied target. The capture wrapper records two bounded runs, checks that each contains exactly one `socket_present` observation for the known listener, and compares the resulting reports.

This validates the expected event path for one long-lived loopback listener on the CI runner. It does not estimate a general event-loss rate, cover short-lived socket lifetimes, or establish coverage across Linux kernel/distribution configurations. The live run can remain partial for declared unavailable categories or other collection gaps.
