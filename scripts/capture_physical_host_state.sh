#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=scripts/lib/host_profile_common.sh
source "$script_dir/lib/host_profile_common.sh"

usage() {
    cat <<'USAGE'
Usage: scripts/capture_physical_host_state.sh --out-dir DIR --label LABEL --profile FILE [--repo-root DIR]

Read-only capture for WP3 host, CPU topology, NIC, IRQ, queue, control-plane,
and Git state. It deliberately does not capture environment variables, command
arguments from processes, credentials, or private-key material.
USAGE
}

out_dir=""
label=""
profile=""
repo_root=$(git -C "$script_dir/.." rev-parse --show-toplevel 2>/dev/null || pwd)
while (( $# > 0 )); do
    case "$1" in
        --out-dir) out_dir=$2; shift 2 ;;
        --label) label=$2; shift 2 ;;
        --profile) profile=$2; shift 2 ;;
        --repo-root) repo_root=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ -n "$out_dir" && -n "$label" && -n "$profile" ]] || { usage >&2; exit 2; }
[[ ! -e "$out_dir" ]] || hp_die "output path already exists: $out_dir"
profile=$(readlink -f "$profile")
repo_root=$(readlink -f "$repo_root")
hp_load_profile "$profile"
mkdir -p "$out_dir/commands"
start_utc=$(hp_utc_now)
command_index=0
failures=0

run_capture() {
    local name=$1
    shift
    command_index=$((command_index + 1))
    local stem
    stem=$(printf '%03d_%s' "$command_index" "$name")
    local started ended ec
    started=$(hp_utc_now)
    set +e
    "$@" >"$out_dir/commands/$stem.stdout" 2>"$out_dir/commands/$stem.stderr"
    ec=$?
    set -e
    ended=$(hp_utc_now)
    {
        printf 'start_utc=%q\n' "$started"
        printf 'end_utc=%q\n' "$ended"
        printf 'exit_code=%q\n' "$ec"
        printf 'command='; printf '%q ' "$@"; printf '\n'
    } >"$out_dir/commands/$stem.meta.env"
    if (( ec != 0 )); then failures=$((failures + 1)); fi
    return 0
}

capture_topology_sysfs() {
    local cpu
    printf 'online\t'; cat /sys/devices/system/cpu/online
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        [[ -d "$cpu/topology" ]] || continue
        printf '%s\tpackage=%s\tcore=%s\tthread_siblings=%s\tcore_cpus=%s\n' \
            "${cpu##*/}" \
            "$(cat "$cpu/topology/physical_package_id")" \
            "$(cat "$cpu/topology/core_id")" \
            "$(cat "$cpu/topology/thread_siblings_list")" \
            "$(cat "$cpu/topology/core_cpus_list")"
    done
}

capture_cpufreq() {
    local policy item
    shopt -s nullglob
    for policy in /sys/devices/system/cpu/cpufreq/policy*; do
        printf '[%s]\n' "$policy"
        for item in affected_cpus related_cpus scaling_available_governors scaling_driver scaling_governor scaling_min_freq scaling_max_freq cpuinfo_min_freq cpuinfo_max_freq scaling_cur_freq; do
            [[ -r "$policy/$item" ]] && printf '%s=%s\n' "$item" "$(cat "$policy/$item")"
        done
    done
    shopt -u nullglob
}

capture_irq_affinity() { hp_irq_records "$EXPERIMENT_IFACE"; }
capture_queues() { hp_queue_state "$EXPERIMENT_IFACE"; }
capture_governors() { hp_governor_state; }
capture_irqbalance() { hp_service_state irqbalance; }
capture_tunable() { hp_current_tunable_state "$EXPERIMENT_IFACE"; }
capture_control() { hp_control_identity_state "$CONTROL_IFACE"; }
capture_ulimit() { printf 'soft_nofile=%s\nhard_nofile=%s\n' "$(ulimit -Sn)" "$(ulimit -Hn)"; }
capture_git() {
    printf 'branch=%s\n' "$(git -C "$repo_root" branch --show-current)"
    printf 'head=%s\n' "$(git -C "$repo_root" rev-parse HEAD)"
    printf 'status_porcelain_v1_begin\n'; git -C "$repo_root" status --porcelain=v1; printf 'status_porcelain_v1_end\n'
    printf 'tag_kind=%s\n' "$(git -C "$repo_root" cat-file -t "$EXPECTED_TAG")"
    printf 'tag_object=%s\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG")"
    printf 'tag_target=%s\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG^{}")"
}
capture_msi_symlinks() { find "/sys/class/net/$EXPERIMENT_IFACE/device/msi_irqs" -mindepth 1 -maxdepth 1 -printf '%f -> %l\n' | sort -n; }
capture_smt() {
    printf 'control='; cat /sys/devices/system/cpu/smt/control 2>/dev/null || true
    printf 'active='; cat /sys/devices/system/cpu/smt/active 2>/dev/null || true
}

