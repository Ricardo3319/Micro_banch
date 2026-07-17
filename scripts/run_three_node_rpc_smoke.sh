#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_three_node_rpc_smoke.sh [options]

Run this script on the observer node. It coordinates one server and two client
nodes through SSH, replays one shared v3 trace through all four policies, and
validates request coverage plus identical ingress-shard mapping.

Required:
  --server-host HOST        SSH alias/hostname for the RPC server
  --client0-host HOST       SSH alias/hostname for client partition 0
  --client1-host HOST       SSH alias/hostname for client partition 1
  --server-ip IPv4          Server experiment-network IPv4 address

Options:
  --repo-dir DIR            Remote repository path (default: ~/Micro_banch)
  --build-dir DIR           Remote build directory (default: build-cloudlab)
  --out-dir DIR             Observer result directory
  --workers N               Server worker/ingress shard count (default: 16)
  --cpus LIST               Explicit server worker CPU list
  --flow-sockets N          Stable sockets per client (default: 256)
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
server_host=""
client0_host=""
client1_host=""
server_ip=""
repo_dir='~/Micro_banch'
build_dir='build-cloudlab'
out_dir=""
workers=16
cpus=""
flow_sockets=256
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
    local host="$1"
    local expression=""
    expression="$(remote_repo_expression "$repo_dir")"
    ssh -o BatchMode=yes "$host" "cd -- $expression && pwd -P"
}

copy_remote() {
    local host="$1"
    local remote_path="$2"
    local destination="$3"
    local quoted_path=""
    printf -v quoted_path '%q' "$remote_path"
    scp -r "$host:$quoted_path" "$destination"
}

while (( $# > 0 )); do
    case "$1" in
        --server-host) server_host="$2"; shift 2 ;;
        --client0-host) client0_host="$2"; shift 2 ;;
        --client1-host) client1_host="$2"; shift 2 ;;
        --server-ip) server_ip="$2"; shift 2 ;;
        --repo-dir) repo_dir="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --cpus) cpus="$2"; shift 2 ;;
        --flow-sockets) flow_sockets="$2"; shift 2 ;;
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

if [[ -z "$server_host" || -z "$client0_host" || -z "$client1_host" || -z "$server_ip" ]]; then
    echo "All three hosts and --server-ip are required" >&2
    exit 2
fi
for value_name in workers flow_sockets port requests warmup seed job_timeout_seconds; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( workers < 1 || flow_sockets < 1 || port < 1024 || port > 65535
      || requests < 1 || job_timeout_seconds < 1 )); then
    echo "Invalid worker, flow, port, request, or timeout option" >&2
    exit 2
fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/three-node-rpc-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi

commit="$(git -C "$root" rev-parse HEAD)"
if [[ -n "$(git -C "$root" status --porcelain)" ]]; then
    echo "Observer repository must be clean" >&2
    exit 1
fi
mkdir -p "$out_dir/metadata"

hosts=("$server_host" "$client0_host" "$client1_host")
declare -A remote_repos=()
for host in "${hosts[@]}"; do
    remote_repo="$(resolve_remote_repo "$host")"
    remote_repos["$host"]="$remote_repo"
    printf -v repo_q '%q' "$remote_repo"
    printf -v server_binary_q '%q' "$remote_repo/$build_dir/rescuesched_rpc_server"
    printf -v client_binary_q '%q' "$remote_repo/$build_dir/rescuesched_rpc_client"
    printf -v wrapper_q '%q' "$remote_repo/scripts/run_background_job.sh"
    ssh -o BatchMode=yes "$host" \
        "test \"\$(git -C $repo_q rev-parse HEAD)\" = '$commit'; \
         test -z \"\$(git -C $repo_q status --porcelain)\"; \
         test -x $server_binary_q; \
         test -x $client_binary_q; \
         test -x $wrapper_q"
    echo "Resolved $host repository: $remote_repo"
done

if [[ ! -x "$root/$build_dir/rescuesched_trace_generator" ]]; then
    echo "Trace generator not found: $root/$build_dir/rescuesched_trace_generator" >&2
    echo "Build the observer checkout first." >&2
    exit 1
