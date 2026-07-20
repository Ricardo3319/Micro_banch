#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/capture_network_counters.sh --interface IFACE --out-dir DIR [--label TEXT]

Capture raw and normalized experiment-NIC plus UDP counters without modifying host state.
Required capture sources fail closed. Optional nstat availability is recorded but does not
change the result.
USAGE
}

interface=""
out_dir=""
label="snapshot"
while (( $# > 0 )); do
    case "$1" in
        --interface) interface="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --label) label="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$interface" || -z "$out_dir" ]]; then
    usage >&2
    exit 2
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi
if [[ ! -d "/sys/class/net/$interface" ]]; then
    echo "Interface does not exist: $interface" >&2
    exit 1
fi
for command_name in date hostname ip ethtool python3 sha256sum; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing required command: $command_name" >&2
        exit 1
    }
done
mkdir -p "$out_dir"

capture_failed=0
run_capture() {
    local requirement="$1"
    local name="$2"
    shift 2
    local start end rc
    start="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    set +e
    "$@" >"$out_dir/$name.stdout" 2>"$out_dir/$name.stderr"
    rc=$?
    set -e
    end="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'requirement=%s\nstarted_at_utc=%s\nended_at_utc=%s\nexit_code=%s\n' \
        "$requirement" "$start" "$end" "$rc" >"$out_dir/$name.metadata.env"
    if [[ "$requirement" == required && "$rc" -ne 0 ]]; then
        printf 'Required network-counter capture failed: %s (exit %s)\n' "$name" "$rc" >&2
        capture_failed=1
    fi
}

{
    echo "format=rescuesched-network-counter-capture-v2"
    echo "label=$label"
    echo "hostname=$(hostname -f 2>/dev/null || hostname)"
    echo "captured_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "interface=$interface"
    echo "ifindex=$(cat "/sys/class/net/$interface/ifindex")"
    echo "operstate=$(cat "/sys/class/net/$interface/operstate")"
} >"$out_dir/metadata.env"

run_capture required ip-link ip -s -d link show dev "$interface"
run_capture required ethtool-stats ethtool -S "$interface"
run_capture required ethtool-driver ethtool -i "$interface"
run_capture required proc-net-snmp cat /proc/net/snmp
run_capture required proc-net-netstat cat /proc/net/netstat
if command -v nstat >/dev/null 2>&1; then
    run_capture optional nstat nstat -asz
else
    printf 'requirement=optional\nstatus=UNAVAILABLE\n' >"$out_dir/nstat.metadata.env"
fi

normalization_rc=0
set +e
python3 - "$out_dir" <<'PY'
import json
import re
import sys
from pathlib import Path

UDP_REQUIRED = ("InErrors", "RcvbufErrors", "SndbufErrors", "InCsumErrors", "MemErrors")
root = Path(sys.argv[1])
normalized = {"format": "rescuesched-network-counters-v2", "nic": {}, "udp": {}}

stats = root / "ethtool-stats.stdout"
for line in stats.read_text(encoding="utf-8", errors="replace").splitlines():
    match = re.match(r"^\s*([^:]+):\s*(-?\d+)\s*$", line)
    if match:
        normalized["nic"][match.group(1).strip()] = int(match.group(2))
if not normalized["nic"]:
    raise SystemExit("ethtool -S produced no parseable integer counters")

lines = (root / "proc-net-snmp.stdout").read_text(
    encoding="ascii", errors="strict"
).splitlines()
for index in range(len(lines) - 1):
    if not lines[index].startswith("Udp:") or not lines[index + 1].startswith("Udp:"):
        continue
    keys = lines[index].split()[1:]
    values = lines[index + 1].split()[1:]
    if len(keys) != len(values):
        raise SystemExit("/proc/net/snmp UDP header/value length mismatch")
    normalized["udp"] = {key: int(value) for key, value in zip(keys, values)}
    break
missing = [name for name in UDP_REQUIRED if name not in normalized["udp"]]
if missing:
    raise SystemExit(f"required UDP counters are missing: {missing}")

(root / "normalized-counters.json").write_text(
    json.dumps(normalized, indent=2, sort_keys=True) + "\n", encoding="ascii"
)
PY
normalization_rc=$?
set -e
printf 'exit_code=%s\n' "$normalization_rc" >"$out_dir/normalization.metadata.env"
if (( normalization_rc != 0 )); then
    capture_failed=1
fi

if [[ -f "$out_dir/normalized-counters.json" ]]; then
    sha256sum "$out_dir/normalized-counters.json" >"$out_dir/normalized-counters.sha256"
fi
{
    echo "status=$([[ "$capture_failed" -eq 0 ]] && echo PASS || echo FAIL)"
    echo "required_sources=ip-link,ethtool-stats,ethtool-driver,proc-net-snmp,proc-net-netstat"
    echo "optional_sources=nstat"
    echo "normalization_exit_code=$normalization_rc"
} >"$out_dir/CAPTURE_STATUS.txt"

if (( capture_failed != 0 )); then
    echo "Network counter capture: FAIL ($out_dir)" >&2
    exit 1
fi
echo "Network counter capture: PASS ($out_dir)"
