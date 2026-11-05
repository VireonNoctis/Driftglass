import XCTest
@testable import DriftglassMacObserver

final class ProcessTreeTests: XCTestCase {
    func testIncludesTargetAndVisibleDescendantsOnly() {
        let records = [
            ProcessRecord(pid: 10, parentPID: 1, executable: "/app/main", startTimeMicroseconds: 100),
            ProcessRecord(pid: 11, parentPID: 10, executable: "/app/worker", startTimeMicroseconds: 110),
            ProcessRecord(pid: 12, parentPID: 11, executable: "/app/helper", startTimeMicroseconds: 120),
            ProcessRecord(pid: 20, parentPID: 1, executable: "/other/app", startTimeMicroseconds: 200)
        ]

        let scoped = ProcessTree.scoped(records, targetPID: 10)

        XCTAssertEqual(scoped.map(\.pid), [10, 11, 12])
    }

    func testDoesNotTreatReusedPidAsSameProcess() {
        let first = ProcessRecord(pid: 11, parentPID: 10, executable: "/app/worker", startTimeMicroseconds: 110)
        let replacement = ProcessRecord(pid: 11, parentPID: 10, executable: "/app/worker", startTimeMicroseconds: 210)

        XCTAssertNotEqual(first.identity, replacement.identity)
    }
}
