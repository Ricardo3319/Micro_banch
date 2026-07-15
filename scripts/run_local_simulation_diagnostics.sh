#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_local_simulation_diagnostics.sh [smoke|pilot|full] [build-dir] [out-dir]

Runs a one-factor-at-a-time diagnostic matrix at the four frozen anchor points:
W3 rho 0.70/0.85/0.90 and W2 rho 0.85. Outputs are post-freeze diagnostics,
not replacements for artifacts/step-21-corrected-full.

Defaults: tier=smoke, build-dir=build, out-dir under artifacts/step-22-...
EOF
}

tier="${1:-smoke}"
build_dir="${2:-build}"

if [[ "$tier" == "-h" || "$tier" == "--help" ]]; then
    usage
    exit 0
fi

case "$tier" in
    smoke)
        warmup=20
        measurement=200
        seeds="11"
        ;;
    pilot)
        warmup=500
        measurement=5000
        seeds="11,23,37,47,59"
        ;;
    full)
        warmup=200000
        measurement=1000000
        seeds="11,23,37,47,59"
        ;;
    *)
        echo "Unknown tier: $tier" >&2
        usage >&2
        exit 2
        ;;
esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"
out="${3:-$root/artifacts/step-22-local-simulation-diagnostics/$tier}"

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

started_utc="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
git_revision="$(git -C "$root" rev-parse HEAD 2>/dev/null || printf unknown)"
if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [[ -n "$(git -C "$root" status --porcelain 2>/dev/null)" ]]; then
        git_dirty=yes
    else
        git_dirty=no
    fi
else
    git_dirty=unknown
fi
simulator_sha256="$(sha256sum "$exe" | awk '{print $1}')"

