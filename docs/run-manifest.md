# Run manifest v1

The authoritative contract is **schemas/manifests/run-manifest-v1.schema.json**. The Python **load_manifest** utility validates the JSON structure and cross-field invariants. The manifest describes one baseline or candidate run; it does not launch an application or collect OS events.

## What it records

- **Identity:** schema version, run ID, experiment ID, and whether the run is the baseline or candidate.
- **Application:** name, version, package identifier when available, and the artifact's SHA-256. If the artifact hash cannot be obtained, the field must be present as **null** rather than silently omitted.
- **Environment:** OS, version/build, architecture, and a snapshot identifier when available.
- **Capture:** status, start/end times, test scenario, and capture profile.
- **Coverage:** the configured event sources, each source's state and event categories, unavailable categories, dropped-event count, and collection errors.

The manifest should be produced by the future run coordinator. In this prototype it is a validated data contract only; there is no run lifecycle or collector implementation.

## Consistency rules

The loader rejects a completed run without an end time, a running run with an end time, an end time preceding the start time, and failed/incomplete terminal captures without a reason. Event-source names must be unique. Every category attributed to a non-active source must also be declared unavailable in the coverage summary.

A run may end with status **completed** and still have coverage gaps or dropped events. Run status describes the capture lifecycle; it is not a declaration that observation was complete. Consumers must inspect coverage, errors, and dropped-event counts separately. An empty event stream, absent collector, denied permission, or unsupported event category must never be interpreted as proof of no activity.

## Privacy and limitations

Manifests and their error strings can contain sensitive local environment details. Use synthetic data in tests and review content before export. The v1 contract has only been tested against synthetic fixtures; it has not been validated against a real OS collector.
