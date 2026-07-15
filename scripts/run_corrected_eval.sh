#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_corrected_eval.sh [pilot|holdout|full] [build-dir]

Runs the corrected RescueSched evaluation matrix and paired analysis.
Defaults: tier=pilot, build-dir=build
EOF
}

tier="${1:-pilot}"
build_dir="${2:-build}"

if [[ "$tier" == "-h" || "$tier" == "--help" ]]; then
    usage
    exit 0
fi

case "$tier" in
    pilot)
        warmup=500
        measurement=5000
        seeds="11,23,37,47,59"
        step="step-20-corrected-pilot"
        ;;
    holdout)
        warmup=500
        measurement=5000
        seeds="71,83,97,109,127"
        step="step-20b-corrected-holdout-pilot"
        ;;
    full)
        warmup=200000
        measurement=1000000
        seeds="11,23,37,47,59,71,83,97,109,127"
        step="step-21-corrected-full"
        ;;
    *)
        echo "Unknown tier: $tier" >&2
        usage >&2
        exit 2
        ;;
esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"

if [[ "$build_dir" = /* ]]; then
    exe="$build_dir/simulator"
else
    exe="$root/$build_dir/simulator"
fi

if [[ ! -x "$exe" ]]; then
    echo "Simulator not found or not executable: $exe" >&2
    echo "Build first with: cmake -S . -B $build_dir -DCMAKE_BUILD_TYPE=Release && cmake --build $build_dir" >&2
    exit 1
fi

python_bin="${PYTHON:-python3}"
if ! command -v "$python_bin" >/dev/null 2>&1; then
    echo "Python interpreter not found: $python_bin" >&2
    exit 1
fi

out="$root/artifacts/$step"
mkdir -p "$out"

"$exe" --mode rescue-main --workload W3 --rho "0.70,0.85,0.90" --seed "$seeds" \
    --warmup-requests "$warmup" --measurement-requests "$measurement" \
    --output "$out/w3.csv"

"$exe" --mode rescue-main --workload W2 --rho "0.70,0.85" --seed "$seeds" \
    --warmup-requests "$warmup" --measurement-requests "$measurement" \
    --output "$out/w2.csv"

"$exe" --mode rescue-main --workload W1 --rho "0.70,0.85" --seed "$seeds" \
    --warmup-requests "$warmup" --measurement-requests "$measurement" \
    --output "$out/w1.csv"

"$python_bin" "$script_dir/corrected_eval_analysis.py" \
    --tier "$tier" --out-dir "$out" --inputs \
    "$out/w3.csv" "$out/w2.csv" "$out/w1.csv"

echo "Corrected $tier evaluation complete: $out"
