#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_physical_preflight.sh [options]

Builds and validates the physical runtime on one Linux host, captures host/NIC
state, runs the real loopback UDP path, and measures the pinned handoff path.

Options:
  --build-dir DIR             CMake build directory (default: build-cloudlab)
  --out-dir DIR               New result directory
  --expected-commit SHA       Require this full SHA or prefix
  --interface IFACE           Experiment NIC for ethtool/RSS capture
  --jobs N                    Parallel build jobs (default: detected CPUs)
  --microbench-runs N         Handoff repetitions (default: 3)
  --microbench-iterations N   Samples per handoff case (default: 20000)
  --microbench-warmup N       Warmup operations per case (default: 2000)
  --require-clean             Refuse a dirty worktree
  -h, --help                  Show this help
EOF
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
original_args=("$@")
build_dir="build-cloudlab"
out_dir=""
expected_commit=""
interface=""
jobs=""
microbench_runs=3
microbench_iterations=20000
microbench_warmup=2000
require_clean=0

while (( $# > 0 )); do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --expected-commit) expected_commit="$2"; shift 2 ;;
        --interface) interface="$2"; shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --microbench-runs) microbench_runs="$2"; shift 2 ;;
        --microbench-iterations) microbench_iterations="$2"; shift 2 ;;
        --microbench-warmup) microbench_warmup="$2"; shift 2 ;;
        --require-clean) require_clean=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$jobs" ]]; then jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"; fi
for value_name in jobs microbench_runs microbench_iterations microbench_warmup; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "$value_name must be a non-negative integer" >&2
        exit 2
    fi
done
if (( jobs < 1 || microbench_runs < 1 || microbench_iterations < 1 )); then
    echo "jobs, microbench runs, and iterations must be positive" >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then build_dir="$root/$build_dir"; fi
host_id="$(hostname -s 2>/dev/null || hostname)"
host_id="$(printf '%s' "$host_id" | tr -c 'A-Za-z0-9._-' '_')"
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/preflight-$(date -u +%Y%m%dT%H%M%SZ)-$host_id"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi

required_commands=(git cmake c++ python3 sha256sum)
for command_name in "${required_commands[@]}"; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing required command: $command_name" >&2
        exit 1
    }
done

actual_commit="$(git -C "$root" rev-parse HEAD)"
if [[ -n "$expected_commit" && "$actual_commit" != "$expected_commit"* ]]; then
    echo "Commit mismatch: expected $expected_commit, found $actual_commit" >&2
    exit 1
fi
if (( require_clean == 1 )) && [[ -n "$(git -C "$root" status --porcelain)" ]]; then
    echo "Git worktree is not clean" >&2
    git -C "$root" status --short >&2
    exit 1
fi

mkdir -p "$out_dir/logs" "$out_dir/metadata"
status_file="$out_dir/PREFLIGHT_STATUS.txt"
succeeded=0
on_exit() {
    local rc=$?
    if (( succeeded == 0 )); then
        printf 'status=FAIL\nexit_code=%s\n' "$rc" > "$status_file"
        echo "Physical preflight failed. Results: $out_dir" >&2
    fi
}
trap on_exit EXIT

run_logged() {
    local name="$1"
    shift
    echo "==> $name"
    "$@" 2>&1 | tee "$out_dir/logs/$name.log"
}

capture_args=(--out-dir "$out_dir/host-state" --label preflight-before)
if [[ -n "$interface" ]]; then capture_args+=(--interface "$interface"); fi
run_logged host-state bash "$root/scripts/capture_physical_host_state.sh" "${capture_args[@]}"

configure_args=(-S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release)
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    configure_args+=(-G Ninja)
fi
run_logged cmake-configure cmake "${configure_args[@]}"
run_logged cmake-build cmake --build "$build_dir" --parallel "$jobs"
run_logged ctest ctest --test-dir "$build_dir" --output-on-failure

run_logged local-rpc bash "$root/scripts/run_local_rpc_smoke.sh" \
    --build-dir "$build_dir" --out-dir "$out_dir/local-rpc" --skip-build
grep -q '^status=PASS$' "$out_dir/local-rpc/server/RPC_SERVER_STATUS.txt"
grep -q '^status=PASS$' "$out_dir/local-rpc/client/RPC_CLIENT_STATUS.txt"

run_logged pinned-handoff bash "$root/scripts/run_pinned_handoff_microbench.sh" \
    --build-dir "$build_dir" --out-dir "$out_dir/pinned-handoff" \
    --iterations "$microbench_iterations" --warmup "$microbench_warmup" \
    --repetitions "$microbench_runs"
grep -q '^status=PASS$' "$out_dir/pinned-handoff/HANDOFF_STATUS.txt"

{
    printf 'bash %q' "$0"
    printf ' %q' "${original_args[@]}"
    printf '\n'
} > "$out_dir/metadata/command.txt"
{
    echo "scope=physical_runtime_host_preflight"
    echo "commit=$actual_commit"
    echo "branch=$(git -C "$root" symbolic-ref --quiet --short HEAD || echo DETACHED)"
    echo "dirty_files=$(git -C "$root" status --porcelain | wc -l)"
    echo "build_dir=$build_dir"
    echo "interface=${interface:-AUTO}"
    echo "physical_rpc_runtime=IMPLEMENTED"
    echo "local_udp_status=PASS"
    echo "pinned_handoff_status=PASS"
} > "$out_dir/metadata/manifest.env"
git -C "$root" status --short --branch > "$out_dir/metadata/git-status.txt"
cp "$build_dir/CMakeCache.txt" "$out_dir/metadata/CMakeCache.txt"

{
    echo "status=PASS"
    echo "commit=$actual_commit"
    echo "physical_rpc_runtime=IMPLEMENTED"
    echo "local_udp_status=PASS"
    echo "pinned_handoff_status=PASS"
    echo "results=$out_dir"
} > "$status_file"
(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum \
        > SHA256SUMS
)

succeeded=1
trap - EXIT
echo "Physical host preflight: PASS"
echo "Results: $out_dir"