cp "$profile" "$out_dir/profile.env"
printf '%s  %s\n' "$(hp_profile_sha256 "$profile")" "$(basename "$profile")" >"$out_dir/profile.sha256"

run_capture date_utc date -u --iso-8601=ns
run_capture hostname hostnamectl
run_capture uname uname -a
run_capture os_release cat /etc/os-release
run_capture lscpu_extended lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ
run_capture lscpu_json lscpu -J
run_capture lscpu_parseable lscpu -p=CPU,CORE,SOCKET,NODE,ONLINE
run_capture topology_sysfs capture_topology_sysfs
run_capture smt capture_smt
run_capture cpufreq capture_cpufreq
run_capture governors capture_governors
run_capture numa numactl --hardware
run_capture memory free -h
run_capture timedatectl timedatectl status
run_capture processes_no_args ps -eLo pid,tid,psr,pcpu,stat,comm
run_capture interrupts cat /proc/interrupts
run_capture softirqs cat /proc/softirqs
run_capture softnet_stat cat /proc/net/softnet_stat
run_capture proc_net_snmp cat /proc/net/snmp
run_capture proc_net_netstat cat /proc/net/netstat
run_capture ip_address ip -details -statistics address
run_capture ip_link ip -details -statistics link
run_capture ip_route ip route show table all
run_capture ip_rule ip rule show
run_capture route_experiment_peer ip route get "$EXPERIMENT_PEER_IPV4"
run_capture route_control_peer ip route get "$CONTROL_PEER_IPV4"
run_capture default_route ip route show default
run_capture ss_udp_no_process_args ss -uan
run_capture experiment_ethtool ethtool "$EXPERIMENT_IFACE"
run_capture experiment_driver ethtool -i "$EXPERIMENT_IFACE"
run_capture experiment_features ethtool -k "$EXPERIMENT_IFACE"
run_capture experiment_channels ethtool -l "$EXPERIMENT_IFACE"
run_capture experiment_ring ethtool -g "$EXPERIMENT_IFACE"
run_capture experiment_rss ethtool -x "$EXPERIMENT_IFACE"
run_capture experiment_stats ethtool -S "$EXPERIMENT_IFACE"
run_capture experiment_msi capture_msi_symlinks
run_capture experiment_irq_affinity capture_irq_affinity
run_capture experiment_queues capture_queues
run_capture irqbalance capture_irqbalance
run_capture sysctl_rmem sysctl net.core.rmem_max
run_capture sysctl_wmem sysctl net.core.wmem_max
run_capture sysctl_backlog sysctl net.core.netdev_max_backlog
run_capture ulimit capture_ulimit
run_capture control_identity capture_control
run_capture control_driver ethtool -i "$CONTROL_IFACE"
run_capture control_link ethtool "$CONTROL_IFACE"
run_capture git_identity capture_git
run_capture sudo_noninteractive sudo -n true
run_capture control_peer_icmp ping -n -c 2 -W 2 "$CONTROL_PEER_IPV4"
run_capture canonical_tunable capture_tunable

end_utc=$(hp_utc_now)
{
    printf 'capture_format=%q\n' 'rescuesched-wp3-host-state-v2'
    printf 'label=%q\n' "$label"
    printf 'capture_start_utc=%q\n' "$start_utc"
    printf 'capture_end_utc=%q\n' "$end_utc"
    printf 'hostname=%q\n' "$(hostname -f 2>/dev/null || hostname)"
    printf 'node_name=%q\n' "$NODE_NAME"
    printf 'experiment_iface=%q\n' "$EXPERIMENT_IFACE"
    printf 'control_iface=%q\n' "$CONTROL_IFACE"
    printf 'profile_sha256=%q\n' "$(hp_profile_sha256 "$profile")"
    printf 'command_count=%q\n' "$command_index"
    printf 'nonzero_command_count=%q\n' "$failures"
    printf 'contains_environment_dump=false\ncontains_process_arguments=false\n'
} >"$out_dir/manifest.env"

(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
)
printf 'capture complete: %s (nonzero optional commands=%s)\n' "$out_dir" "$failures"
