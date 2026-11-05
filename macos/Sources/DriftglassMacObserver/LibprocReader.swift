import Darwin
import Foundation

struct ProcessSnapshot {
    let processes: [ProcessRecord]
    let inaccessibleCount: Int
}

enum LibprocReaderError: Error, CustomStringConvertible {
    case processListUnavailable
    case processListTruncated
    case targetNotVisible(Int32)

    var description: String {
        switch self {
        case .processListUnavailable:
            return "macOS process enumeration was unavailable"
        case .processListTruncated:
            return "macOS process list exceeded the collector buffer"
        case .targetNotVisible(let pid):
            return "target PID \(pid) was not visible to the current process"
        }
    }
}

struct LibprocReader {
    private let maximumProcesses = 32768

    func snapshot(targetPID: Int32) throws -> ProcessSnapshot {
        var pids = [pid_t](repeating: 0, count: maximumProcesses)
        let bytes = pids.withUnsafeMutableBytes { buffer in
            proc_listpids(UInt32(PROC_ALL_PIDS), 0, buffer.baseAddress, Int32(buffer.count))
        }
        guard bytes > 0 else { throw LibprocReaderError.processListUnavailable }
        if Int(bytes) >= pids.count * MemoryLayout<pid_t>.stride {
            throw LibprocReaderError.processListTruncated
        }

        let count = min(Int(bytes) / MemoryLayout<pid_t>.stride, pids.count)
        var records: [ProcessRecord] = []
        var inaccessible = 0
        records.reserveCapacity(count)

        for pid in pids.prefix(count) where pid > 0 {
            var info = proc_bsdinfo()
            let infoBytes = proc_pidinfo(
                pid,
                Int32(PROC_PIDTBSDINFO),
                0,
                &info,
                Int32(MemoryLayout<proc_bsdinfo>.size)
            )
            guard Int(infoBytes) == MemoryLayout<proc_bsdinfo>.size else {
                inaccessible += 1
                continue
            }

            var pathBuffer = [CChar](repeating: 0, count: Int(MAXPATHLEN))
            let pathLength = pathBuffer.withUnsafeMutableBufferPointer { buffer in
                proc_pidpath(pid, buffer.baseAddress, UInt32(buffer.count))
            }
            guard pathLength > 0 else {
                inaccessible += 1
                continue
            }

            let executable = String(cString: pathBuffer)
            let start = UInt64(info.pbi_start_tvsec) * 1_000_000 + UInt64(info.pbi_start_tvusec)
            records.append(ProcessRecord(
                pid: Int32(info.pbi_pid),
                parentPID: Int32(info.pbi_ppid),
                executable: executable,
                startTimeMicroseconds: start
            ))
        }

        if !records.contains(where: { $0.pid == targetPID }) {
            throw LibprocReaderError.targetNotVisible(targetPID)
        }
        return ProcessSnapshot(
            processes: ProcessTree.scoped(records, targetPID: targetPID),
            inaccessibleCount: inaccessible
        )
    }
}
