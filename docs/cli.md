# Native developer CLI

The C++20 command-line application validates event streams and run manifests, coordinates one bounded observer session for an already-running process, compares baseline/candidate runs, exports reports, and manages local SQLite history. It does not launch or stop the target application, create a virtual machine, elevate privileges, or upload telemetry.

## Build and install

Install CMake 3.20+, a C++20 compiler, OpenSSL development files, SQLite3 development files, and nlohmann-json 3.10+.

From the repository root:

~~~bash
cmake -S cpp -B build/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp --parallel
ctest --test-dir build/cpp --output-on-failure
cmake --install build/cpp --prefix "$HOME/.local"
~~~

The build installs driftglass-core and versioned JSON Schema files under share/driftglass/schemas. Create a tar.gz package with:

~~~bash
cmake --build build/cpp --target package
~~~

The CLI resolves schemas from DRIFTGLASS_SCHEMA_DIR, the build-time source schema directory, its installation prefix, or the current working directory. Commands that accept a schema path also accept an explicit override. Local JSON Pointer references such as #/$defs/name are supported.

## Capabilities

~~~bash
build/cpp/driftglass-core capabilities
~~~

The versioned capability document describes implemented and unavailable collector categories. It describes implementation status, not the permissions or coverage of a particular run. Linux observation remains experimental /proc polling; macOS observation remains a process-only Swift sampler.

## Validate input files

~~~bash
build/cpp/driftglass-core events validate ./run-events.jsonl
build/cpp/driftglass-core manifest validate ./run-manifest.json
build/cpp/driftglass-core report validate ./comparison.json
~~~

Optional schema overrides are --event-schema FILE, --manifest-schema FILE, and --report-schema FILE respectively. Event streams are limited to 64 MiB, 1 MiB per record, and 250,000 records. Run manifests are limited to 4 MiB; reports are limited to 64 MiB. Blank JSONL records, duplicate event IDs, duplicate JSON object keys, malformed records, and empty event streams are rejected.

Report validation checks the v1 schema, evidence/run relationships, content-derived finding/report IDs, comparison-profile fingerprint, and cross-field counts. A content hash detects inconsistency relative to the content; it is not a signature and does not prove that capture was complete.

## Detached report integrity

~~~bash
build/cpp/driftglass-core report attest \
  --report ./comparison.json \
  --output ./comparison.integrity.json

build/cpp/driftglass-core report verify \
  --report ./comparison.json \
  --manifest ./comparison.integrity.json
~~~

The sidecar records the report file SHA-256, byte size, and validated report ID. It is not signed. Anyone able to replace both report and sidecar can recompute the hash, so it does not authenticate the author or prove collection completeness. Optional overrides are --report-schema FILE and --manifest-schema FILE.

## Capture an already-running process

Build the experimental observer:

~~~bash
cargo build --locked --release -p driftglass-agent
~~~

Inside an appropriately isolated test environment, start the application yourself and pass its PID:

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

For custom contracts, pass --event-schema FILE and --manifest-schema FILE. Repeat with the same experiment ID and equivalent scenario for the candidate version, changing the role, version, PID and output paths.

Output files must be new and their parent directories must already exist. Report exports are staged in an exclusively created temporary file in the destination directory before publication; on POSIX systems, the published file is owner-readable and owner-writable only. The writer does not follow an existing destination symlink when replacing an output. On Linux, the coordinator opens the canonical observer target with `O_PATH | O_NOFOLLOW`, validates the pinned descriptor, and executes through `/proc/self/fd/<descriptor>`. The inheritable `O_PATH` handle avoids a read-open, supports execute-only files, and does not itself provide normal read/write access. Native binaries receive the canonical path as `argv[0]`; for shebang scripts, Linux supplies the descriptor path as the interpreter's script argument. This pins file identity against pathname replacement but does not prevent in-place modification of the file, and requires Linux `O_PATH` support plus procfs. On macOS, launch remains pathname-based after canonicalization, so replacement of the canonical target before execution remains a TOCTOU limitation. The observer receives a dedicated process group; the coordinator terminates remaining group members on normal exit and on timeout using SIGTERM followed by SIGKILL after a bounded grace period. Descendants that detach into another group or session are outside this cleanup boundary. The coordinator uses an argument vector rather than shell command construction, enforces duration/output limits, checks capture boundary records and PID identity, validates the event stream, and writes a manifest even when the result is incomplete where possible. The optional artifact hash is user-supplied and is not verified against the running executable. Capture does not prove that a process ran inside a VM.

The Linux observer samples process and TCP/UDP socket metadata. It can miss short-lived activity; file, system and persistence categories remain unavailable. A completed capture means the observer completed its declared bounded contract, not that coverage was comprehensive.

## Compare baseline and candidate runs

~~~bash
build/cpp/driftglass-core compare \
  --baseline-manifest ./baseline-manifest.json \
  --candidate-manifest ./candidate-manifest.json \
  --baseline-events ./baseline-events.jsonl \
  --candidate-events ./candidate-events.jsonl \
  --output ./comparison.json \
  --html-output ./comparison.html \
  --history-db ./driftglass-history.sqlite
~~~

Optional inputs are --profile FILE, --event-schema FILE, --manifest-schema FILE, --profile-schema FILE, and --report-schema FILE. Without --output, the complete report JSON is printed to stdout. The local SQLite store keeps the unredacted source report.

For an exported copy, enable only the required redaction categories using --paths, --network, and/or --process-names. Redaction is best-effort, recalculates finding/report IDs, and does not modify the source report. Existing output files are refused unless --overwrite is explicitly supplied.

## Repeat-run analysis

~~~bash
build/cpp/driftglass-core repeat analyze \
  ./comparison-1.json ./comparison-2.json \
  --output ./repeat-analysis.json
~~~

The command accepts 2–500 previously generated report files, limits each to 64 MiB and aggregate input to 256 MiB, and supports --report-schema FILE and --analysis-schema FILE. It summarizes supplied reports; it does not schedule or capture repeated experiments. Recurrence is not evidence of malicious intent.

## Local history

~~~bash
build/cpp/driftglass-core history list --database ./driftglass-history.sqlite
build/cpp/driftglass-core history show --database ./driftglass-history.sqlite --report-id REPORT_ID
build/cpp/driftglass-core history export \
  --database ./driftglass-history.sqlite \
  --report-id REPORT_ID \
  --format html \
  --output ./saved-report.html
build/cpp/driftglass-core history delete \
  --database ./driftglass-history.sqlite \
  --report-id REPORT_ID \
  --confirm REPORT_ID
~~~

History export supports JSON and standalone HTML, optional --report-schema FILE, --paths, --network, --process-names, and --overwrite. Redacted exports are copies; stored history is unchanged. Deletion requires an exact report-ID confirmation and affects only the selected SQLite database. It does not delete original JSONL/manifests, exported files, backups, or securely erase storage media.

Driftglass has no daemon, local HTTP API, remote backend, VM lifecycle manager, automatic target launch, scheduler, or automatic remediation.
