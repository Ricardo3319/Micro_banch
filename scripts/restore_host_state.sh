#!/usr/bin/env bash
set -Euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=scripts/lib/host_profile_common.sh
source "$script_dir/lib/host_profile_common.sh"

usage() {
    echo "Usage: $0 --plan DIR [--log-dir DIR] [--check-plan]" >&2
}
plan=""
log_dir=""
check_only=false
while (( $# )); do
    case "$1" in
        --plan) plan=$2; shift 2 ;;
        --log-dir) log_dir=$2; shift 2 ;;
        --check-plan) check_only=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ -n "$plan" ]] || { usage; exit 2; }
plan=$(readlink -f "$plan")
[[ -d "$plan" && -f "$plan/SHA256SUMS" && -f "$plan/plan.env" ]] || hp_die "invalid restore plan: $plan"
(
    cd "$plan"
    sha256sum -c SHA256SUMS
) >/dev/null
# shellcheck disable=SC1091
source "$plan/plan.env"
[[ ${PLAN_FORMAT:-} == rescuesched-wp3-restore-plan-v1 ]] || hp_die "unsupported restore plan format"
hp_load_profile "$plan/profile.env"
[[ $(hp_profile_sha256 "$plan/profile.env") == "$PLAN_PROFILE_SHA256" ]] || hp_die "profile SHA mismatch inside restore plan"
[[ $(hostname -f 2>/dev/null || hostname) == "$PLAN_HOSTNAME" ]] || hp_die "restore plan belongs to a different hostname"
[[ "$EXPERIMENT_IFACE" == "$PLAN_EXPERIMENT_IFACE" && "$CONTROL_IFACE" == "$PLAN_CONTROL_IFACE" ]] || hp_die "restore plan interface mismatch"
[[ $(hp_iface_ipv4 "$CONTROL_IFACE") == *"$PLAN_CONTROL_IPV4"* ]] || hp_die "control interface identity mismatch"
[[ $(hp_iface_ipv4 "$EXPERIMENT_IFACE") == *"$PLAN_EXPERIMENT_IPV4"* ]] || hp_die "experiment interface identity mismatch"

if [[ "$check_only" == true ]]; then
    printf 'PASS restore plan syntax/integrity/host identity: %s\n' "$plan"
    exit 0
fi
[[ -n "$log_dir" ]] || hp_die "--log-dir is required for an actual restore"
[[ ! -e "$log_dir" ]] || hp_die "restore log directory already exists: $log_dir"
mkdir -p "$log_dir/commands"
start_utc=$(hp_utc_now)
step_index=0
failures=0

record_note() {
    local name=$1 message=$2
    step_index=$((step_index + 1))
    local stem
    stem=$(printf '%03d_%s' "$step_index" "$name")
    {
        printf 'start_utc=%q\nend_utc=%q\nexit_code=0\ncommand=NOTE\n' "$(hp_utc_now)" "$(hp_utc_now)"
        printf 'note=%q\n' "$message"
    } >"$log_dir/commands/$stem.meta.env"
}

run_logged() {
    local name=$1
    shift
    step_index=$((step_index + 1))
    local stem started ended ec
    stem=$(printf '%03d_%s' "$step_index" "$name")
    started=$(hp_utc_now)
    "$@" >"$log_dir/commands/$stem.stdout" 2>"$log_dir/commands/$stem.stderr"
    ec=$?
    ended=$(hp_utc_now)
    {
        printf 'start_utc=%q\nend_utc=%q\nexit_code=%q\n' "$started" "$ended" "$ec"
        printf 'command='; printf '%q ' "$@"; printf '\n'
    } >"$log_dir/commands/$stem.meta.env"
    if (( ec != 0 )); then failures=$((failures + 1)); fi
    return 0
}

skip_if_equal() {
    local name=$1 current=$2 expected=$3
    if [[ "$current" == "$expected" ]]; then
        record_note "$name" "SKIP already effective: $current"
        return 0
    fi
    return 1
}

