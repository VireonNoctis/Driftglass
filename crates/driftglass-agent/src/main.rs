use driftglass_agent::{
    event_record, process_scope, read_process_snapshot, read_socket_owners, read_tcp_snapshot,
    retain_known_processes, EventRecord, ProcessContext, ProcessInfo, ResourceContext, SocketInfo,
};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::env;
use std::fs::OpenOptions;
use std::io::{self, BufWriter, Write};
use std::path::{Path, PathBuf};
#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;
use std::thread;
use std::time::{Duration, Instant};

const DEFAULT_DURATION_SECONDS: u64 = 30;
const DEFAULT_INTERVAL_MS: u64 = 250;
const MAX_DURATION_SECONDS: u64 = 3600;
const MIN_INTERVAL_MS: u64 = 100;
const MAX_INTERVAL_MS: u64 = 10_000;

#[derive(Debug)]
struct Args {
    pid: u32,
    duration: Duration,
    interval: Duration,
    output: Option<PathBuf>,
}

#[derive(Default)]
struct Totals {
    process_permission_denied: u64,
    process_read_errors: u64,
    process_malformed_rows: u64,
    fd_permission_denied: u64,
    fd_read_errors: u64,
    ambiguous_socket_owner_observations: u64,
    network_table_errors: u64,
    network_malformed_rows: u64,
}

impl Totals {
    fn has_gaps(&self) -> bool {
        self.process_permission_denied > 0
            || self.process_read_errors > 0
            || self.process_malformed_rows > 0
            || self.fd_permission_denied > 0
            || self.fd_read_errors > 0
            || self.ambiguous_socket_owner_observations > 0
            || self.network_table_errors > 0
            || self.network_malformed_rows > 0
    }

    fn add_process(&mut self, scan: &driftglass_agent::ProcessScan) {
        self.process_permission_denied += scan.permission_denied;
        self.process_read_errors += scan.read_errors;
        self.process_malformed_rows += scan.malformed_rows;
    }

    fn add_owners(&mut self, scan: &driftglass_agent::SocketOwnerScan) {
        self.fd_permission_denied += scan.permission_denied;
        self.fd_read_errors += scan.read_errors;
        self.ambiguous_socket_owner_observations += scan.ambiguous_inodes.len() as u64;
    }

    fn add_network(&mut self, scan: &driftglass_agent::NetworkScan) {
        self.network_table_errors += scan.table_errors;
        self.network_malformed_rows += scan.malformed_rows;
    }
}

#[derive(Clone)]
struct OwnedSocket {
    socket: SocketInfo,
    owner: ProcessInfo,
    ambiguous_owner: bool,
}

fn main() {
    let arguments: Vec<String> = env::args().skip(1).collect();
    if arguments.len() == 1 && (arguments[0] == "--help" || arguments[0] == "-h") {
        println!("usage: driftglass-agent --pid PID [--duration-seconds 30] [--interval-ms 250] [--output NEW_FILE]");
        return;
    }
    if let Err(error) = run(arguments.into_iter()) {
        eprintln!("driftglass-agent: {error}");
        std::process::exit(1);
    }
}