fi

trace_name="rpc-smoke-${workload}-rho-${rho}-seed-${seed}.csv"
trace_path="$out_dir/$trace_name"
"$root/$build_dir/rescuesched_trace_generator" \
    --workload "$workload" --rho "$rho" --seed "$seed" \
    --warmup "$warmup" --requests "$requests" --workers "$workers" \
    --flow-count "$((flow_sockets * 2))" --out "$trace_path"
trace_sha="$(sha256sum "$trace_path" | awk '{print $1}')"

for host in "${hosts[@]}"; do
    remote_repo="${remote_repos[$host]}"
    remote_input_dir="$remote_repo/physical-results/three-node-input"
    remote_trace="$remote_input_dir/$trace_name"
    printf -v input_dir_q '%q' "$remote_input_dir"
    printf -v trace_q '%q' "$remote_trace"
    ssh "$host" "mkdir -p -- $input_dir_q"
    scp "$trace_path" "$host:$trace_q"
    remote_sha="$(ssh "$host" "sha256sum -- $trace_q | awk '{print \\$1}'")"
    [[ "$remote_sha" == "$trace_sha" ]] || { echo "Trace SHA mismatch on $host" >&2; exit 1; }
done

policies=(L0_RandomCore L1_WorkStealingPolling M0_AltoThreshold M1_RescueSched)

wait_remote_job() {
    local host="$1"
    local run_path="$2"
    local name="$3"
    local remote_repo="${remote_repos[$host]}"
    local exit_path="$remote_repo/$run_path/$name.exit"
    local pid_path="$remote_repo/$run_path/$name.pid"
    local log_path="$remote_repo/$run_path/$name-console.log"
    local exit_path_q=""
    local pid_path_q=""
    local log_path_q=""
    local exit_code=""
    printf -v exit_path_q '%q' "$exit_path"
    printf -v pid_path_q '%q' "$pid_path"
    printf -v log_path_q '%q' "$log_path"
    for (( second = 0; second < job_timeout_seconds; ++second )); do
        if ssh "$host" "test -f $exit_path_q"; then
            exit_code="$(ssh "$host" \
                "sed -n 's/^exit_code=//p' $exit_path_q")"
            if [[ "$exit_code" == 0 ]]; then return 0; fi
            echo "$name failed on $host with exit code ${exit_code:-UNKNOWN}" >&2
            ssh "$host" "tail -n 120 $log_path_q" >&2 || true
            return 1
        fi
        sleep 1
    done
    echo "$name timed out on $host after $job_timeout_seconds seconds" >&2
    ssh "$host" "kill \$(cat $pid_path_q 2>/dev/null) 2>/dev/null || true" || true
    return 1
}

for policy in "${policies[@]}"; do
    policy_dir="$out_dir/$policy"
    mkdir -p "$policy_dir"
    remote_run="physical-results/three-node-active/${policy}-$(date -u +%Y%m%dT%H%M%SZ)"
    cpu_args=""
    if [[ -n "$cpus" ]]; then cpu_args="--cpus $cpus"; fi

    server_repo="${remote_repos[$server_host]}"
    printf -v server_repo_q '%q' "$server_repo"
    ssh "$server_host" "cd -- $server_repo_q; rm -rf '$remote_run'; \
      bash scripts/run_background_job.sh --job-dir '$remote_run' --name server -- \
        ./$build_dir/rescuesched_rpc_server \
        --trace physical-results/three-node-input/$trace_name \
        --out-dir '$remote_run/server' --bind '$server_ip' --port '$port' \
        --policy '$policy' --workers '$workers' $cpu_args \
        --warmup-requests '$warmup' --workload-label '$workload' \
        --rho-label '$rho' --seed-label '$seed' --repetition 1"

    ready=0
    server_log="$server_repo/$remote_run/server-console.log"
    printf -v server_log_q '%q' "$server_log"
    for _ in $(seq 1 50); do
        if ssh "$server_host" "grep -q RPC_SERVER_READY $server_log_q 2>/dev/null"; then
            ready=1
            break
        fi
        sleep 0.2
    done
    if [[ "$ready" != 1 ]]; then
        echo "Server did not become ready for $policy" >&2
        ssh "$server_host" "tail -n 120 $server_log_q" >&2 || true
        exit 1
    fi

    start_at_unix_ns="$(python3 - <<'PY'
