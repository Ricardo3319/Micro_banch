#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_local_physical_runtime_smoke.sh [options]

Generates one frozen trace and replays all four policies through the same
pinned-worker in-process synthetic runtime. Outputs are implementation smoke
evidence only, not RPC, CloudLab, or paper physical results.

Options:
  --build-dir DIR       CMake build directory (default: cmake-build-physical)
  --out-dir DIR         Result root (default: physical-results/local-runtime-<UTC>)
  --workers N           Worker count (default: min(4, allowed CPUs))
  --requests N          Measurement request count (default: 1000)
  --warmup N            Warmup request count (default: 100)
  --time-scale X        Wall/logical multiplier (default: 20)
  --stress-repetitions N Repeat all methods N times (default: 1)
  -h, --help            Show this help
EOF
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"
build_dir="cmake-build-physical"
out_dir=""
workers=""
requests=1000
warmup=100
time_scale=20
repetitions=1

while (( $# > 0 )); do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --workers) workers="$2"; shift 2 ;;
        --requests) requests="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --time-scale) time_scale="$2"; shift 2 ;;
        --stress-repetitions) repetitions="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in requests warmup repetitions; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( requests < 1 || repetitions < 1 )); then
    echo "requests and stress repetitions must be positive" >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then build_dir="$root/$build_dir"; fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/local-runtime-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi

allowed_cpus="$(python3 - <<'PY'
import os
print(",".join(str(cpu) for cpu in sorted(os.sched_getaffinity(0))))
PY
)"
allowed_count="$(awk -F, '{print NF}' <<<"$allowed_cpus")"
if [[ -z "$workers" ]]; then
    if (( allowed_count < 4 )); then workers="$allowed_count"; else workers=4; fi
fi
if [[ ! "$workers" =~ ^[0-9]+$ ]] || (( workers < 1 || workers > allowed_count )); then
    echo "workers must be in [1,$allowed_count]" >&2
    exit 2
fi
cpu_list="$(cut -d, -f1-"$workers" <<<"$allowed_cpus")"

mkdir -p "$out_dir/trace" "$out_dir/runs" "$out_dir/metadata"

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

trace_path="$out_dir/trace/w3-rho-085-seed-11.csv"
"$build_dir/simulator" --mode trace-generate \
    --workload W3 --rho 0.85 --seed 11 \
    --warmup-requests "$warmup" \
    --measurement-requests "$requests" \
    --trace-core-count "$workers" \
    --trace-out "$trace_path"

policies=(L0_RandomCore L1_WorkStealingPolling M0_AltoThreshold M1_RescueSched)
for (( repetition = 1; repetition <= repetitions; ++repetition )); do
    for policy in "${policies[@]}"; do
        run_dir="$out_dir/runs/rep-$repetition/$policy"
        "$build_dir/rescuesched_physical_runtime" \
            --trace "$trace_path" \
            --out-dir "$run_dir" \
            --policy "$policy" \
            --workers "$workers" \
            --cpus "$cpu_list" \
            --warmup-requests "$warmup" \
            --time-scale "$time_scale" \
            --workload-label W3 \
            --rho-label 0.85 \
            --seed-label 11 \
            --repetition "$repetition"
    done
done

{
    echo "evidence_scope=local_synthetic_runtime_implementation_validation"
    echo "physical_rpc_runtime_present=NO"
    echo "commit=$(git -C "$root" rev-parse HEAD)"
    echo "dirty_status=$(git -C "$root" status --porcelain | wc -l)"
    echo "trace=$trace_path"
    echo "workers=$workers"
    echo "cpu_ids=$cpu_list"
    echo "warmup_requests=$warmup"
    echo "measurement_requests=$requests"
    echo "time_scale=$time_scale"
    echo "repetitions=$repetitions"
} > "$out_dir/metadata/manifest.env"

(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)

echo "Local synthetic runtime smoke: PASS"
echo "Results: $out_dir"
echo "Scope: implementation validation only; not RPC, CloudLab, or paper evidence."
