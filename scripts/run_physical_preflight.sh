#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_physical_preflight.sh [options]

Builds and validates the current simulator on a bare-metal Linux host. It does
not run the full corrected matrix, generate plots, or claim a real RPC/runtime
migration experiment.

Options:
  --build-dir DIR          CMake build directory (default: build)
  --out-dir DIR            Result directory (default: physical-results/<run-id>)
  --expected-commit SHA    Fail unless HEAD matches this full SHA or prefix
  --microbench-runs N      Handoff microbenchmark repetitions (default: 3)
  --jobs N                 Parallel build jobs (default: detected CPU count)
  --require-clean          Fail unless the Git worktree is clean
  --skip-anchor            Skip the short W3 rho=0.85 simulator anchor
  -h, --help               Show this help

Example:
  bash scripts/run_physical_preflight.sh \
    --expected-commit ba81d825eaf1e0b6701e21dbb6462c2a801da0b9
EOF
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"
original_args=("$@")

build_dir="build"
out_dir=""
expected_commit=""
microbench_runs=3
jobs=""
run_anchor=1
require_clean=0

while (( $# > 0 )); do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for --build-dir" >&2; exit 2; }
            build_dir="$2"
            shift 2
            ;;
        --out-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for --out-dir" >&2; exit 2; }
            out_dir="$2"
            shift 2
            ;;
        --expected-commit)
            [[ $# -ge 2 ]] || { echo "Missing value for --expected-commit" >&2; exit 2; }
            expected_commit="$2"
            shift 2
            ;;
        --microbench-runs)
            [[ $# -ge 2 ]] || { echo "Missing value for --microbench-runs" >&2; exit 2; }
            microbench_runs="$2"
            shift 2
            ;;
        --jobs)
            [[ $# -ge 2 ]] || { echo "Missing value for --jobs" >&2; exit 2; }
            jobs="$2"
            shift 2
            ;;
        --require-clean)
            require_clean=1
            shift
            ;;
        --skip-anchor)
            run_anchor=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! "$microbench_runs" =~ ^[0-9]+$ ]] || (( microbench_runs < 3 )); then
    echo "--microbench-runs must be an integer greater than or equal to 3" >&2
    exit 2
fi

if [[ -z "$jobs" ]]; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi
if [[ ! "$jobs" =~ ^[0-9]+$ ]] || (( jobs < 1 )); then
    echo "--jobs must be a positive integer" >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then
    build_dir="$root/$build_dir"
fi

host_id="$(hostname -s 2>/dev/null || hostname)"
host_id="$(printf '%s' "$host_id" | tr -c 'A-Za-z0-9._-' '_')"
run_id="$(date -u +%Y%m%dT%H%M%SZ)-${host_id}"
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/preflight-$run_id"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi

logs_dir="$out_dir/logs"
metadata_dir="$out_dir/metadata"
microbench_dir="$out_dir/microbench"
anchor_dir="$out_dir/anchor"
mkdir -p "$logs_dir" "$metadata_dir" "$microbench_dir" "$anchor_dir"

status_file="$out_dir/PREFLIGHT_STATUS.txt"
script_succeeded=0
on_exit() {
    local rc=$?
    if (( script_succeeded == 0 )); then
        {
            echo "status=FAIL"
            echo "exit_code=$rc"
            echo "finished_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        } > "$status_file"
        echo "Physical preflight failed. Logs: $out_dir" >&2
    fi
}
trap on_exit EXIT

run_logged() {
    local name="$1"
    shift
    echo
    echo "==> $name"
    printf 'Command:'
    printf ' %q' "$@"
    printf '\n'
    "$@" 2>&1 | tee "$logs_dir/$name.log"
}

required_commands=(git cmake c++ python3 sha256sum)
missing_commands=()
for command_name in "${required_commands[@]}"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing_commands+=("$command_name")
    fi
done
if (( ${#missing_commands[@]} > 0 )); then
    echo "Missing required commands: ${missing_commands[*]}" >&2
    echo "On Ubuntu install: git build-essential cmake ninja-build python3" >&2
    exit 1
fi

actual_commit="$(git -C "$root" rev-parse HEAD)"
if [[ -n "$expected_commit" && "$actual_commit" != "$expected_commit"* ]]; then
    echo "Commit mismatch: expected $expected_commit, found $actual_commit" >&2
    exit 1
fi
if (( require_clean == 1 )) && [[ -n "$(git -C "$root" status --porcelain)" ]]; then
    echo "Git worktree is not clean; refusing a formal preflight run." >&2
    git -C "$root" status --short >&2
    exit 1
fi

{
    printf 'bash %q' "$0"
    printf ' %q' "${original_args[@]}"
    printf '\n'
} > "$metadata_dir/command.txt"

{
    echo "run_id=$run_id"
    echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "repository=$root"
    echo "commit=$actual_commit"
    echo "branch=$(git -C "$root" symbolic-ref --quiet --short HEAD 2>/dev/null || echo DETACHED)"
    echo "upstream=$(git -C "$root" rev-parse --abbrev-ref '@{upstream}' 2>/dev/null || echo NONE)"
    echo "build_dir=$build_dir"
    echo "output_dir=$out_dir"
    echo "microbench_runs=$microbench_runs"
    echo "short_anchor_enabled=$run_anchor"
    echo "require_clean=$require_clean"
    echo "physical_rpc_runtime_present=NO"
    echo "result_scope=simulator_build_test_and_host_microbenchmark_only"
} > "$metadata_dir/manifest.env"

git -C "$root" status --short --branch > "$metadata_dir/git-status.txt"
git -C "$root" log -1 --decorate=short --format=fuller > "$metadata_dir/git-head.txt"

{
    echo "# Timestamp"
    date --iso-8601=seconds
    echo
    echo "# Host"
    hostnamectl 2>/dev/null || hostname
    echo
    echo "# Kernel"
    uname -a
    echo
    echo "# OS release"
    cat /etc/os-release 2>/dev/null || true
    echo
    echo "# CPU topology"
    lscpu 2>/dev/null || true
    echo
    echo "# NUMA topology"
    numactl --hardware 2>/dev/null || echo "numactl unavailable"
    echo
    echo "# Memory"
    free -h 2>/dev/null || true
    echo
    echo "# CPU governors"
    if compgen -G '/sys/devices/system/cpu/cpufreq/policy*/scaling_governor' >/dev/null; then
        for governor_file in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
            printf '%s=' "$governor_file"
            cat "$governor_file"
        done
    else
        echo "CPU frequency governor files unavailable"
    fi
    echo
    echo "# SMT"
    cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo "SMT control unavailable"
    cat /sys/devices/system/cpu/smt/active 2>/dev/null || true
    echo
    echo "# Toolchain"
    cmake --version
    c++ --version
    python3 --version
    if command -v ninja >/dev/null 2>&1; then ninja --version; else echo "ninja unavailable"; fi
    echo
    echo "# Network interfaces"
    ip -brief address 2>/dev/null || echo "ip command unavailable"
    echo
    echo "# Interrupt summary"
    head -n 40 /proc/interrupts 2>/dev/null || true
} > "$metadata_dir/host-inventory.txt"

if command -v lstopo-no-graphics >/dev/null 2>&1; then
    lstopo-no-graphics > "$metadata_dir/hwloc.txt" 2>&1 || true
elif command -v lstopo >/dev/null 2>&1; then
    lstopo --of console > "$metadata_dir/hwloc.txt" 2>&1 || true
fi

if command -v ethtool >/dev/null 2>&1 && command -v ip >/dev/null 2>&1; then
    while IFS= read -r interface_name; do
        [[ "$interface_name" == "lo" ]] && continue
        {
            echo "# $interface_name"
            ethtool -i "$interface_name" 2>/dev/null || true
            ethtool -k "$interface_name" 2>/dev/null || true
        } >> "$metadata_dir/network-details.txt"
    done < <(ip -o link show | awk -F': ' '{print $2}' | cut -d@ -f1)
fi

configure_args=(-S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release)
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    configure_args+=(-G Ninja)
fi
run_logged cmake-configure cmake "${configure_args[@]}"
run_logged cmake-build cmake --build "$build_dir" --parallel "$jobs"
run_logged ctest ctest --test-dir "$build_dir" --output-on-failure
run_logged rescue-smoke "$build_dir/simulator" --mode rescue-smoke
grep -q "RescueSched smoke status: PASS" "$logs_dir/rescue-smoke.log"

cp "$build_dir/CMakeCache.txt" "$metadata_dir/CMakeCache.txt"

for (( run = 1; run <= microbench_runs; ++run )); do
    run_logged "microbench-$run" \
        "$build_dir/simulator" \
        --mode rescue-cost-microbench \
        --output "$microbench_dir/migration-cost-$run.csv"
done

python3 - "$microbench_dir" "$out_dir/microbench_summary.csv" \
    "$out_dir/MICROBENCH_STATUS.txt" <<'PY' | tee "$logs_dir/microbench-analysis.log"
import csv
import pathlib
import statistics
import sys

input_dir = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
status_path = pathlib.Path(sys.argv[3])
values = []
for path in sorted(input_dir.glob("migration-cost-*.csv")):
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    match = next(row for row in rows if row["benchmark"] == "cross_thread_cv_handoff")
    values.append((path.name, float(match["per_op_us"])))

samples = [value for _, value in values]
median = statistics.median(samples)
spread = (max(samples) - min(samples)) / median if median else float("inf")
stable = spread <= 0.25
with summary_path.open("w", newline="") as handle:
    writer = csv.writer(handle)
    writer.writerow(["run", "cross_thread_handoff_us"])
    writer.writerows(values)
    writer.writerow(["median", median])
    writer.writerow(["relative_range", spread])
with status_path.open("w") as handle:
    handle.write(f"status={'PASS' if stable else 'FAIL'}\n")
    handle.write(f"median_cross_thread_handoff_us={median:.9f}\n")
    handle.write(f"relative_range={spread:.6f}\n")
    handle.write("threshold=0.25\n")
print(f"cross-thread handoff median: {median:.6f} us")
print(f"relative range: {spread:.2%}")
print(f"microbenchmark stability: {'PASS' if stable else 'FAIL'}")
PY

microbench_status="$(sed -n 's/^status=//p' "$out_dir/MICROBENCH_STATUS.txt")"

if (( run_anchor == 1 )); then
    anchor_csv="$anchor_dir/w3-rho-085-seed-11.csv"
    run_logged short-anchor \
        "$build_dir/simulator" \
        --mode rescue-main \
        --workload W3 \
        --rho 0.85 \
        --seed 11 \
        --warmup-requests 500 \
        --measurement-requests 5000 \
        --output "$anchor_csv"
    run_logged short-anchor-schema \
        python3 "$root/tests/integration/validate_rescue_csv_schema.py" "$anchor_csv"
fi

if [[ "$microbench_status" != "PASS" ]]; then
    echo "Microbenchmark stability failed the 25% relative-range gate." >&2
    echo "Repeat on an idle host after fixing CPU affinity/governor conditions." >&2
    exit 1
fi

{
    echo "status=PASS"
    echo "commit=$actual_commit"
    echo "finished_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "scope=simulator_build_test_and_host_microbenchmark_only"
    echo "physical_rpc_runtime=NOT_IMPLEMENTED"
    echo "results=$out_dir"
} > "$status_file"

(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)

script_succeeded=1
echo
echo "Physical host preflight: PASS"
echo "Results: $out_dir"
echo "Important: this validates the simulator and host handoff microbenchmark;"
echo "it is not a real RPC or physical request-migration result."
