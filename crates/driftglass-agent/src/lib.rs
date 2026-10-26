//! Linux /proc sampling primitives and the versioned Driftglass event record.

use serde::Serialize;
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io;
use std::net::{Ipv4Addr, Ipv6Addr};
use std::path::Path;
use time::format_description::well_known::Rfc3339;
use time::OffsetDateTime;

pub const COLLECTOR_VERSION: &str = "driftglass-agent/0.1.0";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcessInfo {
    pub pid: u32,
    pub parent_pid: u32,
    pub name: String,
    pub start_time_ticks: u64,
}

#[derive(Debug, Default)]
pub struct ProcessScan {
    pub processes: BTreeMap<u32, ProcessInfo>,
    pub permission_denied: u64,
    pub read_errors: u64,
    pub malformed_rows: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SocketInfo {
    pub key: String,
    pub inode: u64,
    pub family: &'static str,
    pub local_address: String,
    pub local_port: u16,
    pub remote_address: String,
    pub remote_port: u16,
    pub state: String,
}

#[derive(Debug, Default)]
pub struct NetworkScan {
    pub sockets: BTreeMap<String, SocketInfo>,
    pub table_errors: u64,
    pub malformed_rows: u64,
}

#[derive(Debug, Default)]
pub struct SocketOwnerScan {
    pub inode_to_pid: BTreeMap<u64, u32>,
    /// Socket inodes observed through file descriptors owned by multiple PIDs.
    pub ambiguous_inodes: BTreeSet<u64>,
    pub permission_denied: u64,
    pub read_errors: u64,
}

#[derive(Debug, Serialize)]
pub struct EventRecord {
    schema_version: u8,
    event_id: String,
    timestamp: String,
    category: String,
    action: String,
    source: EventSource,
    #[serde(skip_serializing_if = "Option::is_none")]
    process: Option<ProcessContext>,
    #[serde(skip_serializing_if = "Option::is_none")]
    resource: Option<ResourceContext>,
    #[serde(skip_serializing_if = "Option::is_none")]
    outcome: Option<String>,
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    attributes: BTreeMap<String, Value>,
    collection_quality: String,
}

#[derive(Debug, Serialize)]
struct EventSource {
    collector: String,
    event_ref: String,
}

#[derive(Debug, Serialize, Clone)]
pub struct ProcessContext {
    pid: u32,
    parent_pid: u32,
    executable: String,
    #[serde(skip)]
    start_time_ticks: Option<u64>,
}

#[derive(Debug, Serialize)]
pub struct ResourceContext {
    kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    identifier: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    address: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    port: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    protocol: Option<String>,
}

impl From<&ProcessInfo> for ProcessContext {
    fn from(process: &ProcessInfo) -> Self {
        Self {
            pid: process.pid,
            parent_pid: process.parent_pid,
            executable: process.name.clone(),
            start_time_ticks: Some(process.start_time_ticks),
        }
    }
}

impl From<&SocketInfo> for ResourceContext {
    fn from(socket: &SocketInfo) -> Self {
        Self {
            kind: "network".to_owned(),
            identifier: Some(format!("{}:{}", socket.local_address, socket.local_port)),
            address: Some(socket.remote_address.clone()),
            port: Some(socket.remote_port),
            protocol: Some("tcp".to_owned()),
        }
    }
}

pub fn event_record(
    sequence: u64,
    category: &str,
    action: &str,
    event_ref: String,
    process: Option<ProcessContext>,
    resource: Option<ResourceContext>,
    collection_quality: &str,
    mut attributes: BTreeMap<String, Value>,
) -> Result<EventRecord, String> {
    if let Some(process) = process.as_ref() {
        if let Some(start_time_ticks) = process.start_time_ticks {
            attributes.insert(
                "process_start_time_ticks".to_owned(),
                Value::from(start_time_ticks),
            );
        }
    }

    let now = OffsetDateTime::now_utc();
    let timestamp = now
        .format(&Rfc3339)
        .map_err(|error| format!("could not format event timestamp: {error}"))?;

    Ok(EventRecord {
        schema_version: 1,
        event_id: format!("evt-{:x}-{:x}", now.unix_timestamp_nanos(), sequence),
        timestamp,
        category: category.to_owned(),
        action: action.to_owned(),
        source: EventSource {
            collector: COLLECTOR_VERSION.to_owned(),
            event_ref,
        },
        process,
        resource,
        outcome: None,
        attributes,
        collection_quality: collection_quality.to_owned(),
    })
}

/// Parse process identity and parent information from one procfs stat record.
/// Command-line arguments and process environment are deliberately not read.
pub fn parse_process_stat(record: &str) -> Option<ProcessInfo> {
    let open_paren = record.find('(')?;
    let close_paren = record.rfind(')')?;
    if close_paren <= open_paren {
        return None;
    }

    let pid = record[..open_paren].trim().parse::<u32>().ok()?;
    let name = record[open_paren + 1..close_paren].to_owned();
    let fields: Vec<&str> = record[close_paren + 1..].split_whitespace().collect();

    // The fields after comm start at stat field 3 (state). ppid is field 4
    // (index 1) and starttime is field 22 (index 19).
    let parent_pid = fields.get(1)?.parse::<u32>().ok()?;
    let start_time_ticks = fields.get(19)?.parse::<u64>().ok()?;

    Some(ProcessInfo {
        pid,
        parent_pid,
        name,
        start_time_ticks,
    })
}

pub fn read_process_snapshot(proc_root: &Path) -> io::Result<ProcessScan> {
    let mut scan = ProcessScan::default();
    for entry in fs::read_dir(proc_root)? {
        let entry = match entry {
            Ok(entry) => entry,
            Err(_) => {
                scan.read_errors += 1;
                continue;
            }
        };
        let Some(pid) = entry.file_name().to_str().and_then(|name| name.parse::<u32>().ok()) else {
            continue;
        };

        let stat_path = proc_root.join(pid.to_string()).join("stat");
        let record = match fs::read_to_string(stat_path) {
            Ok(record) => record,
            Err(error) if error.kind() == io::ErrorKind::NotFound => continue,
            Err(error) if error.kind() == io::ErrorKind::PermissionDenied => {
                scan.permission_denied += 1;
                continue;
            }
            Err(_) => {
                scan.read_errors += 1;
                continue;
            }
        };

        match parse_process_stat(&record) {
            Some(process) => {
                scan.processes.insert(process.pid, process);
            }
            None => scan.malformed_rows += 1,
        }
    }
    Ok(scan)
}

/// Find the target process and its current descendants using parent PID metadata.
pub fn process_scope(
    snapshot: &BTreeMap<u32, ProcessInfo>,
    root_pid: u32,
) -> BTreeMap<u32, ProcessInfo> {
    let mut scope = BTreeMap::new();
    if let Some(root) = snapshot.get(&root_pid) {
        scope.insert(root_pid, root.clone());
    } else {
        return scope;
    }

    extend_descendants(snapshot, &mut scope);
    scope
}

/// Retain known processes that still have the same identity after a child is
/// reparented because the target exited. Descendants are expanded afterwards.
pub fn retain_known_processes(
    snapshot: &BTreeMap<u32, ProcessInfo>,
    current_scope: &mut BTreeMap<u32, ProcessInfo>,
    previous_scope: &BTreeMap<u32, ProcessInfo>,
) {
    for (pid, previous) in previous_scope {
        if let Some(current) = snapshot.get(pid) {
            if current.start_time_ticks == previous.start_time_ticks {
                current_scope.insert(*pid, current.clone());
            }
        }
    }
    extend_descendants(snapshot, current_scope);
}

fn extend_descendants(
    snapshot: &BTreeMap<u32, ProcessInfo>,
    scope: &mut BTreeMap<u32, ProcessInfo>,
) {
    loop {
        let known: std::collections::BTreeSet<u32> = scope.keys().copied().collect();
        let mut added = false;
        for process in snapshot.values() {
            if known.contains(&process.parent_pid) && !scope.contains_key(&process.pid) {
                scope.insert(process.pid, process.clone());
                added = true;
            }
        }
        if !added {
            break;
        }
    }
}

fn record_socket_owner(scan: &mut SocketOwnerScan, inode: u64, pid: u32) {
    match scan.inode_to_pid.get(&inode) {
        Some(owner_pid) if *owner_pid != pid => {
            scan.ambiguous_inodes.insert(inode);
        }
        Some(_) => {}
        None => {
            scan.inode_to_pid.insert(inode, pid);
        }
    }
}

pub fn read_socket_owners(proc_root: &Path, pids: &[u32]) -> SocketOwnerScan {
    let mut scan = SocketOwnerScan::default();
    for pid in pids {
        let fd_path = proc_root.join(pid.to_string()).join("fd");
        let entries = match fs::read_dir(fd_path) {
            Ok(entries) => entries,
            Err(error) if error.kind() == io::ErrorKind::NotFound => continue,
            Err(error) if error.kind() == io::ErrorKind::PermissionDenied => {
                scan.permission_denied += 1;
                continue;
            }
            Err(_) => {
                scan.read_errors += 1;
                continue;
            }
        };

        for entry in entries {
            let entry = match entry {
                Ok(entry) => entry,
                Err(_) => {
                    scan.read_errors += 1;
                    continue;
                }
            };
            let target = match fs::read_link(entry.path()) {
                Ok(target) => target,
                Err(error) if error.kind() == io::ErrorKind::NotFound => continue,
                Err(error) if error.kind() == io::ErrorKind::PermissionDenied => {
                    scan.permission_denied += 1;
                    continue;
                }
                Err(_) => {
                    scan.read_errors += 1;
                    continue;
                }
            };
            let target = target.to_string_lossy();
            if let Some(inode) = target
                .strip_prefix("socket:[")
                .and_then(|value| value.strip_suffix(']'))
                .and_then(|value| value.parse::<u64>().ok())
            {
                record_socket_owner(&mut scan, inode, *pid);
            }
        }
    }
    scan
}

pub fn read_tcp_snapshot(proc_root: &Path, network_namespace_pid: u32) -> NetworkScan {
    let mut scan = NetworkScan::default();
    let net_root = proc_root.join(network_namespace_pid.to_string()).join("net");
    for (relative_path, ipv6) in [("tcp", false), ("tcp6", true)] {
        let path = net_root.join(relative_path);
        let contents = match fs::read_to_string(path) {
            Ok(contents) => contents,
            Err(_) => {
                scan.table_errors += 1;
                continue;
            }
        };

        for row in contents.lines().skip(1).filter(|row| !row.trim().is_empty()) {
            match parse_tcp_row(row, ipv6) {
                Some(socket) => {
                    scan.sockets.insert(socket.key.clone(), socket);
                }
                None => scan.malformed_rows += 1,
            }
        }
    }
    scan
}

fn parse_tcp_row(row: &str, ipv6: bool) -> Option<SocketInfo> {
    let fields: Vec<&str> = row.split_whitespace().collect();
    if fields.len() < 10 {
        return None;
    }
    let (local_address, local_port) = parse_endpoint(fields[1], ipv6)?;
    let (remote_address, remote_port) = parse_endpoint(fields[2], ipv6)?;
    let state_code = u8::from_str_radix(fields[3], 16).ok()?;
    let inode = fields[9].parse::<u64>().ok()?;
    let family = if ipv6 { "ipv6" } else { "ipv4" };
    let key = format!(
        "tcp:{family}:{inode}:{local_address}:{local_port}:{remote_address}:{remote_port}"
    );

    Some(SocketInfo {
        key,
        inode,
        family,
        local_address,
        local_port,
        remote_address,
        remote_port,
        state: tcp_state(state_code).to_owned(),
    })
}

fn parse_endpoint(endpoint: &str, ipv6: bool) -> Option<(String, u16)> {
    let (address, port) = endpoint.rsplit_once(':')?;
    let port = u16::from_str_radix(port, 16).ok()?;
    Some((decode_proc_address(address, ipv6)?, port))
}

pub fn decode_proc_address(value: &str, ipv6: bool) -> Option<String> {
    if !value.is_ascii() || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return None;
    }
    if !ipv6 {
        if value.len() != 8 {
            return None;
        }
        let mut octets = [0_u8; 4];
        for (index, octet) in octets.iter_mut().enumerate() {
            let offset = index * 2;
            *octet = u8::from_str_radix(&value[offset..offset + 2], 16).ok()?;
        }
        octets.reverse();
        return Some(Ipv4Addr::from(octets).to_string());
    }

