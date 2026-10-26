# Event record schema v1

Driftglass event records are JSON objects written one per line in UTF-8 JSONL files. The authoritative contract is **schemas/events/event-v1.schema.json** (see [the schema file](../schemas/events/event-v1.schema.json)). The Python **load_jsonl** utility validates each record against that schema before returning it.

## Required fields

| Field | Meaning |
| --- | --- |
| **schema_version** | Integer version of this event contract; v1 requires **1**. |
| **event_id** | Unique normalized event identifier within one input stream. |
| **timestamp** | Date-time string with an explicit UTC offset. |
| **category** | One of **process**, **network**, **file**, **system**, or **coverage**. |
| **action** | Concise action name supplied by the collector/normalizer. |
| **source.collector** | Collector identifier, ideally including its version. |
| **source.event_ref** | Reference to the source event or record. |
| **collection_quality** | Quality of this individual observation: **complete**, **partial**, or **unknown**. This is not a statement that the run's overall event coverage is complete. |

Optional **process**, **resource**, **outcome**, and scalar **attributes** fields preserve useful context. The schema rejects unknown top-level and nested process/resource/source fields so misspellings are visible; source-specific scalar metadata belongs in **attributes**. Attributes are size-bounded, but producers must still avoid putting packet payloads or file contents in the event stream.

Paths, hostnames, addresses, executable names, and command lines may be sensitive. **process.command_line** is optional and should only be collected when a capture profile requires it. The schema does not enable collection; it only describes data that a collector may emit.

## Failure semantics

The Python reader fails with a line-specific validation error on malformed JSON, blank lines, invalid records, duplicate event IDs, unreadable files, or unreadable/invalid schemas. It also enforces a 64 MiB total stream limit, a 1 MiB per-line limit, and a 250,000-record limit. These are parser resource bounds, not event-loss guarantees; valid larger captures must be split into smaller, independently validated streams before they can be compared. It also rejects an empty event file: without a run manifest and coverage information, an empty stream cannot distinguish a quiet run from a failed or inactive collector. This is intentionally conservative, not a claim that every valid experiment must observe application activity.

A valid event stream alone does not establish complete collection coverage. The future run manifest must report enabled event sources, permissions, errors, dropped events, and unsupported categories separately. An observation categorized as **coverage** is available for explicit collector status events, but this contract does not itself implement a collector or a run manifest.

## Compatibility

Changes that alter required fields, meanings, category values, or validation semantics should use a new event schema version and include migration or compatibility tests. This v1 contract is an initial prototype and has not yet been validated against a real OS collector.
