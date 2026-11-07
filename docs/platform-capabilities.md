# Platform capability matrix

This matrix describes the collector code currently present in Driftglass, not the long-term design. The machine-readable implementation summary is available with `build/cpp/driftglass-core capabilities`.

## Native collector status

| Platform | Collector status | Implemented scope | Explicitly unavailable / unverified |
|---|---|---|---|
| Linux | Experimental, best-effort | Polls `/proc` for process stat metadata and samples TCP/TCP6/UDP/UDP6 socket metadata associated with a selected PID and observed descendants. | No file activity, system/persistence changes, individual UDP datagrams, DNS queries, packet payloads, command-line arguments, or comprehensive event tracing. Real-world coverage across Linux builds is unverified. |
| Windows | Not implemented | None. | No native collector or platform coverage tests. |
| macOS | Experimental process-only helper; not integrated | A Swift/libproc prototype samples the selected, already-running PID and visible descendants, emitting v1 process/coverage JSONL records. A macOS CI job builds it and tests process-tree selection. | No Endpoint Security event stream, network, file, system or persistence events; no run-manifest/capture-coordinator integration, signing/entitlement installation workflow, or real-world coverage claim. |
| Other | Not implemented | None. | No native collector or platform coverage tests. |

The Swift macOS helper is a separate experimental process-only source, not a complete macOS platform collector. It uses `libproc` snapshots, so short-lived processes between samples can be missed and protected processes may be inaccessible. Its process records and visible gaps are best-effort. Apple Endpoint Security events, network activity, file activity, system/persistence changes, and signed System Extension deployment are not implemented. The main capability JSON continues to describe the integrated collector, not this standalone prototype.

## What the Linux prototype can observe

The observer samples process identity/parent metadata and TCP/UDP socket state/endpoints. It uses process stat rows and file-descriptor symlinks to infer socket ownership, so permission errors, process races, ambiguous ownership, and namespace differences can reduce coverage.

Collection is polling-based. A process or socket that appears and disappears between samples can be missed. A “no longer observed” event does not establish the exact time a process exited or a socket closed. First-sample observations may already have existed before capture began.

The current capture wrapper declares `file`, `system`, and `persistence` categories unavailable. The `process` and `network` categories do not represent comprehensive collection; network coverage is sampled TCP and UDP socket metadata only, not packet or datagram activity.

### CI runner configurations actually tested

The following runner/kernel configurations have passed the Rust workspace tests and controlled paired-capture loopback test:

| CI image | OS release | Kernel observed in CI logs | Checks |
|---|---|---|---|
| `ubuntu-22.04` | Ubuntu 22.04.5 LTS | `6.8.0-1064-azure` | Rust workspace tests; paired-capture integration test |
| `ubuntu-24.04` | Ubuntu 24.04.5 LTS | `6.17.0-1022-azure` | Rust workspace tests; paired-capture integration test |

This is two observed hosted-runner configurations, not exhaustive distribution/kernel coverage. Permission profiles, non-Azure kernels, other filesystems and separate network namespaces still need independent validation.

## Validation status

Automated CI runs native C++ contract/regression tests, Rust observer tests, and a live paired-capture test using a synthetic loopback TCP listener and UDP socket fixture. The CI matrix runs these checks on Ubuntu 22.04 and 24.04 runner images. This is a limited CI matrix, not a general Linux support guarantee. This smoke test validates visibility for that CI environment; it does not measure event loss, prove coverage on every Linux distribution/kernel, or establish a production false-positive rate. Windows and macOS collector support must remain unclaimed until implementations and platform-specific tests exist.

Driftglass does not launch the target application, create or validate VM isolation, capture packet payloads, collect file contents, or automatically block/remediate an application. Reports are evidence for investigation, not safety verdicts.
