import Foundation
import XCTest
@testable import DriftglassMacObserver

final class EventOutputTests: XCTestCase {
    func testEventOutputIsPrivateAndNeverReplacesExistingPaths() throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("driftglass-event-output-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
        defer { try? FileManager.default.removeItem(at: directory) }

        let output = directory.appendingPathComponent("events.jsonl")
        let events: [[String: Any]] = [["event_id": "original", "category": "coverage"]]
        try writeEvents(events, to: output)

        let attributes = try FileManager.default.attributesOfItem(atPath: output.path)
        XCTAssertEqual((attributes[.posixPermissions] as? NSNumber)?.intValue, 0o600)
        let originalData = try Data(contentsOf: output)

        XCTAssertThrowsError(try writeEvents([["event_id": "replacement"]], to: output))
        XCTAssertEqual(try Data(contentsOf: output), originalData)

        let danglingLink = directory.appendingPathComponent("dangling-link.jsonl")
        let missingTarget = directory.appendingPathComponent("missing-target.jsonl")
        try FileManager.default.createSymbolicLink(
            atPath: danglingLink.path,
            withDestinationPath: missingTarget.path
        )

        XCTAssertThrowsError(try writeEvents(events, to: danglingLink))
        XCTAssertEqual(
            try FileManager.default.destinationOfSymbolicLink(atPath: danglingLink.path),
            missingTarget.path
        )
        XCTAssertFalse(FileManager.default.fileExists(atPath: missingTarget.path))
    }
}