fn run(arguments: impl Iterator<Item = String>) -> Result<(), String> {
    if !cfg!(target_os = "linux") {
        return Err("this collector currently supports Linux /proc only".to_owned());
    }

    let args = parse_args(arguments)?;
    let proc_root = PathBuf::from("/proc");
    let initial_scan = read_process_snapshot(&proc_root)
        .map_err(|error| format!("cannot enumerate /proc: {error}"))?;
    let target_start_time_ticks = initial_scan
        .processes
        .get(&args.pid)
        .map(|process| process.start_time_ticks)
        .ok_or_else(|| {
            format!(
                "target PID {} was not observable; no application was launched",
                args.pid
            )
        })?;

    let output: Box<dyn Write> = match args.output {
        Some(path) => {
            let file = create_new_output_file(&path)
                .map_err(|error| {
                    format!(
                        "cannot create output file '{}': {error}; existing files are never overwritten",
                        path.display()
                    )
                })?;
            Box::new(file)
        }
        None => Box::new(io::stdout()),
    };
    let mut writer = BufWriter::new(output);

    let mut totals = Totals::default();
    totals.add_process(&initial_scan);
    let mut previous_scope = process_scope(&initial_scan.processes, args.pid);
    let initial_owners = read_socket_owners(&proc_root, &scope_pids(&previous_scope));
    totals.add_owners(&initial_owners);
    let initial_network = read_tcp_snapshot(&proc_root, args.pid);
    totals.add_network(&initial_network);
    let mut previous_sockets = owned_sockets(
        &initial_network.sockets,
        &initial_owners,
        &previous_scope,
    );
    let mut network_namespace_anchor_lost = false;

    let mut sequence = 1_u64;
    emit(
        &mut writer,
        &mut sequence,
        "coverage",
        "capture_started",
        "capture:start".to_owned(),
        None,
        None,
        "complete",
        BTreeMap::from([
            ("target_pid".to_owned(), json!(args.pid)),
            ("duration_seconds".to_owned(), json!(args.duration.as_secs())),
            ("sampling_interval_ms".to_owned(), json!(args.interval.as_millis())),
            ("scope".to_owned(), json!("target process and observed descendants")),
        ]),
    )?;

    for process in previous_scope.values() {
        emit_process_event(&mut writer, &mut sequence, "process_observed", process)?;
    }
    for owned in previous_sockets.values() {
        emit_socket_event(&mut writer, &mut sequence, "socket_present", owned, "complete")?;
    }

    let started = Instant::now();
    let deadline = started + args.duration;

    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            break;
        }
        thread::sleep(args.interval.min(remaining));
        if Instant::now() >= deadline {
            break;
        }

        let process_scan = match read_process_snapshot(&proc_root) {
            Ok(scan) => scan,
            Err(_) => {
                emit(
                    &mut writer,
                    &mut sequence,
                    "coverage",
                    "capture_incomplete",
                    "capture:process-scan-error".to_owned(),
                    None,
                    None,
                    "partial",
                    BTreeMap::from([
                        ("reason_code".to_owned(), json!("process_scan_failed")),
                        ("target_pid".to_owned(), json!(args.pid)),
                    ]),
                )?;
                return Err("process enumeration failed during capture; partial events were emitted".to_owned());
            }
        };
        totals.add_process(&process_scan);
        if target_pid_was_reused(
            &process_scan.processes,
            args.pid,
            target_start_time_ticks,
        ) {
            let observed_start_time_ticks = process_scan.processes[&args.pid].start_time_ticks;
            emit(
                &mut writer,
                &mut sequence,
                "coverage",
                "capture_incomplete",
                "capture:target-pid-reused".to_owned(),
                None,
                None,
                "partial",
                BTreeMap::from([
                    ("reason_code".to_owned(), json!("target_pid_reused")),
                    ("target_pid".to_owned(), json!(args.pid)),
                    ("expected_start_time_ticks".to_owned(), json!(target_start_time_ticks)),
                    ("observed_start_time_ticks".to_owned(), json!(observed_start_time_ticks)),
                ]),
            )?;
            writer.flush().map_err(|error| format!("cannot flush partial capture: {error}"))?;
            return Err("target PID was reused during capture; refusing to attribute events to a different process".to_owned());
        }

        let mut current_scope = process_scope(&process_scan.processes, args.pid);
        retain_known_processes(
            &process_scan.processes,
            &mut current_scope,
            &previous_scope,
        );

        for (pid, old_process) in &previous_scope {
            let unchanged_identity = current_scope
                .get(pid)
                .is_some_and(|current| current.start_time_ticks == old_process.start_time_ticks);
            if !unchanged_identity {
                emit_process_event(
                    &mut writer,
                    &mut sequence,
                    "process_no_longer_observed",
                    old_process,
                )?;
            }
        }
        for (pid, current_process) in &current_scope {
            let was_observed = previous_scope
                .get(pid)
                .is_some_and(|previous| previous.start_time_ticks == current_process.start_time_ticks);
            if !was_observed {
                emit_process_event(
                    &mut writer,
                    &mut sequence,
                    "process_observed",
                    current_process,
                )?;
            }
        }

        let current_sockets = if network_namespace_anchor_lost {
            None
        } else if let Some(network_namespace_pid) =
            network_namespace_anchor(&current_scope, args.pid)
        {
            let owners = read_socket_owners(&proc_root, &scope_pids(&current_scope));
            totals.add_owners(&owners);
            let network = read_tcp_snapshot(&proc_root, network_namespace_pid);
            totals.add_network(&network);
            Some(owned_sockets(
                &network.sockets,
                &owners,
                &current_scope,
            ))
        } else {
            network_namespace_anchor_lost = true;
            emit(
                &mut writer,
                &mut sequence,
                "coverage",
                "coverage_gap",
                "capture:network-namespace-anchor-unavailable".to_owned(),
                None,
                None,
                "partial",
                BTreeMap::from([
                    ("category".to_owned(), json!("network")),
                    (
                        "reason_code".to_owned(),
                        json!("target_network_namespace_anchor_unavailable"),
                    ),
                    ("target_pid".to_owned(), json!(args.pid)),
                    (
                        "last_observed_socket_count".to_owned(),
                        json!(previous_sockets.len()),
                    ),
                ]),
            )?;
            None
        };

        if let Some(current_sockets) = current_sockets {
            for (key, old_socket) in &previous_sockets {
                match current_sockets.get(key) {
                    None => emit_socket_event(
                        &mut writer,
                        &mut sequence,
                        "socket_no_longer_observed",
                        old_socket,
                        "complete",
                    )?,
                    Some(current) if current.socket.state != old_socket.socket.state => {
                        emit_socket_event(
                            &mut writer,
                            &mut sequence,
                            "socket_state_changed",
                            current,
                            "complete",
                        )?;
                    }
                    Some(_) => {}
                }
            }
            for (key, current_socket) in &current_sockets {
                if !previous_sockets.contains_key(key) {
                    emit_socket_event(
                        &mut writer,
                        &mut sequence,
                        "socket_first_seen",
                        current_socket,
                        "complete",
                    )?;
                }
            }
            previous_sockets = current_sockets;
        }

        previous_scope = current_scope;
    }

    if totals.process_permission_denied > 0
        || totals.process_read_errors > 0
        || totals.process_malformed_rows > 0
    {
        emit(
            &mut writer,
            &mut sequence,
            "coverage",
            "coverage_gap",
            "coverage:process".to_owned(),
            None,
            None,
            "partial",
            BTreeMap::from([
                ("category".to_owned(), json!("process")),
                ("permission_denied_reads".to_owned(), json!(totals.process_permission_denied)),
                ("read_errors".to_owned(), json!(totals.process_read_errors)),
                ("malformed_rows".to_owned(), json!(totals.process_malformed_rows)),
            ]),
        )?;
    }
    if totals.fd_permission_denied > 0
        || totals.fd_read_errors > 0
        || totals.ambiguous_socket_owner_observations > 0
        || totals.network_table_errors > 0
        || totals.network_malformed_rows > 0
    {
        emit(
            &mut writer,
            &mut sequence,
            "coverage",
            "coverage_gap",
            "coverage:network".to_owned(),
            None,
            None,
            "partial",
            BTreeMap::from([
                ("category".to_owned(), json!("network")),
                ("fd_permission_denied_reads".to_owned(), json!(totals.fd_permission_denied)),
                ("fd_read_errors".to_owned(), json!(totals.fd_read_errors)),
                ("ambiguous_socket_owner_observations".to_owned(), json!(totals.ambiguous_socket_owner_observations)),
                ("tcp_table_errors".to_owned(), json!(totals.network_table_errors)),
                ("malformed_rows".to_owned(), json!(totals.network_malformed_rows)),
            ]),
        )?;
    }

    let network_gap = network_namespace_anchor_lost
        || totals.fd_permission_denied > 0
        || totals.fd_read_errors > 0
        || totals.ambiguous_socket_owner_observations > 0
        || totals.network_table_errors > 0
        || totals.network_malformed_rows > 0;
    let gap_count = u8::from(
        totals.process_permission_denied > 0
            || totals.process_read_errors > 0
            || totals.process_malformed_rows > 0,
    ) + u8::from(network_gap);
    emit(
        &mut writer,
        &mut sequence,
        "coverage",
        "capture_finished",
        "capture:finish".to_owned(),
        None,
        None,
        if totals.has_gaps() || network_namespace_anchor_lost { "partial" } else { "complete" },
        BTreeMap::from([
            ("capture_status".to_owned(), json!("completed")),
            ("coverage_gap_categories".to_owned(), json!(gap_count)),
            ("target_pid_observed_at_end".to_owned(), json!(previous_scope.contains_key(&args.pid))),
            ("processes_in_scope_at_end".to_owned(), json!(previous_scope.len())),
            (
                "tcp_sockets_in_scope_at_end".to_owned(),
                json!(if network_namespace_anchor_lost {
                    None::<usize>
                } else {
                    Some(previous_sockets.len())
                }),
            ),
            (
                "tcp_snapshot_available_at_end".to_owned(),
                json!(!network_namespace_anchor_lost),
            ),
            ("elapsed_ms".to_owned(), json!(started.elapsed().as_millis())),
            ("sampling_interval_ms".to_owned(), json!(args.interval.as_millis())),
        ]),
    )?;

    Ok(())
}

