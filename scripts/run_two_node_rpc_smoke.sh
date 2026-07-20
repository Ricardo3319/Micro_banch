#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_two_node_rpc_smoke.sh [options]

Run this script on the load-generator node. It starts two disjoint local client
partitions, controls one remote server through SSH, replays one shared v3 trace
through the selected policies, and validates request coverage plus identical
ingress-shard mapping. Optional WP4 counter capture and blind infrastructure
validation never inspect deadline, tail, or method-effect fields.

Required:
  --server-host HOST        SSH alias/hostname for the main experiment node
  --server-ip IPv4          Server experiment-network IPv4 address

Options:
  --server-repo-dir DIR     Server repository path (default: ~/Micro_banch)
  --ssh-config FILE         Repository-external OpenSSH config used by ssh/scp
  --build-dir DIR           Repository-relative build directory
                            (default: build-cloudlab)
  --out-dir DIR             Local result directory on the load-generator node
  --workers N               Server worker/ingress shard count (default: 16)
  --cpus LIST               Explicit server worker CPU list
  --receiver-cpus LIST      Two server receiver CPUs
  --response-sender-cpus LIST Two server response sender CPUs
  --scheduler-cpu N         Dedicated server scheduler CPU
  --server-main-cpu N       Dedicated server main/control CPU
  --irq-cpus LIST           Experiment IRQ CPUs for topology validation
  --allow-control-irq-smt-siblings
  --client-bind-ip IPv4     Explicit load-generator experiment IPv4
  --client0-sender-cpu N --client0-receiver-cpu N
  --client1-sender-cpu N --client1-receiver-cpu N
  --server-interface IFACE  Experiment NIC for remote counter snapshots
  --client-interface IFACE  Experiment NIC for local counter snapshots
  --infrastructure-gate     Require counter capture and blind WP4 validation
  --policies LIST           Comma-separated policies (default: all four)
  --check-period-us X       Scheduler/L1 poll period (default: 100)
  --repetition N            Audit repetition label (default: 1)
  --flow-sockets N          Stable sockets per client partition (default: 256)
  --source-port-base N      First local UDP source port (default: 20000)
  --port N                  Frozen non-formal server UDP port (default: 19040)
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
ssh_config=""
server_host=""
server_ip=""
server_repo_dir='~/Micro_banch'
build_dir='build-cloudlab'
out_dir=""
workers=16
cpus=""
receiver_cpus=""
response_sender_cpus=""
scheduler_cpu=""
server_main_cpu=""
irq_cpus=""
allow_control_irq_smt_siblings=0
client_bind_ip=""
client0_sender_cpu=""
client0_receiver_cpu=""
client1_sender_cpu=""
client1_receiver_cpu=""
server_interface=""
client_interface=""
infrastructure_gate=0
policies_csv="L0_RandomCore,L1_WorkStealingPolling,M0_AltoThreshold,M1_RescueSched"
check_period_us=100
repetition=1
flow_sockets=256
source_port_base=20000
port=19040
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
        --ssh-config) ssh_config="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --cpus) cpus="$2"; shift 2 ;;
        --receiver-cpus) receiver_cpus="$2"; shift 2 ;;
        --response-sender-cpus) response_sender_cpus="$2"; shift 2 ;;
        --scheduler-cpu) scheduler_cpu="$2"; shift 2 ;;
        --server-main-cpu) server_main_cpu="$2"; shift 2 ;;
        --irq-cpus) irq_cpus="$2"; shift 2 ;;
        --allow-control-irq-smt-siblings) allow_control_irq_smt_siblings=1; shift ;;
        --client-bind-ip) client_bind_ip="$2"; shift 2 ;;
        --client0-sender-cpu) client0_sender_cpu="$2"; shift 2 ;;
        --client0-receiver-cpu) client0_receiver_cpu="$2"; shift 2 ;;
        --client1-sender-cpu) client1_sender_cpu="$2"; shift 2 ;;
        --client1-receiver-cpu) client1_receiver_cpu="$2"; shift 2 ;;
        --server-interface) server_interface="$2"; shift 2 ;;
        --client-interface) client_interface="$2"; shift 2 ;;
        --infrastructure-gate) infrastructure_gate=1; shift ;;
        --policies) policies_csv="$2"; shift 2 ;;
        --check-period-us) check_period_us="$2"; shift 2 ;;
        --repetition) repetition="$2"; shift 2 ;;
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

