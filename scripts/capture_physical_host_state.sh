#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/capture_physical_host_state.sh --out-dir DIR --label LABEL [--interface IFACE]

Captures host, CPU, NUMA, clock, process, interrupt, and network state without
modifying the machine. The output is instrumentation evidence, not a benchmark
result.
EOF
}

out_dir=""
label=""
interface=""
while (( $# > 0 )); do
    case "$1" in
        --out-dir) out_dir="$2"; shift 2 ;;
        --label) label="$2"; shift 2 ;;
        --interface) interface="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
if [[ -z "$out_dir" || -z "$label" ]]; then
    usage >&2
    exit 2
fi
if [[ -e "$out_dir" ]]; then
    echo "Output path already exists: $out_dir" >&2
    exit 2
fi
mkdir -p "$out_dir"

run_optional() {
    local output="$1"
    shift
    {
        printf 'command='
        printf '%q ' "$@"
        printf '\n'
        "$@"
    } > "$out_dir/$output" 2>&1 || true
}

{
    echo "label=$label"
    echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "hostname=$(hostname -f 2>/dev/null || hostname)"
    echo "kernel=$(uname -srmo)"
    echo "interface=${interface:-AUTO}"
} > "$out_dir/manifest.env"

run_optional date.txt date --iso-8601=ns
run_optional hostname.txt hostnamectl
run_optional lscpu.txt lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ
run_optional numa.txt numactl --hardware
run_optional memory.txt free -h
run_optional timedatectl.txt timedatectl status
run_optional processes.txt ps -eLo pid,tid,psr,pcpu,stat,comm,args
run_optional interrupts.txt cat /proc/interrupts
run_optional softirqs.txt cat /proc/softirqs
run_optional softnet_stat.txt cat /proc/net/softnet_stat
run_optional proc_net_snmp.txt cat /proc/net/snmp
run_optional proc_net_netstat.txt cat /proc/net/netstat
run_optional ip_address.txt ip -details -statistics address
run_optional ip_link.txt ip -details -statistics link
run_optional ip_route.txt ip route show table all
run_optional ss_udp.txt ss -uapn

if [[ -n "$interface" ]]; then
    run_optional ethtool.txt ethtool "$interface"
    run_optional ethtool_driver.txt ethtool -i "$interface"
    run_optional ethtool_stats.txt ethtool -S "$interface"
    run_optional ethtool_channels.txt ethtool -l "$interface"
    run_optional ethtool_rss.txt ethtool -x "$interface"
fi

if compgen -G '/sys/devices/system/cpu/cpufreq/policy*/scaling_governor' >/dev/null; then
    for path in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
        printf '%s=' "$path"
        cat "$path"
    done > "$out_dir/governors.txt" 2>&1 || true
fi
{
    echo -n "control="; cat /sys/devices/system/cpu/smt/control 2>/dev/null || true
    echo -n "active="; cat /sys/devices/system/cpu/smt/active 2>/dev/null || true
} > "$out_dir/smt.txt"

(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum \
        > SHA256SUMS
)
