#!/usr/bin/env bash
# Common helpers for RescueSched INFOCOM 2027 WP3 host-profile transactions.
# This file deliberately contains no top-level mutations.

hp_die() {
    echo "ERROR: $*" >&2
    return 1
}

hp_utc_now() {
    date -u +%Y-%m-%dT%H:%M:%SZ
}

hp_require_command() {
    command -v "$1" >/dev/null 2>&1 || hp_die "required command unavailable: $1"
}

hp_load_profile() {
    local profile=$1
    [[ -f "$profile" ]] || hp_die "profile does not exist: $profile"
    # Profiles are tracked, declarative shell assignments under operator control.
    # shellcheck disable=SC1090
    source "$profile"
    local required=(
        PROFILE_VERSION NODE_NAME EXPECTED_HOSTNAME EXPERIMENT_IFACE CONTROL_IFACE
        EXPECTED_EXPERIMENT_IPV4 EXPECTED_CONTROL_IPV4 CONTROL_PEER_IPV4
        EXPECTED_NIC_DRIVER EXPECTED_NIC_PCI EXPECTED_NIC_FIRMWARE
        EXPECTED_BRANCH EXPECTED_TAG EXPECTED_TAG_OBJECT EXPECTED_TAG_TARGET
        EXPERIMENT_IRQ_CPUS EXPECTED_PHYSICAL_CORES EXPECTED_LOGICAL_CPUS
        EXPECTED_COMBINED_MAX COMBINED_QUEUES ASYNC_IRQ_CPUS XPS_CPUS
        GOVERNOR_MODE RING_RX RING_TX RX_CHECKSUM TX_CHECKSUM TSO GSO GRO LRO
        NET_CORE_RMEM_MAX NET_CORE_WMEM_MAX NET_CORE_NETDEV_MAX_BACKLOG
        SOCKET_BUFFER_REQUEST_BYTES OPEN_FILE_LIMIT
    )
    local key
    for key in "${required[@]}"; do
        [[ -n ${!key+x} && -n ${!key} ]] || hp_die "profile missing required value: $key"
    done
    [[ "$EXPERIMENT_IFACE" != "$CONTROL_IFACE" ]] || hp_die "experiment and control interfaces must differ"
    [[ "$RING_RX" != 4096 && "$RING_TX" != 4096 ]] || hp_die "ring 4096 is outside WP3 authorization"
}

hp_profile_sha256() {
    sha256sum "$1" | awk '{print $1}'
}

hp_expand_cpulist() {
    python3 - "$1" <<'PY'
import sys
text = sys.argv[1].strip()
values = set()
if text:
    for part in text.split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            lo, hi = map(int, part.split('-', 1))
            if hi < lo:
                raise SystemExit(f"invalid CPU range: {part}")
            values.update(range(lo, hi + 1))
        else:
            values.add(int(part))
print(','.join(map(str, sorted(values))))
PY
}