if [[ -n "$ssh_config" ]]; then
    if [[ ! -f "$ssh_config" ]]; then
        echo "SSH config does not exist: $ssh_config" >&2
        exit 2
    fi
    ssh_options=(-F "$ssh_config" -o BatchMode=yes)
fi

if [[ -z "$server_host" || -z "$server_ip" ]]; then
    echo "--server-host and --server-ip are required" >&2
    exit 2
fi
for value_name in workers flow_sockets source_port_base port requests warmup seed job_timeout_seconds repetition; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( workers < 1 || flow_sockets < 1 || source_port_base < 1024
      || source_port_base + 2 * flow_sockets > 65535
      || port < 1024 || port > 65535 || port == 9000 || requests < 1
      || job_timeout_seconds < 1 )); then
    echo "Invalid worker, flow, source-port, server-port, request, or timeout option; this non-formal runner forbids port 9000" >&2
    exit 2
fi
if [[ ! "$check_period_us" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]]; then
    echo "check_period_us must be a positive finite decimal" >&2
    exit 2
fi
python3 - "$check_period_us" <<'PY'
import math, sys
value = float(sys.argv[1])
raise SystemExit(0 if math.isfinite(value) and value > 0 else 1)
PY
for cpu_name in scheduler_cpu server_main_cpu client0_sender_cpu client0_receiver_cpu client1_sender_cpu client1_receiver_cpu; do
    value="${!cpu_name}"
    if [[ -n "$value" && ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$cpu_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( infrastructure_gate == 1 )) && [[ -z "$server_interface" || -z "$client_interface" ]]; then
    echo "--infrastructure-gate requires both --server-interface and --client-interface" >&2
    exit 2
fi
if [[ -n "$server_interface" && -z "$client_interface" ]] || [[ -z "$server_interface" && -n "$client_interface" ]]; then
    echo "server/client counter interfaces must be provided together" >&2
    exit 2
fi
if [[ "$workload" != W2 && "$workload" != W3 ]]; then
    echo "--workload must be W2 or W3" >&2
    exit 2
fi
IFS=',' read -r -a policies <<<"$policies_csv"
valid_policies=" L0_RandomCore L1_WorkStealingPolling M0_AltoThreshold M1_RescueSched "
if (( ${#policies[@]} == 0 )); then
    echo "--policies must not be empty" >&2
    exit 2
fi
declare -A seen_policies=()
for policy in "${policies[@]}"; do
    if [[ -z "$policy" || "$valid_policies" != *" $policy "* ]]; then
        echo "Unknown or empty policy: $policy" >&2
        exit 2
    fi
    if [[ -n "${seen_policies[$policy]:-}" ]]; then
        echo "Duplicate policy: $policy" >&2
        exit 2
    fi
    seen_policies[$policy]=1
done
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
local_stopper="$root/scripts/stop_background_job.sh"
local_counter_capture="$root/scripts/capture_network_counters.sh"
local_nonformal_validator="$root/scripts/validate_wp4_nonformal_config.py"
for path in "$local_trace_generator" "$local_client" "$local_wrapper" "$local_stopper" "$local_counter_capture" "$local_nonformal_validator"; do
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
printf -v server_stopper_q '%q' "$server_repo/scripts/stop_background_job.sh"
server_commit="$(ssh "${ssh_options[@]}" "$server_host" \
    "git -C $server_repo_q rev-parse HEAD")"
if [[ "$server_commit" != "$commit" ]]; then
    echo "Server commit mismatch: local=$commit server=$server_commit" >&2
    exit 1
fi
server_dirty="$(ssh "${ssh_options[@]}" "$server_host" \
    "git -C $server_repo_q status --porcelain")"
if [[ -n "$server_dirty" ]]; then
    echo "Server repository must be clean" >&2
    printf '%s\n' "$server_dirty" >&2
    exit 1
fi
ssh "${ssh_options[@]}" "$server_host" \
    "test -x $server_binary_q && test -x $server_wrapper_q && test -x $server_stopper_q && \
     test -x $server_repo_q/scripts/capture_network_counters.sh"
echo "Resolved server repository: $server_repo"

mkdir -p "$out_dir/metadata"
"$local_nonformal_validator" --actual-port "$port" --workers "$workers" \
    --output "$out_dir/metadata/wp4-nonformal-config-validation.json" \
    > "$out_dir/metadata/wp4-nonformal-config-validation.stdout"
run_id="$(date -u +%Y%m%dT%H%M%S%NZ)-pid-$$-rep-$repetition"
trace_name="rpc-smoke-${workload}-rho-${rho}-seed-${seed}-rep-${repetition}-${run_id}.csv"
trace_path="$out_dir/$trace_name"
"$local_trace_generator" \
    --workload "$workload" --rho "$rho" --seed "$seed" \
    --warmup "$warmup" --requests "$requests" --workers "$workers" \
    --flow-count "$((flow_sockets * 2))" --out "$trace_path"
trace_sha="$(sha256sum "$trace_path" | awk '{print $1}')"

remote_input_dir="$server_repo/physical-results/two-node-input/$run_id"
remote_trace="$remote_input_dir/$trace_name"
printf -v remote_input_dir_q '%q' "$remote_input_dir"
printf -v remote_trace_q '%q' "$remote_trace"
ssh "${ssh_options[@]}" "$server_host" \
    "test ! -e $remote_input_dir_q && mkdir -p -- $remote_input_dir_q"
scp "${ssh_options[@]}" "$trace_path" "$server_host:$remote_trace_q"
remote_sha="$(ssh "${ssh_options[@]}" "$server_host" \
    "sha256sum -- $remote_trace_q | awk '{print \$1}'")"
if [[ "$remote_sha" != "$trace_sha" ]]; then
    echo "Trace SHA mismatch on $server_host" >&2
    exit 1
fi
active_remote_run=""
active_remote_launch_state="NONE"
active_local_job_dir=""
declare -A active_local_launch_state=([client-0]=NONE [client-1]=NONE)
succeeded=0

stop_local_job() {
    local job_dir="$1"
    local name="$2"
    local state="${active_local_launch_state[$name]:-NONE}"
    [[ "$state" != NONE ]] || return 0
    local launch="$job_dir/$name.launch.env"
    local exit_file="$job_dir/$name.exit"
    if [[ -f "$launch" ]]; then
        if "$local_stopper" --job-dir "$job_dir" --name "$name"; then
            active_local_launch_state[$name]=STOPPED
            return 0
        fi
        echo "Exact-PID cleanup command failed for local $name" >&2
        return 1
    fi
    if [[ -f "$exit_file" ]] \
            && grep -qx 'reason=PID_IDENTITY_CAPTURE_FAILED' "$exit_file"; then
        active_local_launch_state[$name]=FAILED_CLOSED
        return 0
    fi
    echo "Cannot establish exact-PID cleanup state for local $name" >&2
    return 1
}

stop_remote_server() {
    [[ -n "$active_remote_run" && "$active_remote_launch_state" != NONE ]] || return 0
    local run_path="$server_repo/$active_remote_run"
    local run_path_q="" launch_path_q="" exit_path_q=""
    printf -v run_path_q '%q' "$run_path"
    printf -v launch_path_q '%q' "$run_path/server.launch.env"
    printf -v exit_path_q '%q' "$run_path/server.exit"
    if ssh "${ssh_options[@]}" "$server_host" \
        "if test -f $launch_path_q; then \
           cd -- $server_repo_q && scripts/stop_background_job.sh \
             --job-dir $run_path_q --name server; \
         elif test -f $exit_path_q && grep -qx reason=PID_IDENTITY_CAPTURE_FAILED $exit_path_q; then \
           exit 0; \
         else \
           echo 'Cannot establish exact-PID cleanup state for remote server' >&2; exit 1; \
         fi"; then
        active_remote_launch_state=STOPPED
        return 0
    fi
    echo "Exact-PID cleanup command failed for remote server" >&2
    return 1
}

collect_remote_audit() {
    local destination_name="$1"
    [[ -n "$active_remote_run" && -n "$active_local_job_dir" ]] || return 0
    local destination="$active_local_job_dir/$destination_name"
    [[ ! -e "$destination" ]] || {
        echo "Remote audit destination already exists: $destination" >&2
        return 1
    }
    copy_remote "$server_repo/$active_remote_run" "$destination"
}

wait_local_launch_manifest() {
    local job_dir="$1"
    local name="$2"
    for _ in $(seq 1 100); do
        if [[ -s "$job_dir/$name.launch.env" ]]             && grep -qx 'manifest_status=PASS' "$job_dir/$name.launch.env"; then
            return 0
        fi
        [[ ! -f "$job_dir/$name.exit" ]] || break
        sleep 0.05
    done
    echo "Local launch manifest was not established for $name" >&2
    return 1
}

wait_remote_launch_manifest() {
    local run_path="$1"
    local launch_path="$server_repo/$run_path/server.launch.env"
    local exit_path="$server_repo/$run_path/server.exit"
    local launch_path_q="" exit_path_q=""
    printf -v launch_path_q '%q' "$launch_path"
    printf -v exit_path_q '%q' "$exit_path"
    ssh "${ssh_options[@]}" "$server_host" \
        "for i in \$(seq 1 100); do \
           if test -s $launch_path_q && grep -qx manifest_status=PASS $launch_path_q; then exit 0; fi; \
           test ! -f $exit_path_q || exit 1; sleep 0.05; \
         done; exit 1"
}

on_exit() {
    local rc=$?
    local cleanup_rc=0
    trap - EXIT
    set +e
    if (( succeeded == 0 )); then
        if [[ -n "$active_local_job_dir" ]]; then
            stop_local_job "$active_local_job_dir" client-0 || cleanup_rc=1
            stop_local_job "$active_local_job_dir" client-1 || cleanup_rc=1
        fi
        stop_remote_server || cleanup_rc=1
        collect_remote_audit remote-partial || cleanup_rc=1
        echo "Two-node RPC run failed. Partial results: $out_dir" >&2
        if (( cleanup_rc != 0 )); then
            echo "Exact-PID cleanup failed; host state is unsafe and requires emergency handling" >&2
            rc=125
        fi
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

capture_local_counters() {
    local destination="$1"
    local label="$2"
    [[ -n "$client_interface" ]] || return 0
    "$local_counter_capture" --interface "$client_interface" \
        --out-dir "$destination" --label "$label"
}

capture_remote_counters() {
    local remote_destination="$1"
    local label="$2"
    [[ -n "$server_interface" ]] || return 0
    local destination_q=""
    local interface_q=""
    local label_q=""
    printf -v destination_q '%q' "$remote_destination"
    printf -v interface_q '%q' "$server_interface"
    printf -v label_q '%q' "$label"
    ssh "${ssh_options[@]}" "$server_host" \
        "cd -- $server_repo_q && scripts/capture_network_counters.sh \
          --interface $interface_q --out-dir $destination_q --label $label_q"
}

for policy in "${policies[@]}"; do
    policy_dir="$out_dir/$policy"
    mkdir -p "$policy_dir"
    active_local_job_dir="$policy_dir"
    active_local_launch_state[client-0]=NONE
    active_local_launch_state[client-1]=NONE
    remote_run="physical-results/two-node-active/${run_id}/${policy}"
    active_remote_run="$remote_run"
    active_remote_launch_state=NONE
    printf -v remote_run_q '%q' "$remote_run"
    ssh "${ssh_options[@]}" "$server_host" \
        "cd -- $server_repo_q && test ! -e $remote_run_q && mkdir -p -- $remote_run_q/network-counters"
    mkdir -p "$policy_dir/network-counters"
    capture_local_counters "$policy_dir/network-counters/node1-before" \
        "$policy node1 before"
    capture_remote_counters "$remote_run/network-counters/node0-before" \
        "$policy node0 before"

    server_command=(
        "./$build_dir/rescuesched_rpc_server"
        --trace "physical-results/two-node-input/$run_id/$trace_name"
        --out-dir "$remote_run/server"
        --bind "$server_ip"
        --port "$port"
        --policy "$policy"
        --workers "$workers"
    )
    if [[ -n "$cpus" ]]; then server_command+=(--cpus "$cpus"); fi
    if [[ -n "$receiver_cpus" ]]; then
        server_command+=(--receiver-cpus "$receiver_cpus")
    fi
    if [[ -n "$response_sender_cpus" ]]; then
        server_command+=(--response-sender-cpus "$response_sender_cpus")
    fi
    if [[ -n "$scheduler_cpu" ]]; then server_command+=(--scheduler-cpu "$scheduler_cpu"); fi
    if [[ -n "$server_main_cpu" ]]; then server_command+=(--server-main-cpu "$server_main_cpu"); fi
    if [[ -n "$irq_cpus" ]]; then server_command+=(--irq-cpus "$irq_cpus"); fi
    if (( allow_control_irq_smt_siblings == 1 )); then
        server_command+=(--allow-control-irq-smt-siblings)
    fi
    server_command+=(
        --check-period-us "$check_period_us"
        --warmup-requests "$warmup"
        --workload-label "$workload"
        --rho-label "$rho"
        --seed-label "$seed"
        --repetition "$repetition"
    )
    printf -v server_command_q '%q ' "${server_command[@]}"
    active_remote_launch_state=MAYBE
    ssh "${ssh_options[@]}" "$server_host" \
        "cd -- $server_repo_q && \
         bash scripts/run_background_job.sh --job-dir $remote_run_q \
           --name server -- $server_command_q"
    wait_remote_launch_manifest "$remote_run"
    active_remote_launch_state=MANIFEST_PASS

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
        sender_cpu=""
        receiver_cpu=""
        if (( index == 0 )); then
            sender_cpu="$client0_sender_cpu"
            receiver_cpu="$client0_receiver_cpu"
        else
            sender_cpu="$client1_sender_cpu"
            receiver_cpu="$client1_receiver_cpu"
        fi
        client_command=(
            "$local_client"
            --trace "$trace_path"
            --server "$server_ip" --port "$port"
            --out-dir "$policy_dir/client-$index"
            --workers "$workers" --flow-sockets "$flow_sockets"
            --client-index "$index" --client-count 2
            --source-port-base "$source_port_base"
            --start-at-unix-ns "$start_at_unix_ns"
            --arrival-scale "$arrival_scale"
            --warmup-requests "$warmup" --workload-label "$workload"
            --rho-label "$rho" --seed-label "$seed" --repetition "$repetition"
        )
        if [[ -n "$client_bind_ip" ]]; then client_command+=(--bind "$client_bind_ip"); fi
        if [[ -n "$sender_cpu" ]]; then client_command+=(--sender-cpu "$sender_cpu"); fi
        if [[ -n "$receiver_cpu" ]]; then client_command+=(--receiver-cpu "$receiver_cpu"); fi
        active_local_launch_state[client-$index]=MAYBE
        bash "$local_wrapper" --job-dir "$policy_dir" --name "client-$index" -- \
            "${client_command[@]}"
        wait_local_launch_manifest "$policy_dir" "client-$index"
        active_local_launch_state[client-$index]=MANIFEST_PASS
    done

    wait_local_job "$policy_dir" client-0
    wait_local_job "$policy_dir" client-1
    wait_remote_server "$remote_run"
    stop_local_job "$policy_dir" client-0
    stop_local_job "$policy_dir" client-1
    stop_remote_server

    capture_local_counters "$policy_dir/network-counters/node1-after" \
        "$policy node1 after"
    capture_remote_counters "$remote_run/network-counters/node0-after" \
        "$policy node0 after"

    copy_remote "$server_repo/$remote_run/server" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server-console.log" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server-command.txt" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server.pid" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server.child.pid" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server.launch.env" "$policy_dir/"
    copy_remote "$server_repo/$remote_run/server.exit" "$policy_dir/"
    if [[ -n "$server_interface" ]]; then
        copy_remote "$server_repo/$remote_run/network-counters/node0-before" \
            "$policy_dir/network-counters/"
        copy_remote "$server_repo/$remote_run/network-counters/node0-after" \
            "$policy_dir/network-counters/"
    fi
    # Capture a complete immutable copy only after after-counters and stop evidence exist.
    collect_remote_audit remote-audit

    active_remote_run=""
    active_remote_launch_state=NONE
    active_local_job_dir=""
    active_local_launch_state[client-0]=NONE
    active_local_launch_state[client-1]=NONE
done

loadgen_host="$(hostname -f 2>/dev/null || hostname)"
{
    echo "scope=physical_network_rpc_two_node_smoke"
    echo "classification=NON_FORMAL_INFRASTRUCTURE_ONLY"
    echo "run_id=$run_id"
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
    echo "policies=$policies_csv"
    echo "check_period_us=$check_period_us"
    echo "repetition=$repetition"
    echo "port=$port"
    echo "client_bind_ip=${client_bind_ip:-UNSPECIFIED}"
    echo "server_interface=${server_interface:-UNSPECIFIED}"
    echo "client_interface=${client_interface:-UNSPECIFIED}"
    echo "job_timeout_seconds=$job_timeout_seconds"
    echo "infrastructure_gate=$infrastructure_gate"
    echo "non_formal_config_validation=PASS"
    echo "node0_profile_sha256=$(sha256sum "$root/config/host-profiles/infocom2027-node0.env" | awk '{print $1}')"
    echo "node1_profile_sha256=$(sha256sum "$root/config/host-profiles/infocom2027-node1.env" | awk '{print $1}')"
    echo "formal_port_9000_used=0"
    echo "ring_4096_used=0"
    echo "S3_GATE=WAIVED_BY_USER_FOR_NON_FORMAL_RUN"
    echo "S3_OPERATIONS=NOT_RUN"
    echo "EVIDENCE_DURABILITY=LOCAL_ONLY"
    echo "FORMAL_RESULT_ELIGIBILITY=NO"
} > "$out_dir/metadata/manifest.env"

python3 "$root/scripts/validate_rpc_two_node_run.py" "$out_dir" \
    --policies "$policies_csv"
if (( infrastructure_gate == 1 )); then
    python3 "$root/scripts/validate_wp4_infrastructure_run.py" "$out_dir" \
        --policies "$policies_csv" --repo-root "$root" \
        --expected-commit "$commit"
elif [[ -n "$server_interface" && -n "$client_interface" ]]; then
    echo "Counter snapshots captured without --infrastructure-gate; blind validator NOT_RUN" >&2
fi
succeeded=1
trap - EXIT
echo "Two-node physical RPC smoke: PASS"
echo "Results: $out_dir"
