# Threat model

## Scope

Driftglass is a local-first prototype for comparing observations from two application runs. This model covers the current CLI, Linux procfs polling observer, JSONL/manifests, comparison/report pipeline, SQLite history, and browser-only report viewer. It does not assert that the software has undergone a complete independent security audit.

## Assets to protect

- Event streams, run manifests, comparison reports, and SQLite history.
- Hostnames, IP addresses, executable names, process identifiers, filesystem paths in imported reports, and operator-supplied artifact metadata.
- Integrity of comparison results and links between findings and source evidence.
- The integrity of the developer's host and test environment.

## Trust boundaries and adversaries

| Input or boundary | Assumption | Relevant controls in the current prototype |
|---|---|---|
| Application being inspected | May behave unexpectedly or be hostile. | Driftglass does not launch or elevate it; run it in a disposable VM managed by the operator. |
| Procfs observations | May be incomplete because of polling, permissions, namespace differences or process races. | Coverage records, incomplete status, event quality and manifest warnings are retained. |
| Imported JSON/JSONL | May be malformed, very large or contain misleading fields. | Strict schema validation, a 64 MiB event-stream limit, 1 MiB event-line limit, 250,000-record limit, 4 MiB manifest limit, 1 MiB rule-profile limit, 64 MiB comparison-report file limit and non-zero errors for invalid inputs. |
| Imported report in browser | Is untrusted input, including HTML-like text. | Structural validation and text rendering; no report-supplied HTML execution is intended. |
| Local history and exported files | May reveal sensitive metadata and can be modified by a local user. | Owner-only file creation where supported, validated report writes and parameterized SQLite queries; local history is not cryptographically immutable. |
| Host/kernel/observer | May be misconfigured or compromised. | No reliable defense against a compromised underlying host is claimed. |

## Main risks and limits

- **Missed or misattributed activity:** sampling can miss short-lived processes and sockets; access errors or ambiguous ownership reduce confidence. Reports must not turn lack of observation into proof of absence.
- **Hostile or malformed input:** malformed event/report structures may trigger errors or misleading display. JSONL and comparison-report size limits plus schema/parser tests reduce resource-exhaustion exposure for those inputs, but do not prove all inputs safe; local SQLite history is bounded to a 512 MiB database-file cap, a 256 MiB aggregate report-JSON payload cap, and 1,000 reports; transient rollback-journal overhead and underlying disk exhaustion remain environmental risks.
- **Sensitive report disclosure:** paths, process names and network metadata may be private. Local-first defaults and opt-in export redaction help; users must inspect redacted outputs before sharing.
- **Environment contamination:** a candidate application could access host resources if the operator runs it outside isolation. The observer itself is not a sandbox.
- **False attribution:** external network behavior, runtime noise or environment differences may explain an observed change. Findings are review leads, not maliciousness verdicts.
- **Tampering:** a local actor can edit JSONL, reports or SQLite data. Content-derived IDs detect some accidental changes when revalidated. A detached SHA-256 sidecar detects report changes relative to the saved manifest, but neither file is signed and both can be recomputed by an actor able to replace them.

## Explicit non-goals

Driftglass does not provide a containment boundary, automatically block/delete/remediate an application, silently elevate privileges, launch targets when opening reports, protect a compromised kernel/hypervisor, or prove software safety. No local API currently exists, so API access-control validation is not applicable to this version; introducing one requires a separate threat review before implementation.

## Security validation still needed

- Controlled, real Linux workloads with expected event sets and measured misses.
- Testing under restricted procfs permissions and namespace configurations.
- Adversarial report/JSONL fuzzing, SQLite history size bounds, and controlled real-workload coverage measurements.
- Independent review of HTML export escaping and browser rendering paths.
- Dependency and release-artifact integrity review.