fn parse_args(arguments: impl Iterator<Item = String>) -> Result<Args, String> {
    let mut args = arguments;
    let mut pid = None;
    let mut duration_seconds = DEFAULT_DURATION_SECONDS;
    let mut interval_ms = DEFAULT_INTERVAL_MS;
    let mut output = None;

    while let Some(argument) = args.next() {
        let value = args
            .next()
            .ok_or_else(|| format!("missing value for {argument}"))?;
        match argument.as_str() {
            "--pid" => {
                let parsed = value
                    .parse::<u32>()
                    .map_err(|_| "--pid must be a positive integer".to_owned())?;
                if parsed == 0 {
                    return Err("--pid must be a positive integer".to_owned());
                }
                pid = Some(parsed);
            }
            "--duration-seconds" => {
                duration_seconds = value
                    .parse::<u64>()
                    .map_err(|_| "--duration-seconds must be an integer".to_owned())?;
                if !(1..=MAX_DURATION_SECONDS).contains(&duration_seconds) {
                    return Err(format!(
                        "--duration-seconds must be between 1 and {MAX_DURATION_SECONDS}"
                    ));
                }
            }
            "--interval-ms" => {
                interval_ms = value
                    .parse::<u64>()
                    .map_err(|_| "--interval-ms must be an integer".to_owned())?;
                if !(MIN_INTERVAL_MS..=MAX_INTERVAL_MS).contains(&interval_ms) {
                    return Err(format!(
                        "--interval-ms must be between {MIN_INTERVAL_MS} and {MAX_INTERVAL_MS}"
                    ));
                }
            }
            "--output" => {
                if value.trim().is_empty() {
                    return Err("--output must be a non-empty path".to_owned());
                }
                output = Some(PathBuf::from(value));
            }
            _ => return Err(format!("unknown argument: {argument}")),
        }
    }

    let pid = pid.ok_or_else(|| {
        "missing required --pid; the observer never launches the application".to_owned()
    })?;
    Ok(Args {
        pid,
        duration: Duration::from_secs(duration_seconds),
        interval: Duration::from_millis(interval_ms),
        output,
    })
}

