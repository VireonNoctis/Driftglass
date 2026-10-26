# Platform capability matrix

This matrix describes the collector code currently present in Driftglass, not the long-term design. The machine-readable implementation summary is available with `python -m driftglass capabilities`.

## Native collector status

| Platform | Collector status | Implemented scope | Explicitly unavailable / unverified |
|---|---|---|---|
| Linux | Experimental, best-effort | Polls `/proc` for process stat metadata and samples TCP/TCP6 socket metadata associated with a selected PID and observed descendants. | No file activity, system/persistence changes, UDP, DNS, packet payloads, command-line arguments, or comprehensive event tracing. Real-world coverage across Linux builds is unverified. |
| Windows | Not implemented | None. | No native collector or platform coverage tests. |
| macOS | Not implemented | None. | No native collector or platform coverage tests. |
| Other | Not implemented | None. | No native collector or platform coverage tests. |

## What the Linux prototype can observe

The observer samples process identity/parent metadata and TCP socket state/endpoints. It uses process stat rows and file-descriptor symlinks to infer socket ownership, so permission errors, process races, ambiguous ownership, and namespace differences can reduce coverage.

Collection is polling-based. A process or socket that appears and disappears between samples can be missed. A “no longer observed” event does not establish the exact time a process exited or a socket closed. First-sample observations may already have existed before capture began.

The current capture wrapper declares `file`, `system`, and `persistence` categories unavailable. The `process` and `network` categories do not represent comprehensive collection; network coverage is TCP metadata only.

## Validation status

Automated CI runs synthetic Python and Rust tests and one live Linux procfs smoke test using the test process and a loopback TCP listener, along with packaging and web checks. This smoke test validates visibility for that CI environment; it does not measure event loss, prove coverage on every Linux distribution/kernel, or establish a production false-positive rate. Windows and macOS collector support must remain unclaimed until implementations and platform-specific tests exist.

Driftglass does not launch the target application, create or validate VM isolation, capture packet payloads, collect file contents, or automatically block/remediate an application. Reports are evidence for investigation, not safety verdicts.