    if value.len() != 32 {
        return None;
    }
    let mut octets = [0_u8; 16];
    for word_index in 0..4 {
        for byte_index in 0..4 {
            let offset = word_index * 8 + byte_index * 2;
            octets[word_index * 4 + (3 - byte_index)] =
                u8::from_str_radix(&value[offset..offset + 2], 16).ok()?;
        }
    }
    Some(Ipv6Addr::from(octets).to_string())
}

fn tcp_state(code: u8) -> &'static str {
    match code {
        0x01 => "ESTABLISHED",
        0x02 => "SYN_SENT",
        0x03 => "SYN_RECV",
        0x04 => "FIN_WAIT1",
        0x05 => "FIN_WAIT2",
        0x06 => "TIME_WAIT",
        0x07 => "CLOSE",
        0x08 => "CLOSE_WAIT",
        0x09 => "LAST_ACK",
        0x0a => "LISTEN",
        0x0b => "CLOSING",
        0x0c => "NEW_SYN_RECV",
        _ => "UNKNOWN",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn event_records_include_version_timestamp_and_source_reference() {
        let record = event_record(
            1,
            "coverage",
            "capture_started",
            "capture:start".to_owned(),
            None,
            None,
            "complete",
            BTreeMap::new(),
        )
        .expect("synthetic event record");
        let value = serde_json::to_value(record).expect("serializable event record");
        assert_eq!(value["schema_version"], 1);
        assert_eq!(value["source"]["event_ref"], "capture:start");
        assert_eq!(value["source"]["collector"], COLLECTOR_VERSION);
        assert!(value["timestamp"].as_str().unwrap().contains('T'));
    }

    #[test]
    fn parses_process_stat_with_spaces_and_parentheses_in_name() {
        let record = "123 (worker (pool) process) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 98765";
        let parsed = parse_process_stat(record).expect("valid synthetic stat record");
        assert_eq!(parsed.pid, 123);
        assert_eq!(parsed.parent_pid, 1);
        assert_eq!(parsed.name, "worker (pool) process");
        assert_eq!(parsed.start_time_ticks, 98765);
    }

    #[test]
    fn decodes_procfs_ipv4_little_endian_address() {
        assert_eq!(decode_proc_address("0100007F", false).as_deref(), Some("127.0.0.1"));
    }

    #[test]
    fn decodes_procfs_ipv6_little_endian_words() {
        assert_eq!(
            decode_proc_address("00000000000000000000000001000000", true).as_deref(),
            Some("::1")
        );
    }

    #[test]
    fn builds_target_and_child_process_scope() {
        let root = ProcessInfo { pid: 10, parent_pid: 1, name: "target".into(), start_time_ticks: 1 };
        let child = ProcessInfo { pid: 11, parent_pid: 10, name: "child".into(), start_time_ticks: 2 };
        let unrelated = ProcessInfo { pid: 20, parent_pid: 1, name: "unrelated".into(), start_time_ticks: 3 };
        let snapshot = BTreeMap::from([(10, root), (11, child), (20, unrelated)]);
        let scope = process_scope(&snapshot, 10);
        assert_eq!(scope.keys().copied().collect::<Vec<_>>(), vec![10, 11]);
    }

    #[test]
    fn rejects_non_ascii_proc_address_without_panicking() {
        assert_eq!(decode_proc_address("éééé", false), None);
    }

    #[test]
    fn flags_socket_inode_referenced_by_multiple_processes() {
        let mut scan = SocketOwnerScan::default();
        record_socket_owner(&mut scan, 123, 10);
        record_socket_owner(&mut scan, 123, 11);
        record_socket_owner(&mut scan, 123, 11);

        assert_eq!(scan.inode_to_pid.get(&123), Some(&10));
        assert_eq!(scan.ambiguous_inodes, BTreeSet::from([123]));
    }

    #[test]
    fn repeated_socket_reference_from_same_process_is_not_ambiguous() {
        let mut scan = SocketOwnerScan::default();
        record_socket_owner(&mut scan, 123, 10);
        record_socket_owner(&mut scan, 123, 10);

        assert_eq!(scan.inode_to_pid.get(&123), Some(&10));
        assert!(scan.ambiguous_inodes.is_empty());
    }

    #[test]
    fn parses_tcp_metadata_row() {
        let row = "  0: 0100007F:1F90 08080808:01BB 01 00000000:00000000 00:00000000 00000000 1000 0 12345";
        let parsed = parse_tcp_row(row, false).expect("valid synthetic /proc/net/tcp row");
        assert_eq!(parsed.local_address, "127.0.0.1");
        assert_eq!(parsed.local_port, 8080);
        assert_eq!(parsed.remote_address, "8.8.8.8");
        assert_eq!(parsed.remote_port, 443);
        assert_eq!(parsed.state, "ESTABLISHED");
        assert_eq!(parsed.inode, 12345);
    }

    #[test]
    fn serializes_process_identity_start_ticks_as_metadata_not_a_process_field() {
        let process = ProcessInfo {
            pid: 123,
            parent_pid: 1,
            name: "synthetic-child".to_owned(),
            start_time_ticks: 4567,
        };
        let record = event_record(
            2,
            "process",
            "process_observed",
            "proc:123".to_owned(),
            Some(ProcessContext::from(&process)),
            None,
            "complete",
            BTreeMap::new(),
        )
        .expect("event record");

        let value = serde_json::to_value(record).expect("serializable");
        assert_eq!(value["process"]["pid"], 123);
        assert_eq!(value["process"]["parent_pid"], 1);
        assert!(value["process"].get("start_time_ticks").is_none());
        assert_eq!(value["attributes"]["process_start_time_ticks"], 4567);
    }

}