fn emit(
    writer: &mut BufWriter<Box<dyn Write>>,
    sequence: &mut u64,
    category: &str,
    action: &str,
    event_ref: String,
    process: Option<ProcessContext>,
    resource: Option<ResourceContext>,
    quality: &str,
    attributes: BTreeMap<String, Value>,
) -> Result<(), String> {
    let record = event_record(
        *sequence,
        category,
        action,
        event_ref,
        process,
        resource,
        quality,
        attributes,
    )?;
    *sequence += 1;
    write_record(writer, &record)
}

fn write_record(writer: &mut impl Write, record: &EventRecord) -> Result<(), String> {
    serde_json::to_writer(&mut *writer, record)
        .map_err(|error| format!("cannot serialize event record: {error}"))?;
    writer
        .write_all(b"\n")
        .map_err(|error| format!("cannot write event record: {error}"))?;
    writer
        .flush()
        .map_err(|error| format!("cannot flush event record: {error}"))
}

fn emit_process_event(
    writer: &mut BufWriter<Box<dyn Write>>,
    sequence: &mut u64,
    action: &str,
    process: &ProcessInfo,
) -> Result<(), String> {
    emit(
        writer,
        sequence,
        "process",
        action,
        format!("proc-stat:{}:{}", process.pid, process.start_time_ticks),
        Some(ProcessContext::from(process)),
        None,
        "complete",
        BTreeMap::new(),
    )
}

