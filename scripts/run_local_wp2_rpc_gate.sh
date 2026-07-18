#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/run_local_wp2_rpc_gate.sh [options]

Runs the WP2 code-level UDP loopback gate without host tuning:
  1. one frozen trace through L0/L1/M0/M1 on the same non-formal port,
     source-port base, and flow-socket count;
  2. exact comparison of flow_id/source_port/kernel_reuseport_ingress_shard;
  3. executable-level response-queue failure injection proving fail-closed.

Options:
  --build-dir DIR       Existing or new CMake build directory (default: build-wp2-release)
  --out-dir DIR         New evidence root (default: physical-results/wp2-rpc-<UTC>)
  --port N              Non-formal loopback port (default: 19184; 9000 is rejected)
  --source-port-base N  Deterministic client flow base (default: 23000)
  --flow-sockets N      Stable loopback flows (default: 32)
  --arrival-scale X     Trace arrival multiplier (default: 40)
  --skip-build          Reuse existing binaries without configure/build/CTest
  -h, --help            Show this help
USAGE
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="build-wp2-release"
out_dir=""
port=19184
source_port_base=23000
flow_sockets=32
arrival_scale=40
skip_build=0
workers=2
warmup=20
requests=200

while (( $# > 0 )); do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --port) port="$2"; shift 2 ;;
        --source-port-base) source_port_base="$2"; shift 2 ;;
        --flow-sockets) flow_sockets="$2"; shift 2 ;;
        --arrival-scale) arrival_scale="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in "port:$port" "source-port-base:$source_port_base" "flow-sockets:$flow_sockets"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "--$name must be an integer" >&2
        exit 2
    fi
done
if (( port < 1024 || port > 65535 || port == 9000 )); then
    echo "--port must be in [1024,65535] and must not be the formal port 9000" >&2
    exit 2
fi
if (( source_port_base < 1024
      || source_port_base + flow_sockets - 1 > 65535
      || flow_sockets < 1 )); then
    echo "invalid source-port-base/flow-sockets range" >&2
    exit 2
fi
if ! awk -v value="$arrival_scale" 'BEGIN {
    exit !(value ~ /^[0-9]+([.][0-9]+)?$/ && value + 0 > 0)
}'; then
    echo "--arrival-scale must be a positive number" >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then build_dir="$root/$build_dir"; fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/wp2-rpc-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi
mkdir -p "$out_dir"/{metadata,logs,trace,policy-matrix,failure-injection}

start_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
if (( skip_build == 0 )); then
    cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
        > "$out_dir/logs/cmake-configure.log" 2>&1
    cmake --build "$build_dir" --parallel \
        > "$out_dir/logs/build.log" 2>&1
    ctest --test-dir "$build_dir" --output-on-failure \
        > "$out_dir/logs/ctest.log" 2>&1
fi
for executable in rescuesched_trace_generator rescuesched_rpc_server rescuesched_rpc_client; do
    [[ -x "$build_dir/$executable" ]] || {
        echo "Missing executable: $build_dir/$executable" >&2
        exit 1
    }
done

cpu_csv="$(python3 - <<'PY'
import os
from pathlib import Path

selected = []
seen = set()
for cpu in sorted(os.sched_getaffinity(0)):
    root = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
    try:
        socket = int((root / "physical_package_id").read_text().strip())
        core = int((root / "core_id").read_text().strip())
    except (OSError, ValueError):
        continue
    identity = (socket, core)
    if identity in seen:
        continue
    seen.add(identity)
    selected.append(cpu)
    if len(selected) == 6:
        break
if len(selected) != 6:
    raise SystemExit("six process-allowed CPUs on distinct physical cores are required")
print(",".join(map(str, selected)))
PY
)"
IFS=, read -r -a selected_cpus <<< "$cpu_csv"
worker_cpus="${selected_cpus[0]},${selected_cpus[1]}"
receiver_cpus="${selected_cpus[2]},${selected_cpus[3]}"
sender_cpus="${selected_cpus[4]},${selected_cpus[5]}"

trace="$out_dir/trace/wp2-rpc-trace.csv"
"$build_dir/rescuesched_trace_generator" \
    --out "$trace" --workload W3 --rho 0.50 --seed 11 \
    --workers "$workers" --warmup "$warmup" --requests "$requests" \
    --flow-count "$flow_sockets" > "$out_dir/logs/trace-generator.log" 2>&1

server_pid=""
cleanup_server() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}
trap cleanup_server EXIT

wait_for_ready() {
    local log="$1"
    for _ in $(seq 1 100); do
        if grep -q 'RPC_SERVER_READY' "$log" 2>/dev/null; then return 0; fi
        if ! kill -0 "$server_pid" 2>/dev/null; then return 1; fi
        sleep 0.05
    done
    return 1
}

