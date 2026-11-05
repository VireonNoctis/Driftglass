import Foundation

struct ProcessRecord: Equatable {
    let pid: Int32
    let parentPID: Int32
    let executable: String
    let startTimeMicroseconds: UInt64

    var identity: String {
        "\(pid):\(startTimeMicroseconds)"
    }
}

enum ProcessTree {
    static func scoped(_ records: [ProcessRecord], targetPID: Int32) -> [ProcessRecord] {
        var selected: Set<Int32> = [targetPID]
        var changed = true

        while changed {
            changed = false
            for record in records where !selected.contains(record.pid) && selected.contains(record.parentPID) {
                selected.insert(record.pid)
                changed = true
            }
        }

        return records
            .filter { selected.contains($0.pid) }
            .sorted { ($0.pid, $0.startTimeMicroseconds) < ($1.pid, $1.startTimeMicroseconds) }
    }
}
