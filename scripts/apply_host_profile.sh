#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=scripts/lib/host_profile_common.sh
source "$script_dir/lib/host_profile_common.sh"

usage() {
    echo "Usage: $0 --profile FILE --log-dir DIR [--repo-root DIR] [--dry-run | --plan DIR]" >&2
}
profile=""; plan=""; log_dir=""; dry_run=false
repo_root=$(git -C "$script_dir/.." rev-parse --show-toplevel 2>/dev/null || pwd)
while (( $# )); do
    case "$1" in
        --profile) profile=$2; shift 2 ;;
        --plan) plan=$2; shift 2 ;;
        --log-dir) log_dir=$2; shift 2 ;;
        --repo-root) repo_root=$2; shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ -n "$profile" && -n "$log_dir" ]] || { usage; exit 2; }
if [[ "$dry_run" != true && -z "$plan" ]]; then hp_die "--plan is required for actual apply"; fi
[[ ! -e "$log_dir" ]] || hp_die "apply log directory already exists: $log_dir"
profile=$(readlink -f "$profile")
if [[ -n "$plan" ]]; then plan=$(readlink -f "$plan"); fi
repo_root=$(readlink -f "$repo_root")
hp_load_profile "$profile"
for cmd in ethtool ip systemctl sysctl sha256sum python3 sudo; do hp_require_command "$cmd"; done
mkdir -p "$log_dir/commands"
start_utc=$(hp_utc_now)
step_index=0
mutated=false
restore_invoked=false

record_note() {
    local name=$1 message=$2
    step_index=$((step_index + 1))
    local stem now
    stem=$(printf '%03d_%s' "$step_index" "$name")
    now=$(hp_utc_now)
    {
        printf 'start_utc=%q\nend_utc=%q\nexit_code=0\ncommand=NOTE\nnote=%q\n' "$now" "$now" "$message"
    } >"$log_dir/commands/$stem.meta.env"
}

run_logged() {
    local name=$1
    shift
    step_index=$((step_index + 1))
    local stem started ended ec
    stem=$(printf '%03d_%s' "$step_index" "$name")
    started=$(hp_utc_now)
    set +e
    "$@" >"$log_dir/commands/$stem.stdout" 2>"$log_dir/commands/$stem.stderr"
    ec=$?
    set -e
    ended=$(hp_utc_now)
    {
        printf 'start_utc=%q\nend_utc=%q\nexit_code=%q\n' "$started" "$ended" "$ec"
        printf 'command='; printf '%q ' "$@"; printf '\n'
    } >"$log_dir/commands/$stem.meta.env"
    return "$ec"
}

skip_if_equal() {
    local name=$1 current=$2 expected=$3
    if [[ "$current" == "$expected" ]]; then record_note "$name" "SKIP already effective: $current"; return 0; fi
    return 1
}

