#!/usr/bin/env bash
set -euo pipefail

CLI=${1:?path to driftglass-core is required}
OBSERVER=${2:?path to driftglass-agent is required}
FIXTURE=${3:?path to loopback fixture is required}
WORK_DIR=${4:?work directory is required}

rm -rf -- "$WORK_DIR"
mkdir -p -- "$WORK_DIR"
PORTS_FILE="$WORK_DIR/ports"

"$FIXTURE" >"$PORTS_FILE" &
FIXTURE_PID=$!
cleanup() {
    kill "$FIXTURE_PID" 2>/dev/null || true
    wait "$FIXTURE_PID" 2>/dev/null || true
}
trap cleanup EXIT

for attempt in $(seq 1 50); do
    if [[ -s "$PORTS_FILE" ]]; then
        break
    fi
    if ! kill -0 "$FIXTURE_PID" 2>/dev/null; then
        echo "loopback fixture exited before publishing its ports" >&2
        exit 1
    fi
    sleep 0.1
done
if [[ ! -s "$PORTS_FILE" ]]; then
    echo "loopback fixture did not publish its ports" >&2
    exit 1
fi
read -r TCP_PORT UDP_PORT <"$PORTS_FILE"

for ROLE in baseline candidate; do
    VERSION=fixture-1
    if [[ "$ROLE" == candidate ]]; then
        VERSION=fixture-2
    fi
    "$CLI" capture \
        --observer "$OBSERVER" \
        --pid "$FIXTURE_PID" \
        --duration-seconds 1 \
        --interval-ms 100 \
        --experiment-id paired-loopback-smoke-v1 \
        --role "$ROLE" \
        --application-name "Driftglass loopback fixture" \
        --application-version "$VERSION" \
        --scenario "Keep loopback TCP and UDP sockets open during bounded capture" \
        --package-id org.driftglass.synthetic-loopback \
        --event-output "$WORK_DIR/$ROLE-events.jsonl" \
        --manifest-output "$WORK_DIR/$ROLE-manifest.json"
    "$CLI" events validate "$WORK_DIR/$ROLE-events.jsonl"
    "$CLI" manifest validate "$WORK_DIR/$ROLE-manifest.json"

    jq -s -e --arg port "$TCP_PORT" '
      any(.[]; .category == "network" and .action == "socket_present"
        and ((.attributes.local_port // "") | tostring) == $port
        and .collection_quality == "complete")
    ' "$WORK_DIR/$ROLE-events.jsonl" >/dev/null

    jq -s -e --arg port "$UDP_PORT" '
      any(.[]; .category == "network" and .action == "socket_present"
        and .resource.protocol == "udp"
        and ((.attributes.local_port // "") | tostring) == $port
        and .collection_quality == "complete")
    ' "$WORK_DIR/$ROLE-events.jsonl" >/dev/null
done

"$CLI" compare \
    --baseline-manifest "$WORK_DIR/baseline-manifest.json" \
    --candidate-manifest "$WORK_DIR/candidate-manifest.json" \
    --baseline-events "$WORK_DIR/baseline-events.jsonl" \
    --candidate-events "$WORK_DIR/candidate-events.jsonl" \
    --output "$WORK_DIR/report.json"

jq -e '
  .findings == []
  and .summary.added_count == 0
  and .summary.removed_count == 0
  and .summary.changed_count == 0
  and .coverage.baseline.recorded_event_counts.network >= 1
  and .coverage.candidate.recorded_event_counts.network >= 1
' "$WORK_DIR/report.json" >/dev/null

"$CLI" report validate "$WORK_DIR/report.json"
echo "paired native loopback capture and comparison passed"
