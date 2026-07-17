#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_local_rpc_smoke.sh [options]

Runs the real UDP server/client over loopback with a small frozen trace.

Options:
  --build-dir DIR   CMake build directory (default: build-physical)
  --out-dir DIR     New result directory (default: physical-results/local-rpc-<UTC>)
  --port N          Loopback UDP port (default: 19000)
  --arrival-scale X Replay spacing multiplier (default: 100)
  --skip-build      Reuse an already built and tested directory
  -h, --help        Show this help
EOF
}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="build-physical"
out_dir=""
port=19000
arrival_scale=100
skip_build=0

while (( $# > 0 )); do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --port) port="$2"; shift 2 ;;
        --arrival-scale) arrival_scale="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$port" =~ ^[0-9]+$ ]] || (( port < 1024 || port > 65535 )); then
    echo "--port must be an integer in [1024,65535]" >&2
    exit 2
fi
if ! awk -v value="$arrival_scale" 'BEGIN {
    exit !(value ~ /^[0-9]+([.][0-9]+)?$/ && value + 0 > 0)
}'; then
    echo "--arrival-scale must be a positive number" >&2
    exit 2
fi
if [[ "$build_dir" != /* ]]; then build_dir="$root/$build_dir"; fi
if [[ -z "$out_dir" ]]; then
    out_dir="$root/physical-results/local-rpc-$(date -u +%Y%m%dT%H%M%SZ)"
elif [[ "$out_dir" != /* ]]; then
    out_dir="$root/$out_dir"
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi

trace="$out_dir/trace.csv"
workers=2
mkdir -p "$out_dir"

if (( skip_build == 0 )); then
    cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$build_dir" --parallel
    ctest --test-dir "$build_dir" --output-on-failure
fi
for executable in rescuesched_trace_generator rescuesched_rpc_server rescuesched_rpc_client; do
    [[ -x "$build_dir/$executable" ]] || {
        echo "Missing executable: $build_dir/$executable" >&2
        exit 1
    }
done

"$build_dir/rescuesched_trace_generator" \
    --out "$trace" --workload W3 --rho 0.50 --seed 11 \
    --workers "$workers" --warmup 20 --requests 200 --flow-count 32

server_dir="$out_dir/server"
client_dir="$out_dir/client"
"$build_dir/rescuesched_rpc_server" \
    --trace "$trace" --out-dir "$server_dir" --bind 127.0.0.1 --port "$port" \
    --workers "$workers" --allow-affinity-failure --warmup-requests 20 \
    --idle-timeout-seconds 5 > "$out_dir/server-console.log" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true' EXIT

ready=0
for _ in $(seq 1 50); do
    if grep -q RPC_SERVER_READY "$out_dir/server-console.log" 2>/dev/null; then
        ready=1
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if (( ready == 0 )); then
    echo "UDP server did not become ready" >&2
    sed -n '1,160p' "$out_dir/server-console.log" >&2
    exit 1
fi

"$build_dir/rescuesched_rpc_client" \
    --trace "$trace" --server 127.0.0.1 --bind 127.0.0.1 --port "$port" \
    --out-dir "$client_dir" --workers "$workers" --flow-sockets 32 \
    --source-port-base 21000 --warmup-requests 20 \
    --arrival-scale "$arrival_scale"
wait "$server_pid"
trap - EXIT

grep -q '^status=PASS$' "$server_dir/RPC_SERVER_STATUS.txt"
grep -q '^status=PASS$' "$client_dir/RPC_CLIENT_STATUS.txt"
{
    echo "scope=local_loopback_real_udp_rpc_smoke"
    echo "commit=$(git -C "$root" rev-parse HEAD)"
    echo "build_dir=$build_dir"
    echo "port=$port"
    echo "workers=$workers"
    echo "arrival_scale=$arrival_scale"
    echo "trace_sha256=$(sha256sum "$trace" | awk '{print $1}')"
} > "$out_dir/manifest.env"
(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum \
        > SHA256SUMS
)

echo "Local UDP RPC smoke: PASS"
echo "Results: $out_dir"