fn emit_socket_event(
    writer: &mut BufWriter<Box<dyn Write>>,
    sequence: &mut u64,
    action: &str,
    owned: &OwnedSocket,
    quality: &str,
) -> Result<(), String> {
    emit(
        writer,
        sequence,
        "network",
        action,
        format!("proc-net-tcp:{}", owned.socket.key),
        Some(ProcessContext::from(&owned.owner)),
        Some(ResourceContext::from(&owned.socket)),
        socket_collection_quality(owned, quality),
        BTreeMap::from([
            ("socket_state".to_owned(), json!(owned.socket.state)),
            ("socket_inode".to_owned(), json!(owned.socket.inode)),
            ("network_family".to_owned(), json!(owned.socket.family)),
            ("local_port".to_owned(), json!(owned.socket.local_port)),
        ]),
    )
}

fn socket_collection_quality<'a>(owned: &OwnedSocket, requested: &'a str) -> &'a str {
    if owned.ambiguous_owner {
        "partial"
    } else {
        requested
    }
}

fn create_new_output_file(path: &Path) -> io::Result<std::fs::File> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    options.mode(0o600);
    options.open(path)
}

fn network_namespace_anchor(
    scope: &BTreeMap<u32, ProcessInfo>,
    target_pid: u32,
) -> Option<u32> {
    scope.contains_key(&target_pid).then_some(target_pid)
}

fn target_pid_was_reused(snapshot: &BTreeMap<u32, ProcessInfo>, target_pid: u32, initial_start_time_ticks: u64) -> bool {
    snapshot.get(&target_pid).is_some_and(|current| current.start_time_ticks != initial_start_time_ticks)
}

fn scope_pids(scope: &BTreeMap<u32, ProcessInfo>) -> Vec<u32> {
    scope.keys().copied().collect()
}

