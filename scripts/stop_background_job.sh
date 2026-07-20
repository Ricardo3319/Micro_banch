#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/stop_background_job.sh --job-dir DIR --name NAME [--wait-seconds N]

Stops only the exact child/wrapper PIDs recorded by run_background_job.sh.
PID reuse is rejected by comparing Linux /proc start ticks. No process-name or
pattern matching is used.
USAGE
}

job_dir=""
name=""
wait_seconds=5
while (( $# > 0 )); do
    case "$1" in
        --job-dir) job_dir="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        --wait-seconds) wait_seconds="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
if [[ -z "$job_dir" || -z "$name" || ! "$name" =~ ^[A-Za-z0-9._-]+$ \
      || ! "$wait_seconds" =~ ^[0-9]+$ || "$wait_seconds" -lt 1 ]]; then
    usage >&2
    exit 2
fi
launch_file="$job_dir/$name.launch.env"
[[ -f "$launch_file" ]] || { echo "Missing launch manifest: $launch_file" >&2; exit 1; }

read_value() {
    local key="$1"
    sed -n "s/^${key}=//p" "$launch_file"
}
format="$(read_value format)"
manifest_status="$(read_value manifest_status)"
manifest_host="$(read_value hostname)"
wrapper_pid="$(read_value wrapper_pid)"
wrapper_start_ticks="$(read_value wrapper_start_ticks)"
child_pid="$(read_value child_pid)"
child_start_ticks="$(read_value child_start_ticks)"
current_host="$(hostname -f 2>/dev/null || hostname)"
[[ "$format" == rescuesched-background-job-v2 ]] || { echo "Unsupported launch manifest" >&2; exit 1; }
[[ "$manifest_status" == PASS ]] || { echo "Launch manifest is not complete/PASS" >&2; exit 1; }
[[ "$manifest_host" == "$current_host" ]] || { echo "Launch manifest belongs to another host" >&2; exit 1; }
for pair in "$wrapper_pid:$wrapper_start_ticks" "$child_pid:$child_start_ticks"; do
    pid="${pair%%:*}"
    ticks="${pair#*:}"
    [[ "$pid" =~ ^[0-9]+$ && "$ticks" =~ ^[0-9]+$ ]] || {
        echo "Invalid PID/start-tick identity in $launch_file" >&2
        exit 1
    }
done

identity_state() {
    local pid="$1"
    local expected="$2"
    if [[ ! -e "/proc/$pid" ]]; then
        printf 'ABSENT'
        return 0
    fi
    if [[ ! -r "/proc/$pid/stat" ]]; then
        printf 'UNREADABLE'
        return 0
    fi
    local actual=""
    actual="$(awk '{print $22}' "/proc/$pid/stat" 2>/dev/null || true)"
    if [[ "$actual" == "$expected" ]]; then
        printf 'MATCH'
    else
        printf 'MISMATCH:%s' "${actual:-UNAVAILABLE}"
    fi
}

matches_identity() {
    [[ "$(identity_state "$1" "$2")" == MATCH ]]
}

wait_gone() {
    local pid="$1"
    local ticks="$2"
    local tenths=$((wait_seconds * 10))
    for ((i=0; i<tenths; ++i)); do
        matches_identity "$pid" "$ticks" || return 0
        sleep 0.1
    done
    return 1
}

child_initial_state="$(identity_state "$child_pid" "$child_start_ticks")"
wrapper_initial_state="$(identity_state "$wrapper_pid" "$wrapper_start_ticks")"
stop_rc=0
child_action=ALREADY_EXITED
wrapper_action=ALREADY_EXITED
if [[ "$child_initial_state" == MISMATCH:* || "$child_initial_state" == UNREADABLE \
      || "$wrapper_initial_state" == MISMATCH:* || "$wrapper_initial_state" == UNREADABLE ]]; then
    stop_rc=1
    child_action=IDENTITY_REJECTED
    wrapper_action=IDENTITY_REJECTED
else
    if [[ "$child_initial_state" == MATCH ]]; then
        child_action=TERM
        kill -TERM "$child_pid" 2>/dev/null || true
        if ! wait_gone "$child_pid" "$child_start_ticks"; then
            child_action=TERM_THEN_KILL
            kill -KILL "$child_pid" 2>/dev/null || true
            wait_gone "$child_pid" "$child_start_ticks" || stop_rc=1
        fi
    fi
    # The wrapper normally observes the child exit and writes the exit record.
    if matches_identity "$wrapper_pid" "$wrapper_start_ticks"; then
        if wait_gone "$wrapper_pid" "$wrapper_start_ticks"; then
            wrapper_action=EXITED_AFTER_CHILD
        else
            wrapper_action=TERM
            kill -TERM "$wrapper_pid" 2>/dev/null || true
            if ! wait_gone "$wrapper_pid" "$wrapper_start_ticks"; then
                wrapper_action=TERM_THEN_KILL
                kill -KILL "$wrapper_pid" 2>/dev/null || true
                wait_gone "$wrapper_pid" "$wrapper_start_ticks" || stop_rc=1
            fi
        fi
    fi
fi
stamp="$(date -u +%Y%m%dT%H%M%S%NZ)"
stop_file="$job_dir/$name.stop-$stamp.env"
[[ ! -e "$stop_file" ]] || { echo "Stop evidence path exists: $stop_file" >&2; exit 1; }
{
    echo "format=rescuesched-background-job-stop-v2"
    echo "hostname=$current_host"
    echo "completed_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "child_pid=$child_pid"
    echo "child_start_ticks=$child_start_ticks"
    echo "child_initial_state=$child_initial_state"
    echo "child_action=$child_action"
    echo "wrapper_pid=$wrapper_pid"
    echo "wrapper_start_ticks=$wrapper_start_ticks"
    echo "wrapper_initial_state=$wrapper_initial_state"
    echo "wrapper_action=$wrapper_action"
    echo "status=$([[ "$stop_rc" -eq 0 ]] && echo PASS || echo FAIL)"
} > "$stop_file"
if (( stop_rc != 0 )); then
    echo "Exact-PID cleanup rejected unsafe PID identity: $stop_file" >&2
    exit 1
fi
echo "Exact-PID cleanup: PASS ($stop_file)"
