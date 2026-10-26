# Experiment lifecycle and isolation boundaries

This is the supported manual workflow today. Driftglass does not orchestrate a complete experiment or manage virtual machines.

## 1. Prepare an isolated test environment

Use a disposable VM snapshot or another deliberate isolation boundary appropriate to the application. Keep personal accounts, credentials, private files and sensitive networks out of the lab. The operator, not Driftglass, installs or starts the target application and verifies the isolation. A container alone is not claimed to be a strong boundary for hostile software.

## 2. Define and record the run

Choose a consistent experiment ID, application name/version, scenario, capture profile and environment. Run the same user-performed scenario for baseline and candidate. Record meaningful differences in OS/kernel build, architecture, snapshot, permissions and networking. An optional artifact SHA-256 is user-supplied and is not verified against the process under observation.

## 3. Capture one already-running PID

Build the Rust observer and invoke `python -m driftglass capture` with the PID of a process already running inside the lab. Capture is bounded by duration and interval. Driftglass starts the observer only; it does not launch or stop the target, elevate privileges, make VM snapshots, or automatically repeat the scenario.

Outputs are new JSONL and manifest files. Existing output paths are refused. The wrapper validates observer output and the v1 manifest contract. It exits non-zero when the observer cannot complete its capture contract; where possible it writes a failed/incomplete manifest to preserve the failure.

## 4. Interpret status and coverage correctly

- **completed:** the observer exited successfully and the expected start/finish boundary events were present. This is not proof that every relevant event was observed.
- **incomplete:** events exist but the observer reported an incomplete capture or another contract gap.
- **failed:** no valid capture stream was produced or the observer could not be started.
- **coverage gap:** a category/source/read had a known limitation or error. It does not mean the application did nothing in that area.
- **dropped_events = 0:** the current polling observer does not report a kernel event-queue drop count. Zero is not a measured event-loss rate and does not prove no observations were missed.

Short-lived processes/sockets can begin and end between polls. The first sample may show activity that existed before capture started. A later absence does not establish an exact exit/close time.

## 5. Validate, compare and review

Validate JSONL and manifests, then compare baseline and candidate with the same experiment ID and equivalent setup. Review added/removed/changed findings alongside evidence, per-run coverage, environment differences and uncertainty. Repeat analysis summarizes previously generated reports; it does not schedule captures and a recurring pattern does not prove malicious intent.

## 6. Export and clean up

Create JSON/standalone HTML or SQLite history only as needed. Redaction is opt-in and best-effort; inspect exports before sharing. Deletion is explicit and report-ID-specific, and does not securely erase source files or backups.

## Things Driftglass must not do automatically

The current workflow does not launch the target, stop or modify it, create/revert VMs, silently elevate, upload reports, block network activity, delete application files, or apply remediation. Opening a report never starts the candidate application.