on_error() {
    local ec=$?
    trap - ERR
    local end
    end=$(hp_utc_now)
    if [[ "$mutated" == true ]]; then
        restore_invoked=true
        set +e
        "$script_dir/restore_host_state.sh" --plan "$plan" --log-dir "$log_dir/automatic-restore" >"$log_dir/automatic-restore.stdout" 2>"$log_dir/automatic-restore.stderr"
        restore_ec=$?
        set -e
    else
        restore_ec=not_needed
    fi
    {
        printf 'status=FAIL_APPLY_OR_VERIFY\nstart_utc=%q\nend_utc=%q\noriginal_exit_code=%q\nrestore_invoked=%q\nrestore_exit_code=%q\n' \
            "$start_utc" "$end" "$ec" "$restore_invoked" "$restore_ec"
    } >"$log_dir/RESULT.env"
    (cd "$log_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
    exit "$ec"
}
trap on_error ERR

run_logged topology_validator python3 "$script_dir/validate_host_profile.py" --profile "$profile" --repo-root "$repo_root" --out-dir "$log_dir/topology-validator"
run_logged sudo_noninteractive sudo -n true

if [[ "$dry_run" == true ]]; then
    hp_current_tunable_state "$EXPERIMENT_IFACE" >"$log_dir/dryrun_current_canonical.tsv"
    hp_control_identity_state "$CONTROL_IFACE" >"$log_dir/dryrun_control.tsv"
    cat >"$log_dir/DRY_RUN_PLAN.txt" <<PLAN
No mutations performed. This dry-run intentionally precedes final restore-plan capture.
1. stop irqbalance (experiment host only)
2. governor mode: $GOVERNOR_MODE
3. sysctl rmem/wmem/backlog: $NET_CORE_RMEM_MAX/$NET_CORE_WMEM_MAX/$NET_CORE_NETDEV_MAX_BACKLOG
4. experiment NIC $EXPERIMENT_IFACE combined queues: $COMBINED_QUEUES
5. experiment NIC ring RX/TX: $RING_RX/$RING_TX (ring 4096 forbidden)
6. experiment NIC offloads RX/TX checksum=$RX_CHECKSUM/$TX_CHECKSUM, TSO/GSO/GRO/LRO=$TSO/$GSO/$GRO/$LRO
7. preserve the node-specific RSS key; equal indirection over $COMBINED_QUEUES queues
8. completion IRQs -> $EXPERIMENT_IRQ_CPUS; async IRQs -> $ASYNC_IRQ_CPUS
9. RPS off; XPS CPUs -> $XPS_CPUS
10. verify effective state and control interface; run no workload
11. after this dry-run, capture an independent restore plan and verify restore_host_state.sh before actual apply
Control interface $CONTROL_IFACE is never a mutation target.
PLAN
    {
        printf 'status=PASS_DRY_RUN\nstart_utc=%q\nend_utc=%q\nprofile_sha256=%q\nmutation_count=0\nrestore_plan_used=false\n' "$start_utc" "$(hp_utc_now)" "$(hp_profile_sha256 "$profile")"
    } >"$log_dir/RESULT.env"
    trap - ERR
    (cd "$log_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
    exit 0
fi

run_logged restore_plan_check "$script_dir/restore_host_state.sh" --plan "$plan" --check-plan
# shellcheck disable=SC1091
source "$plan/plan.env"
check_sha=$(hp_profile_sha256 "$profile")
[[ "$check_sha" == "$PLAN_PROFILE_SHA256" ]] || hp_die "profile SHA drift: current=$check_sha plan=$PLAN_PROFILE_SHA256"

hp_current_tunable_state "$EXPERIMENT_IFACE" >"$log_dir/preapply_canonical.tsv"
hp_control_identity_state "$CONTROL_IFACE" >"$log_dir/preapply_control.tsv"
diff -u "$plan/canonical_before.tsv" "$log_dir/preapply_canonical.tsv" >"$log_dir/preapply.diff" || true
diff -u "$plan/control_before.tsv" "$log_dir/preapply_control.tsv" >"$log_dir/preapply_control.diff" || true
cmp -s "$plan/canonical_before.tsv" "$log_dir/preapply_canonical.tsv" || hp_die "host state drifted after restore plan creation; refuse apply"
cmp -s "$plan/control_before.tsv" "$log_dir/preapply_control.tsv" || hp_die "control interface state drifted; refuse apply"

mutated=true
current=$(systemctl is-active irqbalance 2>/dev/null || true)
if [[ "$current" == inactive ]]; then record_note irqbalance_stop "SKIP already inactive"; else run_logged irqbalance_stop sudo -n systemctl stop irqbalance; fi

if [[ "$GOVERNOR_MODE" == absent ]]; then
    if [[ -n $(hp_governor_state) ]]; then hp_die "profile requires absent cpufreq policies but policies appeared"; fi
    record_note governor "PASS no cpufreq policy exists; no governor value fabricated"
else
    while IFS=$'\t' read -r path current; do
        [[ -n "$path" ]] || continue
        policy_name=$(basename "$(dirname "$path")")
        if ! skip_if_equal "governor_$policy_name" "$current" "$GOVERNOR_MODE"; then run_logged "governor_$policy_name" hp_write_root "$path" "$GOVERNOR_MODE"; fi
    done < <(hp_governor_state)
fi

apply_sysctl() {
    local name=$1 expected=$2 current_value
    current_value=$(sysctl -n "$name")
    if ! skip_if_equal "sysctl_${name//./_}" "$current_value" "$expected"; then run_logged "sysctl_${name//./_}" sudo -n sysctl -q -w "$name=$expected"; fi
}
apply_sysctl net.core.rmem_max "$NET_CORE_RMEM_MAX"
apply_sysctl net.core.wmem_max "$NET_CORE_WMEM_MAX"
apply_sysctl net.core.netdev_max_backlog "$NET_CORE_NETDEV_MAX_BACKLOG"

current=$(hp_current_channels "$EXPERIMENT_IFACE")
if ! skip_if_equal channels "$current" "$COMBINED_QUEUES"; then run_logged channels sudo -n ethtool -L "$EXPERIMENT_IFACE" combined "$COMBINED_QUEUES"; fi
read -r current_rx current_tx < <(hp_current_ring "$EXPERIMENT_IFACE")
if [[ "$current_rx" == "$RING_RX" && "$current_tx" == "$RING_TX" ]]; then record_note ring "SKIP already rx=$current_rx tx=$current_tx"; else run_logged ring sudo -n ethtool -G "$EXPERIMENT_IFACE" rx "$RING_RX" tx "$RING_TX"; fi

declare -a offload_rows=(
    "rx rx-checksumming $RX_CHECKSUM"
    "tx tx-checksumming $TX_CHECKSUM"
    "tso tcp-segmentation-offload $TSO"
    "gso generic-segmentation-offload $GSO"
    "gro generic-receive-offload $GRO"
    "lro large-receive-offload $LRO"
)
for row in "${offload_rows[@]}"; do
    read -r flag feature expected <<<"$row"
    current=$(hp_offload_value "$EXPERIMENT_IFACE" "$feature")
    if ! skip_if_equal "offload_$flag" "$current" "$expected"; then run_logged "offload_$flag" sudo -n ethtool -K "$EXPERIMENT_IFACE" "$flag" "$expected"; fi
done

rss_key=$(cat "$plan/rss_key_before.txt")
rss_tmp=$(mktemp)
hp_rss_indirection "$EXPERIMENT_IFACE" >"$rss_tmp"
current_equal=$(hp_rss_equal_count "$rss_tmp" 2>/dev/null || true)
rm -f "$rss_tmp"
current_key=$(hp_rss_key "$EXPERIMENT_IFACE")
if [[ "$current_equal" == "$COMBINED_QUEUES" && "$current_key" == "$rss_key" ]]; then
    record_note rss "SKIP already equal=$COMBINED_QUEUES and original key preserved"
else
    run_logged rss sudo -n ethtool -X "$EXPERIMENT_IFACE" equal "$COMBINED_QUEUES" hkey "$rss_key"
fi

mapfile -t irq_cpus < <(hp_expand_cpulist "$EXPERIMENT_IRQ_CPUS" | tr ',' '\n')
async_target=$(hp_canonical_cpulist "$ASYNC_IRQ_CPUS")
irq_count=0
while IFS=$'\t' read -r irq name affinity; do
    if [[ "$name" =~ ^mlx5_comp([0-9]+)@ ]]; then
        index=${BASH_REMATCH[1]}
        target=${irq_cpus[$((index % ${#irq_cpus[@]}))]}
    elif [[ "$name" =~ ^mlx5_async ]]; then
        target=$async_target
    else
        hp_die "unknown experiment NIC IRQ vector: irq=$irq name=$name"
    fi
    if [[ $(hp_canonical_cpulist "$affinity") == $(hp_canonical_cpulist "$target") ]]; then
        record_note "irq_$irq" "SKIP $name already affinity=$affinity"
    else
        run_logged "irq_$irq" hp_write_root "/proc/irq/$irq/smp_affinity_list" "$target"
    fi
    irq_count=$((irq_count + 1))
done < <(hp_irq_records "$EXPERIMENT_IFACE")
(( irq_count > 0 )) || hp_die "no experiment NIC IRQs discovered after channel change"

xps_mask=$(hp_cpulist_to_mask "$XPS_CPUS")
while IFS=$'\t' read -r rel value; do
    path="/sys/class/net/$EXPERIMENT_IFACE/queues/$rel"
    safe_name=${rel//\//_}; safe_name=${safe_name//-/_}
    case "$rel" in
        */rps_cpus)
            if [[ -z $(hp_mask_to_cpulist "$value") ]]; then record_note "rps_$safe_name" "SKIP already off"; else run_logged "rps_$safe_name" hp_write_root "$path" 00000000; fi
            ;;
        */xps_cpus)
            if [[ $(hp_mask_to_cpulist "$value") == $(hp_canonical_cpulist "$XPS_CPUS") ]]; then record_note "xps_$safe_name" "SKIP already target CPUs"; else run_logged "xps_$safe_name" hp_write_root "$path" "$xps_mask"; fi
            ;;
    esac
done < <(hp_queue_state "$EXPERIMENT_IFACE")

run_logged ulimit_probe bash -c 'ulimit -n "$1" && test "$(ulimit -n)" = "$1"' _ "$OPEN_FILE_LIMIT"
run_logged effective_verification "$script_dir/verify_host_profile.sh" --profile "$profile" --plan "$plan" --mode effective --out-dir "$log_dir/effective-verification" --repo-root "$repo_root"

hp_current_tunable_state "$EXPERIMENT_IFACE" >"$log_dir/effective_canonical.tsv"
hp_control_identity_state "$CONTROL_IFACE" >"$log_dir/effective_control.tsv"
{
    printf 'status=PASS_APPLIED_AND_EFFECTIVE_VERIFIED\nstart_utc=%q\nend_utc=%q\nprofile_sha256=%q\nlogged_steps=%q\nrestore_invoked=false\n' \
        "$start_utc" "$(hp_utc_now)" "$check_sha" "$step_index"
} >"$log_dir/RESULT.env"
trap - ERR
(cd "$log_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
