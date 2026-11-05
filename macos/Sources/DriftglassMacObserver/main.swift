import Darwin
import Foundation

private struct Options {
    let pid: Int32
    let durationSeconds: Int
    let intervalMilliseconds: Int
    let output: URL

    static func parse(_ arguments: [String]) throws -> Options? {
        if arguments == ["--help"] || arguments.isEmpty { return nil }
        var values: [String: String] = [:]
        var index = 0
        let valueOptions: Set<String> = ["--pid", "--duration-seconds", "--interval-ms", "--output"]
        while index < arguments.count {
            let option = arguments[index]
            guard valueOptions.contains(option), index + 1 < arguments.count, values[option] == nil else {
                throw UsageError.invalidArguments
            }
            values[option] = arguments[index + 1]
            index += 2
        }

        guard let pidText = values["--pid"], let pid = Int32(pidText), pid > 0,
              let durationText = values["--duration-seconds"], let duration = Int(durationText),
              (1...3600).contains(duration),
              let intervalText = values["--interval-ms"], let interval = Int(intervalText),
              (50...60000).contains(interval),
              let outputText = values["--output"], !outputText.isEmpty else {
            throw UsageError.invalidArguments
        }
        return Options(
            pid: pid,
            durationSeconds: duration,
            intervalMilliseconds: interval,
            output: URL(fileURLWithPath: outputText).standardizedFileURL
        )
    }
}

private enum UsageError: Error {
    case invalidArguments
}

private func usage() {
    print("""
    Driftglass experimental macOS process observer

    Usage:
      driftglass-macos-observer --pid PID --duration-seconds 30 --interval-ms 1000 --output EVENTS.jsonl

    The observer samples process identities for an already-running PID and its visible descendants.
    It does not launch or control the target application. Network, file, system and persistence
    telemetry are not collected by this source. Collection gaps are emitted as coverage events.
    """)
}

private func timestamp() -> String {
    ISO8601DateFormatter().string(from: Date())
}

private func makeEvent(
    id: String,
    category: String,
    action: String,
    eventRef: String,
    process: ProcessRecord? = nil,
    attributes: [String: Any] = [:],
    quality: String = "complete"
) -> [String: Any] {
    var event: [String: Any] = [
        "schema_version": 1,
        "event_id": id,
        "timestamp": timestamp(),
        "category": category,
        "action": action,
        "source": [
            "collector": "macos-libproc-poll-v1",
            "event_ref": eventRef
        ],
        "collection_quality": quality,
        "attributes": attributes
    ]
    if let process {
        event["process"] = [
            "pid": Int(process.pid),
            "parent_pid": Int(process.parentPID),
            "executable": process.executable
        ]
        event["resource"] = [
            "kind": "process",
            "identifier": process.executable
        ]
    }
    return event
}

private func processEvent(_ action: String, _ process: ProcessRecord, sequence: Int) -> [String: Any] {
    makeEvent(
        id: UUID().uuidString.lowercased(),
        category: "process",
        action: action,
        eventRef: "libproc:\(process.pid):\(process.startTimeMicroseconds):\(sequence)",
        process: process,
        attributes: ["process_start_time_usec": process.startTimeMicroseconds]
    )
}