import time
print(time.time_ns() + 3_000_000_000)
PY
)"
    for index in 0 1; do
        host="$client0_host"
        [[ "$index" == 1 ]] && host="$client1_host"
        client_repo="${remote_repos[$host]}"
        printf -v client_repo_q '%q' "$client_repo"
        source_port_base=20000
        ssh "$host" "cd -- $client_repo_q; \
          bash scripts/run_background_job.sh --job-dir '$remote_run' \
            --name 'client-$index' -- ./$build_dir/rescuesched_rpc_client \
            --trace physical-results/three-node-input/$trace_name \
            --server '$server_ip' --port '$port' --out-dir '$remote_run/client-$index' \
            --workers '$workers' --flow-sockets '$flow_sockets' \
            --client-index '$index' --client-count 2 \
            --source-port-base '$source_port_base' \
            --start-at-unix-ns '$start_at_unix_ns' --arrival-scale '$arrival_scale' \
            --warmup-requests '$warmup' --workload-label '$workload' \
            --rho-label '$rho' --seed-label '$seed' --repetition 1"
    done

    for index in 0 1; do
        host="$client0_host"
        [[ "$index" == 1 ]] && host="$client1_host"
        wait_remote_job "$host" "$remote_run" "client-$index"
    done
    wait_remote_job "$server_host" "$remote_run" server

    client0_repo="${remote_repos[$client0_host]}"
    client1_repo="${remote_repos[$client1_host]}"
    copy_remote "$server_host" "$server_repo/$remote_run/server" "$policy_dir/"
    copy_remote "$server_host" "$server_repo/$remote_run/server-console.log" "$policy_dir/"
    copy_remote "$server_host" "$server_repo/$remote_run/server-command.txt" "$policy_dir/"
    copy_remote "$server_host" "$server_repo/$remote_run/server.exit" "$policy_dir/"
    copy_remote "$client0_host" "$client0_repo/$remote_run/client-0" "$policy_dir/"
    copy_remote "$client0_host" "$client0_repo/$remote_run/client-0-console.log" "$policy_dir/"
    copy_remote "$client0_host" "$client0_repo/$remote_run/client-0-command.txt" "$policy_dir/"
    copy_remote "$client0_host" "$client0_repo/$remote_run/client-0.exit" "$policy_dir/"
    copy_remote "$client1_host" "$client1_repo/$remote_run/client-1" "$policy_dir/"
    copy_remote "$client1_host" "$client1_repo/$remote_run/client-1-console.log" "$policy_dir/"
    copy_remote "$client1_host" "$client1_repo/$remote_run/client-1-command.txt" "$policy_dir/"
    copy_remote "$client1_host" "$client1_repo/$remote_run/client-1.exit" "$policy_dir/"
done

{
    echo "scope=physical_network_rpc_three_node_smoke"
    echo "commit=$commit"
    echo "server_host=$server_host"
    echo "client0_host=$client0_host"
    echo "client1_host=$client1_host"
    echo "server_ip=$server_ip"
    echo "trace_sha256=$trace_sha"
    echo "workers=$workers"
    echo "flow_sockets_per_client=$flow_sockets"
    echo "workload=$workload"
    echo "rho=$rho"
    echo "seed=$seed"
    echo "warmup_requests=$warmup"
    echo "measurement_requests=$requests"
    echo "arrival_scale=$arrival_scale"
    echo "job_timeout_seconds=$job_timeout_seconds"
} > "$out_dir/metadata/manifest.env"

python3 "$root/scripts/validate_rpc_three_node_run.py" "$out_dir"
echo "Three-node physical RPC smoke: PASS"
echo "Results: $out_dir"