for protected in \
    "$root/artifacts/step-20-corrected-pilot" \
    "$root/artifacts/step-20b-corrected-holdout-pilot" \
    "$root/artifacts/step-21-corrected-full"; do
    case "$(realpath -m -- "$out")" in
        "$protected"|"$protected"/*)
            echo "Refusing to write diagnostics into protected evidence: $out" >&2
            exit 2
            ;;
    esac
done

raw="$out/raw"
logs="$out/logs"
profiles="$out/profiles.csv"
if [[ -e "$out" ]] && find "$out" -mindepth 1 -print -quit | grep -q .; then
    echo "Diagnostic output directory is not empty: $out" >&2
    echo "Choose a new output directory to preserve provenance." >&2
    exit 2
fi
mkdir -p "$raw" "$logs"
status_file="$out/RUN_STATUS.txt"
metadata_file="$out/RUN_METADATA.txt"
completed=0
on_exit() {
    local rc=$?
    trap - EXIT
    if [[ "$completed" -ne 1 && -n "${status_file:-}" && -d "$out" ]]; then
        printf 'status=INCOMPLETE\nexit_code=%s\nstarted_utc=%s\n' \
            "$rc" "$started_utc" > "$status_file"
        rm -f "$out/checksums.sha256" "$out/checksums.sha256.tmp"
    fi
    exit "$rc"
}
trap on_exit EXIT

printf 'status=INCOMPLETE\nstarted_utc=%s\n' "$started_utc" > "$status_file"
cat > "$metadata_file" <<EOF
tier=$tier
source_revision=$git_revision
source_worktree_dirty=$git_dirty
simulator_path=$exe
simulator_sha256=$simulator_sha256
runner_sha256=$(sha256sum "$script_dir/run_local_simulation_diagnostics.sh" | awk '{print $1}')
analyzer_sha256=$(sha256sum "$script_dir/local_simulation_diagnostics.py" | awk '{print $1}')
schema_validator_sha256=$(sha256sum "$root/tests/integration/validate_rescue_csv_schema.py" | awk '{print $1}')
started_utc=$started_utc
EOF
printf '%s\n' 'profile,dimension,value,notes,simulator_arguments' > "$profiles"

run_profile() {
    local profile="$1"
    local dimension="$2"
    local value="$3"
    local notes="$4"
    shift 4
    local args=("$@")
    local arg_text="${args[*]:-default}"

    printf '%s,%s,%s,%s,%s\n' \
        "$profile" "$dimension" "$value" "$notes" "$arg_text" >> "$profiles"
    echo "[$tier] profile=$profile dimension=$dimension value=$value"

    "$exe" --mode rescue-main --workload W3 --rho "0.70,0.85,0.90" \
        --seed "$seeds" --warmup-requests "$warmup" \
        --measurement-requests "$measurement" \
        --output "$raw/${profile}__w3.csv" "${args[@]}" \
        > "$logs/${profile}__w3.log" 2>&1

    "$exe" --mode rescue-main --workload W2 --rho "0.85" \
        --seed "$seeds" --warmup-requests "$warmup" \
        --measurement-requests "$measurement" \
        --output "$raw/${profile}__w2.csv" "${args[@]}" \
        > "$logs/${profile}__w2.log" 2>&1
}

run_profile baseline baseline corrected-default frozen-corrected-configuration
run_profile placement-flow-uniform placement flow-affine-uniform simulator-model-not-RSS-measurement \
    --placement flow-affine --flow-count 4096 --flow-zipf-alpha 0
run_profile placement-flow-zipf placement flow-affine-zipf-1.1 simulator-model-not-RSS-measurement \
    --placement flow-affine --flow-count 4096 --flow-zipf-alpha 1.1
run_profile handoff-0 handoff-us 0 configured-simulated-delay --migration-cost-us 0
run_profile handoff-2 handoff-us 2 configured-simulated-delay --migration-cost-us 2
run_profile handoff-5 handoff-us 5 configured-simulated-delay --migration-cost-us 5
run_profile check-2 check-period-us 2 one-factor-at-a-time --check-period-us 2
run_profile check-5 check-period-us 5 one-factor-at-a-time --check-period-us 5
run_profile scan-32 scan-depth 32 one-factor-at-a-time --scan-depth 32
run_profile scan-128 scan-depth 128 one-factor-at-a-time --scan-depth 128
run_profile candidates-8 k-candidates 8 one-factor-at-a-time --k-candidates 8
run_profile candidates-32 k-candidates 32 one-factor-at-a-time --k-candidates 32
run_profile targets-2 h-targets 2 one-factor-at-a-time --h-targets 2
run_profile targets-8 h-targets 8 one-factor-at-a-time --h-targets 8
run_profile budget-2 budget-per-check 2 one-factor-at-a-time --budget-per-check 2
run_profile budget-4 budget-per-check 4 one-factor-at-a-time --budget-per-check 4
run_profile epsilon-0 epsilon-us 0 one-factor-at-a-time --epsilon-us 0
run_profile epsilon-5 epsilon-us 5 one-factor-at-a-time --epsilon-us 5
run_profile ewma-0.01 ewma-alpha 0.01 one-factor-at-a-time --ewma-alpha 0.01
run_profile ewma-0.20 ewma-alpha 0.20 one-factor-at-a-time --ewma-alpha 0.20
run_profile control-accounting-unit control-cost normalized-1us-per-operation \
    accounting-only-does-not-advance-time \
    --control-check-cost-us 1 --control-queue-entry-cost-us 1 \
    --control-candidate-cost-us 1 --control-target-cost-us 1 \
    --control-estimator-update-cost-us 1 --control-poll-cost-us 1

"$python_bin" "$root/tests/integration/validate_rescue_csv_schema.py" "$raw"/*.csv

"$python_bin" "$script_dir/local_simulation_diagnostics.py" \
    --tier "$tier" --profiles "$profiles" --out-dir "$out" \
    --git-revision "$git_revision" --git-dirty "$git_dirty" \
    --simulator-sha256 "$simulator_sha256" --started-utc "$started_utc" \
    --inputs "$raw"/*.csv

completed_utc="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
printf 'status=PASS\nstarted_utc=%s\ncompleted_utc=%s\n' \
    "$started_utc" "$completed_utc" > "$status_file"
(
    cd "$out"
    sha256sum RUN_STATUS.txt RUN_METADATA.txt profiles.csv raw/*.csv logs/*.log \
        summary.csv paired_comparisons.csv sensitivity_vs_default.csv manifest.md
) > "$out/checksums.sha256.tmp"
mv "$out/checksums.sha256.tmp" "$out/checksums.sha256"
completed=1

echo "Local simulation diagnostics complete: $out"