write_command() {
    local path="$1"; shift
    printf '%q ' "$@" > "$path"
    printf '\n' >> "$path"
}

run_policy() {
    local policy="$1"
    local run_root="$out_dir/policy-matrix/$policy"
    local server_dir="$run_root/server"
    local client_dir="$run_root/client"
    local server_log="$run_root/server-console.log"
    local client_log="$run_root/client-console.log"
    mkdir -p "$run_root"
    local -a server_cmd=(
        "$build_dir/rescuesched_rpc_server"
        --trace "$trace" --out-dir "$server_dir"
        --bind 127.0.0.1 --port "$port"
        --policy "$policy" --workers "$workers" --cpus "$worker_cpus"
        --receiver-cpus "$receiver_cpus"
        --response-sender-cpus "$sender_cpus"
        --warmup-requests "$warmup" --idle-timeout-seconds 5
        --decision-sample-cap 100000 --decision-bucket-us 1000
        --workload-label W3 --rho-label 0.50 --seed-label 11 --repetition 1
    )
    local -a client_cmd=(
        "$build_dir/rescuesched_rpc_client"
        --trace "$trace" --server 127.0.0.1 --bind 127.0.0.1 --port "$port"
        --out-dir "$client_dir" --workers "$workers"
        --flow-sockets "$flow_sockets" --source-port-base "$source_port_base"
        --warmup-requests "$warmup" --arrival-scale "$arrival_scale"
        --response-timeout-seconds 5
        --workload-label W3 --rho-label 0.50 --seed-label 11 --repetition 1
    )
    write_command "$run_root/server-command.txt" "${server_cmd[@]}"
    write_command "$run_root/client-command.txt" "${client_cmd[@]}"
    "${server_cmd[@]}" > "$server_log" 2>&1 &
    server_pid=$!
    if ! wait_for_ready "$server_log"; then
        echo "$policy server did not become ready" >&2
        cleanup_server
        return 1
    fi
    set +e
    "${client_cmd[@]}" > "$client_log" 2>&1
    local client_rc=$?
    wait "$server_pid"
    local server_rc=$?
    set -e
    server_pid=""
    printf 'server_rc=%d\nclient_rc=%d\n' "$server_rc" "$client_rc" \
        > "$run_root/exit-codes.txt"
    (( server_rc == 0 && client_rc == 0 ))
    grep -q '^status=PASS$' "$server_dir/RPC_SERVER_STATUS.txt"
    grep -q '^status=PASS$' "$client_dir/RPC_CLIENT_STATUS.txt"
    awk -F, 'BEGIN {OFS=","}
        NR == 1 {print "flow_id","source_port","kernel_reuseport_ingress_shard"; next}
        {print $2,$4,$5}' "$server_dir/ingress_mapping.csv" \
        > "$run_root/ingress-mapping-projection.csv"
}

policies=(L0_RandomCore L1_WorkStealingPolling M0_AltoThreshold M1_RescueSched)
for policy in "${policies[@]}"; do
    run_policy "$policy"
done

reference="$out_dir/policy-matrix/L0_RandomCore/ingress-mapping-projection.csv"
comparison="$out_dir/policy-matrix/MAPPING_COMPARISON.txt"
{
    echo "status=PASS"
    echo "comparison_fields=flow_id,source_port,kernel_reuseport_ingress_shard"
    echo "destination_port=$port"
    echo "source_port_base=$source_port_base"
    echo "flow_sockets=$flow_sockets"
    echo "trace_sha256=$(sha256sum "$trace" | awk '{print $1}')"
    for policy in "${policies[@]}"; do
        projection="$out_dir/policy-matrix/$policy/ingress-mapping-projection.csv"
        diff -u "$reference" "$projection" \
            > "$out_dir/policy-matrix/$policy/mapping-vs-L0.diff"
        echo "$policy=$(sha256sum "$projection" | awk '{print $1}')"
    done
} > "$comparison"