current_tmp=$(mktemp)
control_tmp=$(mktemp)
trap 'rm -f "$current_tmp" "$control_tmp"' EXIT
if hp_current_tunable_state "$EXPERIMENT_IFACE" >"$current_tmp" && hp_control_identity_state "$CONTROL_IFACE" >"$control_tmp" \
    && cmp -s "$current_tmp" "$plan/canonical_before.tsv" && cmp -s "$control_tmp" "$plan/control_before.tsv"; then
    record_note already_restored "PASS: current semantic state exactly equals restore plan; no mutation performed"
    cp "$current_tmp" "$log_dir/canonical_restored.tsv"
    cp "$control_tmp" "$log_dir/control_restored.tsv"
    diff -u "$plan/canonical_before.tsv" "$current_tmp" >"$log_dir/restored.diff" || true
    diff -u "$plan/control_before.tsv" "$control_tmp" >"$log_dir/control.diff" || true
    {
        printf 'status=PASS_ALREADY_RESTORED\nstart_utc=%q\nend_utc=%q\nmutation_count=0\n' "$start_utc" "$(hp_utc_now)"
    } >"$log_dir/RESULT.env"
    (cd "$log_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
    exit 0
fi

# shellcheck disable=SC1091
source "$plan/scalars_before.env"
# shellcheck disable=SC1091
source "$plan/irqbalance_before.env"

current_active=$(systemctl is-active irqbalance 2>/dev/null || true)
if [[ "$current_active" == active ]]; then
    run_logged stop_irqbalance sudo -n systemctl stop irqbalance
else
    record_note stop_irqbalance "SKIP irqbalance is not active"
fi

current=$(hp_current_channels "$EXPERIMENT_IFACE" 2>/dev/null || true)
if ! skip_if_equal channels "$current" "$channels"; then run_logged channels sudo -n ethtool -L "$EXPERIMENT_IFACE" combined "$channels"; fi
read -r current_rx current_tx < <(hp_current_ring "$EXPERIMENT_IFACE")
if [[ "$current_rx" == "$ring_rx" && "$current_tx" == "$ring_tx" ]]; then
    record_note ring "SKIP already effective: rx=$current_rx tx=$current_tx"
else
    run_logged ring sudo -n ethtool -G "$EXPERIMENT_IFACE" rx "$ring_rx" tx "$ring_tx"
fi

declare -a offload_rows=(
    "rx rx-checksumming $rx_checksum"
    "tx tx-checksumming $tx_checksum"
    "tso tcp-segmentation-offload $tso"
    "gso generic-segmentation-offload $gso"
    "gro generic-receive-offload $gro"
    "lro large-receive-offload $lro"
)
for row in "${offload_rows[@]}"; do
    read -r flag feature expected <<<"$row"
    current=$(hp_offload_value "$EXPERIMENT_IFACE" "$feature")
    if ! skip_if_equal "offload_$flag" "$current" "$expected"; then run_logged "offload_$flag" sudo -n ethtool -K "$EXPERIMENT_IFACE" "$flag" "$expected"; fi
done

rss_tmp=$(mktemp)
hp_rss_indirection "$EXPERIMENT_IFACE" >"$rss_tmp"
current_equal=$(hp_rss_equal_count "$rss_tmp" 2>/dev/null || true)
current_key=$(hp_rss_key "$EXPERIMENT_IFACE")
expected_key=$(cat "$plan/rss_key_before.txt")
expected_equal=$(cat "$plan/rss_equal_before.txt")
if [[ "$current_equal" == "$expected_equal" && "$current_key" == "$expected_key" ]]; then
    record_note rss "SKIP RSS equal=$current_equal and key already restored"
else
    run_logged rss sudo -n ethtool -X "$EXPERIMENT_IFACE" equal "$expected_equal" hkey "$expected_key"
fi
rm -f "$rss_tmp"

restore_sysctl() {
    local name=$1 expected=$2 current_value
    current_value=$(sysctl -n "$name")
    if ! skip_if_equal "sysctl_${name//./_}" "$current_value" "$expected"; then run_logged "sysctl_${name//./_}" sudo -n sysctl -q -w "$name=$expected"; fi
}
restore_sysctl net.core.rmem_max "$rmem_max"
restore_sysctl net.core.wmem_max "$wmem_max"
restore_sysctl net.core.netdev_max_backlog "$netdev_max_backlog"

while IFS=$'\t' read -r path expected; do
    [[ -n "$path" ]] || continue
    if [[ ! -e "$path" ]]; then
        record_note governor_missing "FAIL missing governor path: $path"
        failures=$((failures + 1))
        continue
    fi
    current=$(cat "$path")
    policy_name=$(basename "$(dirname "$path")")
    if ! skip_if_equal "governor_$policy_name" "$current" "$expected"; then run_logged "governor_$policy_name" hp_write_root "$path" "$expected"; fi
done <"$plan/governors_before.tsv"

while IFS=$'\t' read -r rel expected; do
    [[ -n "$rel" ]] || continue
    path="/sys/class/net/$EXPERIMENT_IFACE/queues/$rel"
    if [[ ! -e "$path" ]]; then
        record_note queue_missing "FAIL missing queue control: $rel"
        failures=$((failures + 1))
        continue
    fi
    current=$(cat "$path")
    safe_name=${rel//\//_}; safe_name=${safe_name//-/_}
    if ! skip_if_equal "queue_$safe_name" "$current" "$expected"; then run_logged "queue_$safe_name" hp_write_root "$path" "$expected"; fi
done <"$plan/queues_before.tsv"

current_enabled=$(systemctl is-enabled irqbalance 2>/dev/null || true)
if [[ "$enabled" == enabled && "$current_enabled" != enabled ]]; then
    run_logged irqbalance_enable sudo -n systemctl enable irqbalance
elif [[ "$enabled" == disabled && "$current_enabled" != disabled ]]; then
    run_logged irqbalance_disable sudo -n systemctl disable irqbalance
else
    record_note irqbalance_enabled "SKIP current=$current_enabled expected=$enabled"
fi
current_active=$(systemctl is-active irqbalance 2>/dev/null || true)
if [[ "$active" == active && "$current_active" != active ]]; then
    run_logged irqbalance_start sudo -n systemctl start irqbalance
elif [[ "$active" != active && "$current_active" == active ]]; then
    run_logged irqbalance_stop_final sudo -n systemctl stop irqbalance
else
    record_note irqbalance_active "SKIP current=$current_active expected=$active"
fi

while IFS=$'\t' read -r _old_irq name expected; do
    [[ -n "$name" ]] || continue
    irq=$(hp_find_irq_by_name "$EXPERIMENT_IFACE" "$name")
    if [[ -z "$irq" || ! -e "/proc/irq/$irq/smp_affinity_list" ]]; then
        record_note irq_missing "FAIL missing IRQ vector during restore: $name"
        failures=$((failures + 1))
        continue
    fi
    current=$(cat "/proc/irq/$irq/smp_affinity_list")
    if [[ $(hp_canonical_cpulist "$current") == $(hp_canonical_cpulist "$expected") ]]; then
        record_note "irq_$irq" "SKIP $name already affinity=$current"
    else
        run_logged "irq_$irq" hp_write_root "/proc/irq/$irq/smp_affinity_list" "$expected"
    fi
done <"$plan/irqs_before.tsv"

hp_current_tunable_state "$EXPERIMENT_IFACE" >"$log_dir/canonical_restored.tsv" 2>"$log_dir/canonical_restored.stderr" || failures=$((failures + 1))
hp_control_identity_state "$CONTROL_IFACE" >"$log_dir/control_restored.tsv" 2>"$log_dir/control_restored.stderr" || failures=$((failures + 1))
diff -u "$plan/canonical_before.tsv" "$log_dir/canonical_restored.tsv" >"$log_dir/restored.diff" || failures=$((failures + 1))
diff -u "$plan/control_before.tsv" "$log_dir/control_restored.tsv" >"$log_dir/control.diff" || failures=$((failures + 1))

end_utc=$(hp_utc_now)
status=PASS
(( failures == 0 )) || status=FAIL
{
    printf 'status=%q\nstart_utc=%q\nend_utc=%q\nlogged_steps=%q\nfailures=%q\n' "$status" "$start_utc" "$end_utc" "$step_index" "$failures"
} >"$log_dir/RESULT.env"
(cd "$log_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
(( failures == 0 ))
