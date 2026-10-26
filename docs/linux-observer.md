# Experimental Linux /proc observer

The Rust package **driftglass-agent** is an experimental, best-effort sampler for Linux. It is not yet a complete collector and is not a security boundary.

## Run

Build with a Rust toolchain:

~~~bash
cargo test --workspace
cargo build -p driftglass-agent --release
./target/release/driftglass-agent --pid 1234 --duration-seconds 30 --interval-ms 250 --output ./run-events.jsonl
~~~

Supply the PID of an already-running application inside a disposable test environment. The agent does not start, stop, elevate, block, or remediate the target. The duration defaults to 30 seconds; valid duration values are 1–3600 seconds. The sampling interval defaults to 250 ms and must be 100–10000 ms. The output path must name a new file; the agent refuses to overwrite an existing path. Omit the output option to write JSONL to standard output.

The observer samples procfs process stat records to identify the target and processes whose parent chain descends from it. It samples TCP tables in the selected target's network namespace and inspects the tracked processes' file-descriptor symlinks to associate socket inodes with that process tree. Descendants in a different network namespace are not covered by the same TCP table sampling. A synthetic Rust test confirms that `read_tcp_snapshot` reads the `/proc/<pid>/net` table for the requested PID. That test checks PID-specific path selection only; it does not create real network namespaces or prove visibility into descendants in another namespace. Emitted event records use the schema in schemas/events/event-v1.schema.json.

## Scope and coverage limitations

- This is polling, not kernel event tracing. Processes and sockets that start and stop between samples can be missed. Observation timestamps represent sample time, not the precise time an action occurred.
- A process or socket absent from a later sample is reported as no longer observed; this does not prove the exact exit or close time.
- The collector emits TCP metadata only. It does not inspect packet payloads, DNS, UDP, file contents, file access, persistence changes, or command-line arguments.
- TCP socket ownership can be unreadable due to permissions or process races. If the same socket inode is referenced by multiple scoped PIDs, the event keeps one deterministic representative owner, marks each affected socket event `collection_quality: partial`, and emits a network coverage gap recording the ambiguous-owner observation count; treat that process attribution as uncertain. Missing procfs tables, denied reads, and malformed rows are also summarized as coverage-gap or incomplete-capture records when possible. Inspect capture-finished collection quality and preceding coverage events; a finished capture can have incomplete coverage.
- When a process context is available, event records include `process_start_time_ticks` in the generic attributes object. This is the procfs start-time tick count since boot, not a wall-clock timestamp; downstream behavior matching ignores this run-specific value and report evidence preserves it as optional process identity metadata.

The Rust observer itself does not produce a run manifest. The experimental Python CLI wrapper can pair its JSONL output with a v1 manifest for an already-running PID. The wrapper records host OS/kernel release and architecture, accepts an optional user-supplied artifact hash that is not verified against the process, and declares file, system, and persistence categories unavailable. It does not infer dropped kernel events or provide a formal comparison.
- It emits records only for the selected process and observed descendants, but samples process stat metadata and TCP tables from procfs to perform that filter. Run it in a disposable VM; these sources can reveal sensitive local system information.
- Procfs visibility and socket-to-PID ownership differ with user privileges, container configuration, kernel settings, and distribution. No cross-kernel coverage claim is made.

The Rust test suite includes one live Linux smoke test that binds a loopback-only TCP listener, then checks that procfs process metadata, the TCP table and socket-to-PID association all expose it in the CI environment. It does not measure event loss or validate other kernel/distribution/permission configurations.

The first-sample actions record what was already present at the first sample. They do not claim that the process or connection started during capture. A capture-finished record means the sampling interval ended; partial collection quality signals known gaps, not a negative result.