fn owned_sockets(
    sockets: &BTreeMap<String, SocketInfo>,
    owners: &driftglass_agent::SocketOwnerScan,
    scope: &BTreeMap<u32, ProcessInfo>,
) -> BTreeMap<String, OwnedSocket> {
    let mut result = BTreeMap::new();
    for (key, socket) in sockets {
        let Some(pid) = owners.inode_to_pid.get(&socket.inode) else {
            continue;
        };
        let Some(owner) = scope.get(pid) else {
            continue;
        };
        result.insert(
            key.clone(),
            OwnedSocket {
                socket: socket.clone(),
                owner: owner.clone(),
                ambiguous_owner: owners.ambiguous_inodes.contains(&socket.inode),
            },
        );
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_target_pid_reuse_without_treating_a_missing_pid_as_reuse() {
        let original = ProcessInfo { pid: 10, parent_pid: 1, name: "target".into(), start_time_ticks: 100 };
        assert!(!target_pid_was_reused(&BTreeMap::from([(10, original.clone())]), 10, 100));
        assert!(!target_pid_was_reused(&BTreeMap::new(), 10, 100));
        let reused = BTreeMap::from([(10, ProcessInfo { start_time_ticks: 200, ..original })]);
        assert!(target_pid_was_reused(&reused, 10, 100));
    }

    #[test]
    fn retained_descendant_does_not_replace_missing_target_namespace_anchor() {
        let child = ProcessInfo {
            pid: 11,
            parent_pid: 1,
            name: "retained-child".into(),
            start_time_ticks: 200,
        };
        let descendants_only = BTreeMap::from([(11, child.clone())]);
        assert_eq!(network_namespace_anchor(&descendants_only, 10), None);

        let target = ProcessInfo {
            pid: 10,
            parent_pid: 1,
            name: "target".into(),
            start_time_ticks: 100,
        };
        let target_and_child = BTreeMap::from([(10, target), (11, child)]);
        assert_eq!(network_namespace_anchor(&target_and_child, 10), Some(10));
    }

    #[cfg(unix)]
    #[test]
    fn creates_capture_output_with_owner_only_permissions() {
        use std::os::unix::fs::PermissionsExt;
        use std::time::{SystemTime, UNIX_EPOCH};
        let unique = SystemTime::now().duration_since(UNIX_EPOCH).expect("current time").as_nanos();
        let path = std::env::temp_dir().join(format!("driftglass-agent-{}-{unique}.jsonl", std::process::id()));
        let file = create_new_output_file(&path).expect("create secure output");
        assert_eq!(file.metadata().expect("output metadata").permissions().mode() & 0o777, 0o600);
        drop(file);
        std::fs::remove_file(path).expect("remove temporary output");
    }

    #[test]
    fn rejects_missing_target_pid() {
        let error = parse_args(std::iter::empty()).expect_err("PID is required");
        assert!(error.contains("missing required --pid"));
    }

    #[test]
    fn rejects_out_of_range_sampling_interval() {
        let values = ["--pid", "10", "--interval-ms", "1"]
            .into_iter()
            .map(str::to_owned);
        let error = parse_args(values).expect_err("interval below minimum");
        assert!(error.contains("between 100 and 10000"));
    }

    #[test]
    fn ambiguous_socket_attribution_reduces_each_event_quality() {
        let socket = SocketInfo {
            key: "tcp:ipv4:123:127.0.0.1:50000:198.51.100.2:443".into(),
            inode: 123,
            family: "ipv4",
            local_address: "127.0.0.1".into(),
            local_port: 50000,
            remote_address: "198.51.100.2".into(),
            remote_port: 443,
            state: "ESTABLISHED".into(),
        };
        let owner = ProcessInfo {
            pid: 10,
            parent_pid: 1,
            name: "fixture".into(),
            start_time_ticks: 100,
        };
        let ambiguous = OwnedSocket {
            socket: socket.clone(),
            owner: owner.clone(),
            ambiguous_owner: true,
        };
        let unambiguous = OwnedSocket {
            socket,
            owner,
            ambiguous_owner: false,
        };

        assert_eq!(socket_collection_quality(&ambiguous, "complete"), "partial");
        assert_eq!(socket_collection_quality(&unambiguous, "complete"), "complete");
    }

    #[test]
    fn ambiguous_socket_ownership_marks_capture_partial() {
        let mut totals = Totals::default();
        let mut owners = driftglass_agent::SocketOwnerScan::default();
        owners.ambiguous_inodes.insert(123);
        totals.add_owners(&owners);

        assert!(totals.has_gaps());
        assert_eq!(totals.ambiguous_socket_owner_observations, 1);
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn live_procfs_observes_this_process_and_its_loopback_tcp_listener() {
        use std::net::TcpListener;

        let listener = TcpListener::bind(("127.0.0.1", 0)).expect("bind loopback-only listener");
        let local_port = listener.local_addr().expect("listener address").port();
        let pid = std::process::id();
        let proc_root = PathBuf::from("/proc");

        let process_scan = read_process_snapshot(&proc_root).expect("read live procfs process table");
        let process = process_scan.processes.get(&pid).expect("test process is visible in procfs");
        assert_eq!(process.pid, pid);

        let scope = process_scope(&process_scan.processes, pid);
        let owners = read_socket_owners(&proc_root, &scope_pids(&scope));
        let network = read_tcp_snapshot(&proc_root, pid);
        let socket = network
            .sockets
            .values()
            .find(|socket| socket.local_port == local_port && socket.state == "LISTEN")
            .expect("loopback listener is visible in the live TCP table");
        assert_eq!(owners.inode_to_pid.get(&socket.inode), Some(&pid));

        let owned = owned_sockets(&network.sockets, &owners, &scope);
        assert!(owned.values().any(|item| {
            item.socket.local_port == local_port && item.owner.pid == pid
        }));
    }

    #[test]
    fn parses_scoped_capture_arguments() {
        let values = [
            "--pid", "42", "--duration-seconds", "5", "--interval-ms", "100",
        ]
        .into_iter()
        .map(str::to_owned);
        let parsed = parse_args(values).expect("valid arguments");
        assert_eq!(parsed.pid, 42);
        assert_eq!(parsed.duration.as_secs(), 5);
        assert_eq!(parsed.interval.as_millis(), 100);
        assert!(parsed.output.is_none());
    }
}
