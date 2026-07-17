#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_background_job.sh --job-dir DIR --name NAME -- COMMAND [ARG...]

Starts one auditable background process. The wrapper writes NAME.pid,
NAME.exit, and NAME-console.log in DIR. It refuses to overwrite prior job
metadata.
EOF
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
exit_file="$job_dir/$name.exit"
log_file="$job_dir/$name-console.log"
command_file="$job_dir/$name-command.txt"
for path in "$pid_file" "$exit_file" "$log_file" "$command_file"; do
    if [[ -e "$path" ]]; then
        echo "Refusing to overwrite job file: $path" >&2
        exit 2
    fi
done

printf '%q ' "$@" > "$command_file"
printf '\n' >> "$command_file"

nohup bash -c '
    exit_file=$1
    shift
    set +e
    "$@"
    rc=$?
    printf "exit_code=%s\nfinished_utc=%s\n" \
        "$rc" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$exit_file"
    exit "$rc"
' bash "$exit_file" "$@" > "$log_file" 2>&1 < /dev/null &
pid=$!
printf '%s\n' "$pid" > "$pid_file"
printf 'pid=%s\nlog=%s\nexit_file=%s\n' "$pid" "$log_file" "$exit_file"
