#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_two_node_rpc_smoke.sh [options]

Run this script on the load-generator node. It starts two disjoint local client
partitions, controls one remote server through SSH, replays one shared v3 trace
through all four policies, and validates request coverage plus identical
ingress-shard mapping.

Required:
  --server-host HOST        SSH alias/hostname for the main experiment node
  --server-ip IPv4          Server experiment-network IPv4 address

Options:
  --server-repo-dir DIR     Server repository path (default: ~/Micro_banch)
  --build-dir DIR           Repository-relative build directory
                            (default: build-cloudlab)
  --out-dir DIR             Local result directory on the load-generator node
  --workers N               Server worker/ingress shard count (default: 16)
  --cpus LIST               Explicit server worker CPU list
  --flow-sockets N          Stable sockets per client partition (default: 256)
  --source-port-base N      First local UDP source port (default: 20000)
  --port N                  Server UDP port (default: 9000)
  --requests N              Measurement requests (default: 5000)
  --warmup N                Warmup requests (default: 500)
  --workload W2|W3          Smoke workload (default: W3)
  --rho X                   Trace rho label (default: 0.85)
  --seed N                  Trace seed (default: 11)
  --arrival-scale X         Physical arrival multiplier (default: 1)
  --job-timeout-seconds N   Per-process completion timeout (default: 120)
EOF
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ssh_options=(-o BatchMode=yes)
server_host=""
server_ip=""
server_repo_dir='~/Micro_banch'
build_dir='build-cloudlab'
out_dir=""
workers=16
cpus=""
flow_sockets=256
source_port_base=20000
port=9000
requests=5000
warmup=500
workload=W3
rho=0.85
seed=11
arrival_scale=1
job_timeout_seconds=120

remote_repo_expression() {
    local requested="$1"
    local quoted=""
    if [[ "$requested" == "~" ]]; then
        printf '%s' '$HOME'
    elif [[ "$requested" == "~/"* ]]; then
        printf -v quoted '%q' "${requested#\~/}"
        printf '$HOME/%s' "$quoted"
    else
        printf '%q' "$requested"
    fi
}

resolve_remote_repo() {
    local expression=""
    expression="$(remote_repo_expression "$server_repo_dir")"
    ssh "${ssh_options[@]}" "$server_host" "cd -- $expression && pwd -P"
}

copy_remote() {
    local remote_path="$1"
    local destination="$2"
    local quoted_path=""
    printf -v quoted_path '%q' "$remote_path"
    scp "${ssh_options[@]}" -r "$server_host:$quoted_path" "$destination"
}

while (( $# > 0 )); do
    case "$1" in
        --server-host) server_host="$2"; shift 2 ;;
        --server-ip) server_ip="$2"; shift 2 ;;
        --server-repo-dir|--repo-dir) server_repo_dir="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --cpus) cpus="$2"; shift 2 ;;
        --flow-sockets) flow_sockets="$2"; shift 2 ;;
        --source-port-base) source_port_base="$2"; shift 2 ;;
        --port) port="$2"; shift 2 ;;
        --requests) requests="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --workload) workload="$2"; shift 2 ;;
        --rho) rho="$2"; shift 2 ;;
        --seed) seed="$2"; shift 2 ;;
        --arrival-scale) arrival_scale="$2"; shift 2 ;;
        --job-timeout-seconds) job_timeout_seconds="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$server_host" || -z "$server_ip" ]]; then
    echo "--server-host and --server-ip are required" >&2
    exit 2
fi
for value_name in workers flow_sockets source_port_base port requests warmup seed job_timeout_seconds; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( workers < 1 || flow_sockets < 1 || source_port_base < 1024
      || source_port_base + 2 * flow_sockets > 65535
      || port < 1024 || port > 65535 || requests < 1
      || job_timeout_seconds < 1 )); then
    echo "Invalid worker, flow, source-port, server-port, request, or timeout option" >&2
    exit 2
fi
if [[ "$workload" != W2 && "$workload" != W3 ]]; then
    echo "--workload must be W2 or W3" >&2
    exit 2