failure_root="$out_dir/failure-injection"
failure_server="$failure_root/server"
failure_client="$failure_root/client"
failure_server_log="$failure_root/server-console.log"
failure_client_log="$failure_root/client-console.log"
server_cmd=(
    "$build_dir/rescuesched_rpc_server"
    --trace "$trace" --out-dir "$failure_server"
    --bind 127.0.0.1 --port "$port"
    --policy M1_RescueSched --workers "$workers" --cpus "$worker_cpus"
    --receiver-cpus "$receiver_cpus" --response-sender-cpus "$sender_cpus"
    --warmup-requests "$warmup" --idle-timeout-seconds 5
    --response-queue-capacity 64 --inject-response-queue-failure-after 8
    --decision-sample-cap 100000 --decision-bucket-us 1000
)
client_cmd=(
    "$build_dir/rescuesched_rpc_client"
    --trace "$trace" --server 127.0.0.1 --bind 127.0.0.1 --port "$port"
    --out-dir "$failure_client" --workers "$workers"
    --flow-sockets "$flow_sockets" --source-port-base "$source_port_base"
    --warmup-requests "$warmup" --arrival-scale "$arrival_scale"
    --response-timeout-seconds 3
)
write_command "$failure_root/server-command.txt" "${server_cmd[@]}"
write_command "$failure_root/client-command.txt" "${client_cmd[@]}"
"${server_cmd[@]}" > "$failure_server_log" 2>&1 &
server_pid=$!
if ! wait_for_ready "$failure_server_log"; then
    echo "failure-injection server did not become ready" >&2
    cleanup_server
    exit 1
fi
set +e
"${client_cmd[@]}" > "$failure_client_log" 2>&1
failure_client_rc=$?
wait "$server_pid"
failure_server_rc=$?
set -e
server_pid=""
printf 'server_rc=%d\nclient_rc=%d\n' \
    "$failure_server_rc" "$failure_client_rc" > "$failure_root/exit-codes.txt"

[[ "$failure_server_rc" -eq 1 ]]
[[ "$failure_client_rc" -eq 1 ]]
grep -q '^status=FAIL$' "$failure_server/RPC_SERVER_STATUS.txt"
grep -q '^status=FAIL$' "$failure_client/RPC_CLIENT_STATUS.txt"
enqueue_failures="$(awk -F= '$1 == "response_enqueue_failures" {print $2}' \
    "$failure_server/RPC_SERVER_STATUS.txt")"
responses_sent="$(awk -F= '$1 == "responses_sent" {print $2}' \
    "$failure_server/RPC_SERVER_STATUS.txt")"
expected_requests="$(awk -F= '$1 == "expected_requests" {print $2}' \
    "$failure_server/RPC_SERVER_STATUS.txt")"
if [[ ! "$enqueue_failures" =~ ^[0-9]+$ ]] || (( enqueue_failures < 1 )); then
    echo "response queue failure injection was not counted" >&2
    exit 1
fi
if [[ ! "$responses_sent" =~ ^[0-9]+$ || ! "$expected_requests" =~ ^[0-9]+$ ]]; then
    echo "failure injection response counts were not numeric" >&2
    exit 1
fi
if (( responses_sent >= expected_requests )); then
    echo "failure injection did not produce an incomplete response count" >&2
    exit 1
fi
{
    echo "status=PASS"
    echo "classification=EXPECTED_FAIL_CLOSED"
    echo "server_status=FAIL"
    echo "client_status=FAIL"
    echo "response_enqueue_failures=$enqueue_failures"
    echo "responses_sent=$responses_sent"
    echo "expected_requests=$expected_requests"
    echo "silent_drop=NO"
} > "$failure_root/GATE_STATUS.txt"

end_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
{
    echo "status=PASS"
    echo "classification=WP2_CODE_LEVEL_LOOPBACK_GATE"
    echo "start_utc=$start_utc"
    echo "end_utc=$end_utc"
    echo "commit=$(git -C "$root" rev-parse HEAD)"
    echo "dirty_file_count=$(git -C "$root" status --porcelain | wc -l)"
    echo "scope=local_loopback_only_no_host_tuning_no_two_node_rpc"
    echo "port=$port"
    echo "formal_port_9000_used=NO"
    echo "source_port_base=$source_port_base"
    echo "flow_sockets=$flow_sockets"
    echo "worker_cpus=$worker_cpus"
    echo "receiver_cpus=$receiver_cpus"
    echo "response_sender_cpus=$sender_cpus"
    echo "mapping_comparison=PASS"
    echo "response_queue_failure_injection=PASS_EXPECTED_FAIL_CLOSED"
} > "$out_dir/WP2_RPC_GATE_STATUS.txt"
{
    echo "start_utc=$start_utc"
    echo "end_utc=$end_utc"
    echo "build_dir=$build_dir"
    echo "trace=$trace"
    echo "trace_sha256=$(sha256sum "$trace" | awk '{print $1}')"
    echo "arrival_scale=$arrival_scale"
    echo "warmup_requests=$warmup"
    echo "measurement_requests=$requests"
} > "$out_dir/metadata/manifest.env"
(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum \
        > SHA256SUMS
)
trap - EXIT

echo "WP2 local RPC/mapping/failure-injection gate: PASS"
echo "Results: $out_dir"
echo "Scope: code-level loopback only; no host tuning or two-node RPC."
