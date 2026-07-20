#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/run_background_job.sh --job-dir DIR --name NAME -- COMMAND [ARG...]

Starts one auditable background process. The wrapper writes exact wrapper/child
PIDs, Linux process start ticks, timestamps, command, output, and exit metadata.
It refuses to overwrite prior job metadata.
USAGE
}

job_dir=""
name=""
while (( $# > 0 )); do
    case "$1" in
        --job-dir) job_dir="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        --) shift; break ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$job_dir" || -z "$name" || $# -eq 0 ]]; then
    usage >&2
    exit 2
fi
if [[ ! "$name" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "Invalid job name: $name" >&2
    exit 2
fi

mkdir -p "$job_dir"
pid_file="$job_dir/$name.pid"
child_pid_file="$job_dir/$name.child.pid"
launch_file="$job_dir/$name.launch.env"
exit_file="$job_dir/$name.exit"
log_file="$job_dir/$name-console.log"
command_file="$job_dir/$name-command.txt"
for path in "$pid_file" "$child_pid_file" "$launch_file" "$exit_file" "$log_file" "$command_file"; do
    if [[ -e "$path" ]]; then
        echo "Refusing to overwrite job file: $path" >&2
        exit 2
    fi
done

printf '%q ' "$@" > "$command_file"
printf '\n' >> "$command_file"

nohup bash -c '
    set -u
    exit_file=$1
    child_pid_file=$2
    launch_file=$3
    name=$4
    shift 4
    started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    "$@" &
    child_pid=$!
    child_start_ticks=$(awk "{print \$22}" "/proc/$child_pid/stat" 2>/dev/null || true)
    wrapper_start_ticks=$(awk "{print \$22}" "/proc/$$/stat" 2>/dev/null || true)
    if [[ ! "$wrapper_start_ticks" =~ ^[0-9]+$ || ! "$child_start_ticks" =~ ^[0-9]+$ ]]; then
        printf "exit_code=125\nfinished_utc=%s\nreason=PID_IDENTITY_CAPTURE_FAILED\n" \
            "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$exit_file"
        kill -TERM "$child_pid" 2>/dev/null || true
        wait "$child_pid" 2>/dev/null || true
        exit 125
    fi
    printf "%s\n" "$child_pid" > "$child_pid_file.tmp.$$"
    mv -- "$child_pid_file.tmp.$$" "$child_pid_file"
    launch_tmp="$launch_file.tmp.$$"
    {
        printf "format=rescuesched-background-job-v2\n"
        printf "manifest_status=PASS\n"
        printf "name=%s\n" "$name"
        printf "hostname=%s\n" "$(hostname -f 2>/dev/null || hostname)"
        printf "started_at_utc=%s\n" "$started_utc"
        printf "wrapper_pid=%s\n" "$$"
        printf "wrapper_start_ticks=%s\n" "$wrapper_start_ticks"
        printf "child_pid=%s\n" "$child_pid"
        printf "child_start_ticks=%s\n" "$child_start_ticks"
    } > "$launch_tmp"
    mv -- "$launch_tmp" "$launch_file"
    set +e
    wait "$child_pid"
    rc=$?
    set -e
    printf "exit_code=%s\nfinished_utc=%s\n" \
        "$rc" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$exit_file"
    exit "$rc"
' bash "$exit_file" "$child_pid_file" "$launch_file" "$name" "$@" \
    > "$log_file" 2>&1 < /dev/null &
wrapper_pid=$!
printf '%s\n' "$wrapper_pid" > "$pid_file.tmp.$$"
mv -- "$pid_file.tmp.$$" "$pid_file"
printf 'wrapper_pid=%s\nlog=%s\nexit_file=%s\nlaunch_file=%s\n' \
    "$wrapper_pid" "$log_file" "$exit_file" "$launch_file"