fi
if [[ "$build_dir" == /* || "$build_dir" == *..* ]]; then
    echo "--build-dir must be a repository-relative path without '..'" >&2
    exit 2
fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/two-node-rpc-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi

required_commands=(git python3 sha256sum ssh scp hostname)
for command_name in "${required_commands[@]}"; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing required command: $command_name" >&2
        exit 1
    }
done

commit="$(git -C "$root" rev-parse HEAD)"
if [[ -n "$(git -C "$root" status --porcelain)" ]]; then
    echo "Load-generator repository must be clean" >&2
    exit 1
fi

local_trace_generator="$root/$build_dir/rescuesched_trace_generator"
local_client="$root/$build_dir/rescuesched_rpc_client"
local_wrapper="$root/scripts/run_background_job.sh"
for path in "$local_trace_generator" "$local_client" "$local_wrapper"; do
    if [[ ! -x "$path" ]]; then
        echo "Required local executable not found: $path" >&2
        echo "Build the load-generator checkout first." >&2
        exit 1
    fi
done

server_repo="$(resolve_remote_repo)"
printf -v server_repo_q '%q' "$server_repo"
printf -v server_binary_q '%q' "$server_repo/$build_dir/rescuesched_rpc_server"
printf -v server_wrapper_q '%q' "$server_repo/scripts/run_background_job.sh"
ssh "${ssh_options[@]}" "$server_host" \
    "test \"\$(git -C $server_repo_q rev-parse HEAD)\" = '$commit'; \
     test -z \"\$(git -C $server_repo_q status --porcelain)\"; \
     test -x $server_binary_q; \
     test -x $server_wrapper_q"
echo "Resolved server repository: $server_repo"

mkdir -p "$out_dir/metadata"
trace_name="rpc-smoke-${workload}-rho-${rho}-seed-${seed}.csv"
trace_path="$out_dir/$trace_name"
"$local_trace_generator" \
    --workload "$workload" --rho "$rho" --seed "$seed" \
    --warmup "$warmup" --requests "$requests" --workers "$workers" \
    --flow-count "$((flow_sockets * 2))" --out "$trace_path"
trace_sha="$(sha256sum "$trace_path" | awk '{print $1}')"

remote_input_dir="$server_repo/physical-results/two-node-input"
remote_trace="$remote_input_dir/$trace_name"
printf -v remote_input_dir_q '%q' "$remote_input_dir"
printf -v remote_trace_q '%q' "$remote_trace"
ssh "${ssh_options[@]}" "$server_host" "mkdir -p -- $remote_input_dir_q"
scp "${ssh_options[@]}" "$trace_path" "$server_host:$remote_trace_q"
remote_sha="$(ssh "${ssh_options[@]}" "$server_host" \
    "sha256sum -- $remote_trace_q | awk '{print \$1}'")"
if [[ "$remote_sha" != "$trace_sha" ]]; then
    echo "Trace SHA mismatch on $server_host" >&2
    exit 1
fi

policies=(L0_RandomCore L1_WorkStealingPolling M0_AltoThreshold M1_RescueSched)
active_remote_run=""
active_local_job_dir=""
succeeded=0

stop_local_job() {
    local job_dir="$1"
    local name="$2"
    local pid_file="$job_dir/$name.pid"
    [[ -f "$pid_file" ]] || return 0
    local pid=""
    pid="$(cat "$pid_file" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 0
    pkill -TERM -P "$pid" 2>/dev/null || true
    kill "$pid" 2>/dev/null || true
}

stop_remote_server() {
    [[ -n "$active_remote_run" ]] || return 0
    local pid_path="$server_repo/$active_remote_run/server.pid"
    local pid_path_q=""
    printf -v pid_path_q '%q' "$pid_path"
    ssh "${ssh_options[@]}" "$server_host" \
        "pid=\$(cat $pid_path_q 2>/dev/null || true); \
         if test -n \"\$pid\"; then \
           pkill -TERM -P \"\$pid\" 2>/dev/null || true; \
           kill \"\$pid\" 2>/dev/null || true; \
         fi" >/dev/null 2>&1 || true
}

on_exit() {
    local rc=$?
    trap - EXIT
    if (( succeeded == 0 )); then
        if [[ -n "$active_local_job_dir" ]]; then
            stop_local_job "$active_local_job_dir" client-0
            stop_local_job "$active_local_job_dir" client-1
        fi
        stop_remote_server
        echo "Two-node RPC run failed. Partial results: $out_dir" >&2
    fi
    exit "$rc"
}
trap on_exit EXIT

wait_local_job() {
    local job_dir="$1"
    local name="$2"
    local exit_path="$job_dir/$name.exit"
    local log_path="$job_dir/$name-console.log"
    local exit_code=""
    for (( second = 0; second < job_timeout_seconds; ++second )); do
        if [[ -f "$exit_path" ]]; then
            exit_code="$(sed -n 's/^exit_code=//p' "$exit_path")"
            if [[ "$exit_code" == 0 ]]; then return 0; fi
            echo "$name failed locally with exit code ${exit_code:-UNKNOWN}" >&2
            tail -n 120 "$log_path" >&2 || true
            return 1
        fi
        sleep 1
    done
    echo "$name timed out locally after $job_timeout_seconds seconds" >&2
    stop_local_job "$job_dir" "$name"
    return 1
}

wait_remote_server() {
    local run_path="$1"
    local exit_path="$server_repo/$run_path/server.exit"
    local log_path="$server_repo/$run_path/server-console.log"
    local exit_path_q=""
    local log_path_q=""
    local exit_code=""
    printf -v exit_path_q '%q' "$exit_path"
    printf -v log_path_q '%q' "$log_path"
    for (( second = 0; second < job_timeout_seconds; ++second )); do
        if ssh "${ssh_options[@]}" "$server_host" "test -f $exit_path_q"; then
            exit_code="$(ssh "${ssh_options[@]}" "$server_host" \
                "sed -n 's/^exit_code=//p' $exit_path_q")"
            if [[ "$exit_code" == 0 ]]; then return 0; fi
            echo "server failed on $server_host with exit code ${exit_code:-UNKNOWN}" >&2
            ssh "${ssh_options[@]}" "$server_host" \
                "tail -n 120 $log_path_q" >&2 || true
            return 1
        fi
        sleep 1
    done
    echo "server timed out on $server_host after $job_timeout_seconds seconds" >&2
    stop_remote_server
    return 1
}

for policy in "${policies[@]}"; do
    policy_dir="$out_dir/$policy"
    mkdir -p "$policy_dir"
    active_local_job_dir="$policy_dir"
    remote_run="physical-results/two-node-active/${policy}-$(date -u +%Y%m%dT%H%M%SZ)-$$"
    active_remote_run="$remote_run"

    server_command=(
        "./$build_dir/rescuesched_rpc_server"
        --trace "physical-results/two-node-input/$trace_name"
        --out-dir "$remote_run/server"
        --bind "$server_ip"
        --port "$port"
        --policy "$policy"
        --workers "$workers"
    )
    if [[ -n "$cpus" ]]; then server_command+=(--cpus "$cpus"); fi
    server_command+=(
        --warmup-requests "$warmup"
        --workload-label "$workload"
        --rho-label "$rho"
        --seed-label "$seed"
        --repetition 1
    )
    printf -v server_command_q '%q ' "${server_command[@]}"
    printf -v remote_run_q '%q' "$remote_run"
    ssh "${ssh_options[@]}" "$server_host" \
        "cd -- $server_repo_q && \
         rm -rf -- $remote_run_q && \
         bash scripts/run_background_job.sh --job-dir $remote_run_q \
           --name server -- $server_command_q"

    ready=0
    server_log="$server_repo/$remote_run/server-console.log"
    server_exit="$server_repo/$remote_run/server.exit"
    printf -v server_log_q '%q' "$server_log"
    printf -v server_exit_q '%q' "$server_exit"
    for _ in $(seq 1 50); do
        if ssh "${ssh_options[@]}" "$server_host" \
            "grep -q RPC_SERVER_READY $server_log_q 2>/dev/null"; then
            ready=1
            break
        fi
        if ssh "${ssh_options[@]}" "$server_host" "test -f $server_exit_q"; then
            break
        fi
        sleep 0.2
    done
    if [[ "$ready" != 1 ]]; then
        echo "Server did not become ready for $policy" >&2
        ssh "${ssh_options[@]}" "$server_host" \
            "tail -n 120 $server_log_q" >&2 || true
        exit 1
    fi

    start_at_unix_ns="$(python3 - <<'PY'
import time
print(time.time_ns() + 3_000_000_000)
PY
)"
    for index in 0 1; do
        bash "$local_wrapper" --job-dir "$policy_dir" --name "client-$index" -- \
            "$local_client" \
            --trace "$trace_path" \
            --server "$server_ip" --port "$port" \
            --out-dir "$policy_dir/client-$index" \
            --workers "$workers" --flow-sockets "$flow_sockets" \
            --client-index "$index" --client-count 2 \
            --source-port-base "$source_port_base" \
            --start-at-unix-ns "$start_at_unix_ns" \
            --arrival-scale "$arrival_scale" \
            --warmup-requests "$warmup" --workload-label "$workload" \
            --rho-label "$rho" --seed-label "$seed" --repetition 1
    done

    wait_local_job "$policy_dir" client-0
    wait_local_job "$policy_dir" client-1
    wait_remote_server "$remote_run"

    copy_remote "$server_repo/$remote_run/server" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server-console.log" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server-command.txt" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server.exit" "$policy_dir/"

    active_remote_run=""
    active_local_job_dir=""
done

loadgen_host="$(hostname -f 2>/dev/null || hostname)"
{
    echo "scope=physical_network_rpc_two_node_smoke"
    echo "commit=$commit"
    echo "server_host=$server_host"
    echo "loadgen_host=$loadgen_host"
    echo "server_ip=$server_ip"
    echo "client_partitions=2"
    echo "trace_sha256=$trace_sha"
    echo "workers=$workers"
    echo "flow_sockets_per_client=$flow_sockets"
    echo "client_0_source_ports=${source_port_base}-$((source_port_base + flow_sockets - 1))"
    echo "client_1_source_ports=$((source_port_base + flow_sockets))-$((source_port_base + 2 * flow_sockets - 1))"
    echo "workload=$workload"
    echo "rho=$rho"
    echo "seed=$seed"
    echo "warmup_requests=$warmup"
    echo "measurement_requests=$requests"
    echo "arrival_scale=$arrival_scale"
    echo "job_timeout_seconds=$job_timeout_seconds"
} > "$out_dir/metadata/manifest.env"

python3 "$root/scripts/validate_rpc_two_node_run.py" "$out_dir"
succeeded=1
trap - EXIT
echo "Two-node physical RPC smoke: PASS"
echo "Results: $out_dir"