hp_cpulist_to_mask() {
    python3 - "$1" <<'PY'
import sys
text = sys.argv[1].strip()
cpus = set()
if text:
    for part in text.split(','):
        part = part.strip()
        if '-' in part:
            lo, hi = map(int, part.split('-', 1))
            if hi < lo:
                raise SystemExit(f"invalid CPU range: {part}")
            cpus.update(range(lo, hi + 1))
        elif part:
            cpus.add(int(part))
if not cpus:
    print('00000000')
    raise SystemExit
words = [0] * (max(cpus) // 32 + 1)
for cpu in cpus:
    words[cpu // 32] |= 1 << (cpu % 32)
print(','.join(f'{word:08x}' for word in reversed(words)))
PY
}

hp_mask_to_cpulist() {
    python3 - "$1" <<'PY'
import sys
text = sys.argv[1].strip().lower().replace(',', '')
if not text:
    print('')
    raise SystemExit
value = int(text, 16)
print(','.join(str(cpu) for cpu in range(value.bit_length()) if value & (1 << cpu)))
PY
}

hp_canonical_cpulist() {
    hp_expand_cpulist "$1"
}

hp_current_channels() {
    ethtool -l "$1" | awk '
        /^Current hardware settings:/ {current=1; next}
        current && /^[[:space:]]*Combined:/ {print $2; exit}'
}

hp_max_channels() {
    ethtool -l "$1" | awk '
        /^Pre-set maximums:/ {maximum=1; next}
        /^Current hardware settings:/ {maximum=0}
        maximum && /^[[:space:]]*Combined:/ {print $2; exit}'
}

hp_current_ring() {
    ethtool -g "$1" | awk '
        /^Current hardware settings:/ {current=1; next}
        current && /^[[:space:]]*RX:/ && !rx {rx=$2}
        current && /^[[:space:]]*TX:/ && !tx {tx=$2}
        END {print rx "\t" tx}'
}

hp_offload_value() {
    local iface=$1 key=$2
    ethtool -k "$iface" | awk -F': ' -v key="$key" '$1 == key {split($2,a," "); print a[1]; exit}'
}

hp_rss_key() {
    ethtool -x "$1" | awk '/^RSS hash key:/ {getline; gsub(/^[[:space:]]+|[[:space:]]+$/, ""); print; exit}'
}

hp_rss_indirection() {
    ethtool -x "$1" | awk '
        /^[[:space:]]*[0-9]+:[[:space:]]+/ {
            sub(/^[[:space:]]*[0-9]+:[[:space:]]+/, "")
            for (i=1; i<=NF; ++i) print $i
        }'
}

hp_rss_equal_count() {
    python3 - "$1" <<'PY'
import sys
values = [int(line.strip()) for line in open(sys.argv[1], encoding='utf-8') if line.strip()]
if not values:
    raise SystemExit(1)
for count in range(1, max(values) + 2):
    if all(value == index % count for index, value in enumerate(values)):
        print(count)
        raise SystemExit
raise SystemExit(1)
PY
}

hp_irq_records() {
    local iface=$1 path irq line name affinity
    for path in /sys/class/net/"$iface"/device/msi_irqs/*; do
        [[ -e "$path" ]] || continue
        irq=${path##*/}
        line=$(awk -v n="$irq" '$1 == n ":" {print; exit}' /proc/interrupts)
        name=$(awk '{print $NF}' <<<"$line")
        affinity=$(cat "/proc/irq/$irq/smp_affinity_list")
        printf '%s\t%s\t%s\n' "$irq" "$name" "$affinity"
    done | sort -n -k1,1
}

hp_find_irq_by_name() {
    local iface=$1 wanted=$2
    hp_irq_records "$iface" | awk -F '\t' -v name="$wanted" '$2 == name {print $1; exit}'
}

hp_queue_state() {
    local iface=$1 path rel value
    while IFS= read -r -d '' path; do
        rel=${path#"/sys/class/net/$iface/queues/"}
        value=$(cat "$path")
        printf '%s\t%s\n' "$rel" "$value"
    done < <(find "/sys/class/net/$iface/queues" -maxdepth 2 -type f \
        \( -name rps_cpus -o -name rps_flow_cnt -o -name xps_cpus \) -print0 | sort -z -V)
}

hp_governor_state() {
    local path
    for path in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
        [[ -e "$path" ]] || continue
        printf '%s\t%s\n' "$path" "$(cat "$path")"
    done
}

hp_service_state() {
    local service=$1 active enabled
    active=$(systemctl is-active "$service" 2>/dev/null || true)
    enabled=$(systemctl is-enabled "$service" 2>/dev/null || true)
    printf 'active=%s\nenabled=%s\n' "${active:-unknown}" "${enabled:-unknown}"
}

hp_iface_ipv4() {
    ip -4 -o addr show dev "$1" scope global | awk '{split($4,a,"/"); print a[1]}'
}

hp_iface_driver() {
    ethtool -i "$1" | awk -F': ' '$1 == "driver" {print $2; exit}'
}

hp_iface_firmware() {
    ethtool -i "$1" | awk -F': ' '$1 == "firmware-version" {print $2; exit}'
}

hp_iface_pci() {
    basename "$(readlink -f "/sys/class/net/$1/device")"
}

hp_write_root() {
    local path=$1 value=$2
    printf '%s\n' "$value" | sudo -n tee "$path" >/dev/null
}

hp_current_tunable_state() {
    local iface=$1
    local rx tx
    read -r rx tx < <(hp_current_ring "$iface")
    printf 'channels\t%s\n' "$(hp_current_channels "$iface")"
    printf 'ring_rx\t%s\nring_tx\t%s\n' "$rx" "$tx"
    printf 'offload_rx-checksumming\t%s\n' "$(hp_offload_value "$iface" rx-checksumming)"
    printf 'offload_tx-checksumming\t%s\n' "$(hp_offload_value "$iface" tx-checksumming)"
    printf 'offload_tcp-segmentation-offload\t%s\n' "$(hp_offload_value "$iface" tcp-segmentation-offload)"
    printf 'offload_generic-segmentation-offload\t%s\n' "$(hp_offload_value "$iface" generic-segmentation-offload)"
    printf 'offload_generic-receive-offload\t%s\n' "$(hp_offload_value "$iface" generic-receive-offload)"
    printf 'offload_large-receive-offload\t%s\n' "$(hp_offload_value "$iface" large-receive-offload)"
    printf 'sysctl_net.core.rmem_max\t%s\n' "$(sysctl -n net.core.rmem_max)"
    printf 'sysctl_net.core.wmem_max\t%s\n' "$(sysctl -n net.core.wmem_max)"
    printf 'sysctl_net.core.netdev_max_backlog\t%s\n' "$(sysctl -n net.core.netdev_max_backlog)"
    hp_service_state irqbalance | awk -F= '{print "irqbalance_" $1 "\t" $2}'
    hp_governor_state | sed 's/^/governor\t/'
    printf 'rss_key\t%s\n' "$(hp_rss_key "$iface")"
    hp_rss_indirection "$iface" | paste -sd, - | sed 's/^/rss_indirection\t/'
    hp_irq_records "$iface" | awk -F '\t' '{print $2 "\t" $3}' | sort -V -k1,1 | sed 's/^/irq\t/'
    hp_queue_state "$iface" | sed 's/^/queue\t/'
}

hp_control_identity_state() {
    local iface=$1
    printf 'iface\t%s\n' "$iface"
    printf 'ipv4\t%s\n' "$(hp_iface_ipv4 "$iface" | paste -sd, -)"
    printf 'driver\t%s\n' "$(hp_iface_driver "$iface")"
    printf 'pci\t%s\n' "$(hp_iface_pci "$iface")"
    printf 'link\t%s\n' "$(cat "/sys/class/net/$iface/operstate")"
    printf 'mtu\t%s\n' "$(cat "/sys/class/net/$iface/mtu")"
}