private func collect(_ options: Options) throws -> [[String: Any]] {
    let reader = LibprocReader()
    let start = Date()
    let end = start.addingTimeInterval(TimeInterval(options.durationSeconds))
    var events: [[String: Any]] = [
        makeEvent(
            id: UUID().uuidString.lowercased(),
            category: "coverage",
            action: "capture_started",
            eventRef: "capture:start",
            attributes: ["target_pid": Int(options.pid)]
        )
    ]
    var captureStatus = "completed"
    var statusReason: String?
    var sequence = 0

    do {
        var snapshot = try reader.snapshot(targetPID: options.pid)
        if snapshot.inaccessibleCount > 0 {
            events.append(makeEvent(
                id: UUID().uuidString.lowercased(),
                category: "coverage",
                action: "coverage_gap",
                eventRef: "libproc:initial-snapshot",
                attributes: ["inaccessible_process_count": snapshot.inaccessibleCount],
                quality: "partial"
            ))
        }

        var previous = Dictionary(uniqueKeysWithValues: snapshot.processes.map { ($0.identity, $0) })
        for process in snapshot.processes {
            events.append(processEvent("process_observed", process, sequence: sequence))
            sequence += 1
        }

        while Date() < end && events.count < 249_998 {
            let remaining = end.timeIntervalSinceNow
            Thread.sleep(forTimeInterval: min(remaining, Double(options.intervalMilliseconds) / 1000.0))
            if Date() >= end { break }

            snapshot = try reader.snapshot(targetPID: options.pid)
            if snapshot.inaccessibleCount > 0 {
                events.append(makeEvent(
                    id: UUID().uuidString.lowercased(),
                    category: "coverage",
                    action: "coverage_gap",
                    eventRef: "libproc:snapshot:\(sequence)",
                    attributes: ["inaccessible_process_count": snapshot.inaccessibleCount],
                    quality: "partial"
                ))
            }
            let current = Dictionary(uniqueKeysWithValues: snapshot.processes.map { ($0.identity, $0) })
            let oldKeys = Set(previous.keys)
            let newKeys = Set(current.keys)

            for key in newKeys.subtracting(oldKeys).sorted() {
                if let process = current[key] {
                    events.append(processEvent("process_started", process, sequence: sequence))
                    sequence += 1
                }
            }
            for key in oldKeys.subtracting(newKeys).sorted() {
                if let process = previous[key] {
                    events.append(processEvent("process_exited", process, sequence: sequence))
                    sequence += 1
                }
            }
            previous = current
        }

        if events.count >= 249_998 {
            captureStatus = "incomplete"
            statusReason = "event record limit reached; remaining process changes were not collected"
            events.append(makeEvent(
                id: UUID().uuidString.lowercased(),
                category: "coverage",
                action: "events_dropped",
                eventRef: "libproc:event-limit",
                attributes: ["maximum_records": 250000],
                quality: "partial"
            ))
        }
    } catch {
        captureStatus = "incomplete"
        statusReason = String(describing: error)
        events.append(makeEvent(
            id: UUID().uuidString.lowercased(),
            category: "coverage",
            action: "collector_error",
            eventRef: "libproc:error",
            attributes: ["reason": String(describing: error)],
            quality: "partial"
        ))
    }

    var finishAttributes: [String: Any] = ["capture_status": captureStatus]
    if let statusReason { finishAttributes["status_reason"] = statusReason }
    events.append(makeEvent(
        id: UUID().uuidString.lowercased(),
        category: "coverage",
        action: "capture_finished",
        eventRef: "capture:finish",
        attributes: finishAttributes,
        quality: captureStatus == "completed" ? "complete" : "partial"
    ))
    return events
}

private func writeEvents(_ events: [[String: Any]], to output: URL) throws {
    let fileManager = FileManager.default
    guard fileManager.fileExists(atPath: output.deletingLastPathComponent().path) else {
        throw NSError(domain: "DriftglassMacObserver", code: 1, userInfo: [
            NSLocalizedDescriptionKey: "output directory does not exist"
        ])
    }
    guard !fileManager.fileExists(atPath: output.path) else {
        throw NSError(domain: "DriftglassMacObserver", code: 2, userInfo: [
            NSLocalizedDescriptionKey: "output already exists; choose a new path"
        ])
    }

    let temporary = output.appendingPathExtension("tmp-\(UUID().uuidString)")
    do {
        guard fileManager.createFile(atPath: temporary.path, contents: nil) else {
            throw NSError(domain: "DriftglassMacObserver", code: 3, userInfo: [
                NSLocalizedDescriptionKey: "could not create temporary event file"
            ])
        }
        let handle = try FileHandle(forWritingTo: temporary)
        defer { try? handle.close() }
        for event in events {
            let data = try JSONSerialization.data(withJSONObject: event, options: [.sortedKeys, .fragmentsAllowed])
            try handle.write(contentsOf: data)
            try handle.write(contentsOf: Data([0x0A]))
        }
        try handle.synchronize()
        try fileManager.moveItem(at: temporary, to: output)
    } catch {
        try? fileManager.removeItem(at: temporary)
        throw error
    }
}

@main
struct DriftglassMacObserver {
    static func main() {
        do {
            guard let options = try Options.parse(Array(CommandLine.arguments.dropFirst())) else {
                usage()
                return
            }
            let events = try collect(options)
            try writeEvents(events, to: options.output)
            let status = events.last?["attributes"] as? [String: Any]
            let captureStatus = status?["capture_status"] as? String ?? "unknown"
            print("capture_status=\(captureStatus) events=\(events.count) output=\(options.output.path)")
            if captureStatus != "completed" { exit(1) }
        } catch UsageError.invalidArguments {
            usage()
            fputs("invalid arguments\n", stderr)
            exit(2)
        } catch {
            fputs("driftglass-macos-observer: \(error)\n", stderr)
            exit(2)
        }
    }
}
