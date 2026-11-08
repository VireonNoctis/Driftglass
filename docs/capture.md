# Experimental native capture coordination

The C++20 CLI wraps an explicitly selected observer for a bounded capture of an application that is already running. It does not launch or stop the application, elevate privileges, create a VM, or coordinate a full baseline/candidate workflow.

## Prepare the environment

Build the Rust observer and native CLI from the repository root:

~~~bash
cargo build --locked --release -p driftglass-agent
cmake -S cpp -B build/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp --parallel
~~~

Run untrusted software only inside a disposable VM or other appropriately isolated environment. Launch the test application yourself, then supply its PID to the capture command. Keep personal accounts, credentials, private files, and sensitive networks out of the test environment.

## Capture one run

~~~bash
build/cpp/driftglass-core capture \
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

Use the same experiment ID and equivalent scenarios for the candidate run. Change the role, version, PID, and output paths, and record important environment differences. Artifact SHA-256 is optional and user-supplied; the coordinator does not verify that it corresponds to the executable running under the supplied PID.

Output paths must be distinct, must not already exist, and must have an existing parent directory. Manifest content is written to an owner-only temporary file in the destination directory, flushed, and atomically published with a no-overwrite hard link; an existing or concurrently created destination is preserved, and readers never see a partially written manifest. The observer independently refuses to overwrite an existing JSONL path. The coordinator resolves the supplied observer path to a canonical path, verifies that it names an executable regular file, and passes that same resolved path to the process launcher and as argv[0]. This avoids resolving a caller-supplied symlink alias a second time at launch; it does not make the executable immutable if another actor can replace the canonical target itself. The coordinator passes arguments as an argv list rather than constructing a shell command.

The native CLI applies the checked-in v1 event and manifest schemas by default. Use --event-schema FILE and --manifest-schema FILE for custom schema contracts. The JSONL reader enforces a 64 MiB total stream limit, 1 MiB per-line limit, and 250,000-record limit.

## Manifest and failure handling

A normal observer exit without both capture boundary records is treated as incomplete. The capture_started record must identify the exact PID requested by the coordinator, and capture_finished must declare capture_status: completed; missing or mismatched boundary metadata is incomplete. An explicit capture_incomplete event also invalidates the capture, even if the process exits zero and emits both boundary records. A failed start, non-zero exit, timeout, missing stream, or invalid JSONL produces a failed/incomplete manifest when the schema and output path allow it, then returns a non-zero command status.

The Linux observer samples process and TCP/UDP socket metadata. File, system, and persistence categories are declared unavailable. It can miss short-lived activity, and permission/read errors or ambiguous socket ownership can produce partial-quality records and coverage notes. A capture status of completed means the sampling interval ended and its boundary records were present; it does not mean collection was comprehensive.

The dropped_events field is zero because this observer does not report an event-queue drop count. It is not proof that no relevant activity was missed. Review coverage records, manifest errors, and environment metadata before comparing runs. The observer and wrapper are experimental; the coordinator does not verify actual VM isolation or broad Linux event coverage.

## Controlled paired-capture CI test

The Linux CI matrix builds the Rust observer and invokes cpp/tests/run_paired_capture.sh through CTest. The fixture is a native C++ process that binds a loopback-only TCP listener and UDP socket, then holds both sockets open while the coordinator performs a bounded baseline/candidate pair. It does not launch a third-party or user-supplied target. The integration checks that each captured stream contains the expected TCP and UDP socket metadata, validates the manifests, and compares the pair.

This validates the expected event path for known long-lived loopback sockets on the CI runners. It does not estimate a general event-loss rate, cover short-lived socket lifetimes, or establish coverage across Linux kernel/distribution configurations. The live run can remain partial for declared unavailable categories or other collection gaps.