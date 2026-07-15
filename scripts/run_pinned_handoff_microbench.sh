#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_pinned_handoff_microbench.sh [options]

Runs the pinned descriptor handoff primitive on topology-valid CPU pairs.
Results are host-local preflight evidence only, not RPC/NIC migration results.

Options:
  --build-dir DIR      CMake build directory (default: cmake-build-physical)
  --out-dir DIR        New result directory (default: physical-results/handoff-<UTC>)
  --iterations N       Samples per scenario and repetition (default: 20000)
  --warmup N           Discarded operations (default: 2000)
  --repetitions N      Repetitions per scenario (default: 3)
  -h, --help           Show this help
EOF
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"
build_dir="cmake-build-physical"
out_dir=""
iterations=20000
warmup=2000
repetitions=3

while (( $# > 0 )); do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --repetitions) repetitions="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in iterations warmup repetitions; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( iterations < 1 || repetitions < 1 )); then
    echo "iterations and repetitions must be positive" >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then build_dir="$root/$build_dir"; fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/handoff-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi
mkdir -p "$out_dir/runs" "$out_dir/metadata"

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel

python3 - "$out_dir/metadata/topology.env" <<'PY'
import os
import pathlib
import sys

allowed = sorted(os.sched_getaffinity(0))
records = []
for cpu in allowed:
    base = pathlib.Path(f"/sys/devices/system/cpu/cpu{cpu}")
    try:
        core = int((base / "topology/core_id").read_text().strip())
        socket = int((base / "topology/physical_package_id").read_text().strip())
    except OSError:
        core = cpu
        socket = 0
    nodes = [path.name for path in base.glob("node[0-9]*")]
    node = int(nodes[0][4:]) if nodes else 0
    records.append((cpu, core, socket, node))

source = records[0]
same_socket = next((record for record in records
                    if record[2] == source[2] and record[1] != source[1]), None)
cross_numa = next((record for record in records if record[3] != source[3]), None)
used_cores = {(source[1], source[2])}
if same_socket:
    used_cores.add((same_socket[1], same_socket[2]))
contender = next((record for record in records
                  if (record[1], record[2]) not in used_cores), None)

with open(sys.argv[1], "w", encoding="ascii") as handle:
    handle.write("allowed_cpus=" + ",".join(str(record[0]) for record in records) + "\n")
    handle.write(f"source_cpu={source[0]}\n")
    handle.write(f"source_socket={source[2]}\n")
    handle.write(f"source_node={source[3]}\n")
    handle.write(f"same_socket_cpu={same_socket[0] if same_socket else -1}\n")
    handle.write(f"cross_numa_cpu={cross_numa[0] if cross_numa else -1}\n")
    handle.write(f"contention_cpu={contender[0] if contender else -1}\n")
    handle.write(f"numa_node_count={len({record[3] for record in records})}\n")
PY

# shellcheck disable=SC1090
source "$out_dir/metadata/topology.env"
binary="$build_dir/rescuesched_handoff_microbench"

run_case() {
    local repetition="$1"
    local scenario="$2"
    local contention="$3"
    local target_cpu="$4"
    local name="${scenario}-${contention}-rep-${repetition}"
    local args=(
        --scenario "$scenario"
        --source-cpu "$source_cpu"
        --iterations "$iterations"
        --warmup "$warmup"
        --output "$out_dir/runs/$name.csv"
        --samples "$out_dir/runs/$name-samples.csv"
    )
    if [[ "$scenario" != "same-core" ]]; then
        args+=(--target-cpu "$target_cpu")
    fi
    if [[ "$contention" == "contended" ]]; then
        args+=(--contended --contention-cpu "$contention_cpu")
    fi
    "$binary" "${args[@]}"
}

for (( repetition = 1; repetition <= repetitions; ++repetition )); do
    run_case "$repetition" same-core unloaded -1
    if (( same_socket_cpu >= 0 )); then
        run_case "$repetition" same-socket unloaded "$same_socket_cpu"
        if (( contention_cpu >= 0 )); then
            run_case "$repetition" same-socket contended "$same_socket_cpu"
        fi
    fi
    if (( cross_numa_cpu >= 0 )); then
        run_case "$repetition" cross-numa unloaded "$cross_numa_cpu"
        if (( contention_cpu >= 0 )); then
            run_case "$repetition" cross-numa contended "$cross_numa_cpu"
        fi
    fi
done

perf_status="UNAVAILABLE"
if command -v perf >/dev/null 2>&1 && (( same_socket_cpu >= 0 )); then
    perf_dir="$out_dir/perf"
    mkdir -p "$perf_dir"
    set +e
    perf stat -x, -e cycles,cache-misses,context-switches \
        -o "$perf_dir/same-socket-unloaded.csv" -- \
        "$binary" \
            --scenario same-socket \
            --source-cpu "$source_cpu" \
            --target-cpu "$same_socket_cpu" \
            --iterations "$iterations" \
            --warmup "$warmup" \
            --output "$perf_dir/same-socket-unloaded-summary.csv" \
        > "$perf_dir/stdout.log" 2> "$perf_dir/stderr.log"
    perf_rc=$?
    set -e
    if (( perf_rc == 0 )); then
        perf_status="PASS"
    else
        perf_status="UNSUPPORTED_OR_PERMISSION_DENIED"
    fi
fi

python3 - "$out_dir" "$perf_status" <<'PY'
import csv
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
perf_status = sys.argv[2]
rows = []
for path in sorted((root / "runs").glob("*.csv")):
    if path.name.endswith("-samples.csv"):
        continue
    with path.open(newline="", encoding="ascii") as handle:
        row = next(csv.DictReader(handle))
    row["run_file"] = str(path.relative_to(root))
    rows.append(row)

groups = {}
for row in rows:
    groups.setdefault((row["scenario"], row["contention"]), []).append(row)

fields = [
    "scenario", "contention", "repetitions", "affinity_all_pass",
    "median_of_run_mean_us", "median_of_run_P50_us", "median_of_run_P95_us",
    "median_of_run_P99_us", "median_of_run_P999_us", "mean_relative_range",
    "status",
]
summary = []
for key, current in sorted(groups.items()):
    means = [float(row["mean_us"]) for row in current]
    run_mean_median = statistics.median(means)
    spread = ((max(means) - min(means)) / run_mean_median
              if run_mean_median else float("inf"))
    affinity = all(row["affinity_success"] == "1" for row in current)
    status = "PASS" if affinity and spread <= 0.25 else "FAIL"
    summary.append({
        "scenario": key[0],
        "contention": key[1],
        "repetitions": len(current),
        "affinity_all_pass": int(affinity),
        "median_of_run_mean_us": run_mean_median,
        "median_of_run_P50_us": statistics.median(
            float(row["P50_us"]) for row in current),
        "median_of_run_P95_us": statistics.median(
            float(row["P95_us"]) for row in current),
        "median_of_run_P99_us": statistics.median(
            float(row["P99_us"]) for row in current),
        "median_of_run_P999_us": statistics.median(
            float(row["P999_us"]) for row in current),
        "mean_relative_range": spread,
        "status": status,
    })

with (root / "summary.csv").open("w", newline="", encoding="ascii") as handle:
    writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    writer.writerows(summary)

topology = {}
for line in (root / "metadata/topology.env").read_text(encoding="ascii").splitlines():
    key, value = line.split("=", 1)
    topology[key] = value

with (root / "HANDOFF_STATUS.txt").open("w", encoding="ascii") as handle:
    handle.write("evidence_scope=local_host_pinned_handoff_microbenchmark_not_rpc_or_network\n")
    handle.write(f"status={'PASS' if summary and all(row['status'] == 'PASS' for row in summary) else 'FAIL'}\n")
    handle.write("cross_numa_status=" + (
        "RUN" if int(topology["cross_numa_cpu"]) >= 0
        else "SKIPPED_UNAVAILABLE_TOPOLOGY") + "\n")
    handle.write(f"perf_stat_status={perf_status}\n")
    handle.write("cache_misses_status=" + (
        "COLLECTED_IN_PERF_SUBDIRECTORY" if perf_status == "PASS"
        else "NOT_COLLECTED_PERF_UNAVAILABLE_OR_PERMISSION_DENIED") + "\n")
PY

{
    echo "evidence_scope=local_host_pinned_handoff_microbenchmark_not_rpc_or_network"
    echo "physical_rpc_runtime_present=NO"
    echo "commit=$(git -C "$root" rev-parse HEAD)"
    echo "dirty_status=$(git -C "$root" status --porcelain | wc -l)"
    echo "build_dir=$build_dir"
    echo "iterations=$iterations"
    echo "warmup=$warmup"
    echo "repetitions=$repetitions"
    echo "clock=std_chrono_steady_clock"
    echo "cycles=x86_rdtscp_when_available"
} > "$out_dir/metadata/manifest.env"
lscpu > "$out_dir/metadata/lscpu.txt" 2>&1 || true
uname -a > "$out_dir/metadata/uname.txt"

(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)

grep -q '^status=PASS$' "$out_dir/HANDOFF_STATUS.txt"
echo "Pinned handoff preflight: PASS"
echo "Results: $out_dir"
echo "Scope: host-local primitive timing only; not RPC, NIC, or paper physical evidence."
