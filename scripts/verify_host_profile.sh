#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=scripts/lib/host_profile_common.sh
source "$script_dir/lib/host_profile_common.sh"

usage() {
    echo "Usage: $0 --profile FILE --plan DIR --mode effective|restored --out-dir DIR [--repo-root DIR]" >&2
}
profile=""; plan=""; mode=""; out_dir=""
repo_root=$(git -C "$script_dir/.." rev-parse --show-toplevel 2>/dev/null || pwd)
while (( $# )); do
    case "$1" in
        --profile) profile=$2; shift 2 ;;
        --plan) plan=$2; shift 2 ;;
        --mode) mode=$2; shift 2 ;;
        --out-dir) out_dir=$2; shift 2 ;;
        --repo-root) repo_root=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ -n "$profile" && -n "$plan" && -n "$mode" && -n "$out_dir" ]] || { usage; exit 2; }
[[ "$mode" == effective || "$mode" == restored ]] || hp_die "invalid verification mode: $mode"
[[ ! -e "$out_dir" ]] || hp_die "verification output already exists: $out_dir"
profile=$(readlink -f "$profile")
plan=$(readlink -f "$plan")
repo_root=$(readlink -f "$repo_root")
hp_load_profile "$profile"
mkdir -p "$out_dir"
start_utc=$(hp_utc_now)
failures=0
checks=0

check_equal() {
    local name=$1 actual=$2 expected=$3 detail=${4:-}
    checks=$((checks + 1))
    if [[ "$actual" == "$expected" ]]; then
        printf 'PASS\t%s\tactual=%q\texpected=%q\t%s\n' "$name" "$actual" "$expected" "$detail" >>"$out_dir/checks.tsv"
    else
        printf 'FAIL\t%s\tactual=%q\texpected=%q\t%s\n' "$name" "$actual" "$expected" "$detail" >>"$out_dir/checks.tsv"
        failures=$((failures + 1))
    fi
}
check_true() {
    local name=$1 condition=$2 actual=$3 expected=$4 detail=${5:-}
    checks=$((checks + 1))
    if [[ "$condition" == true ]]; then
        printf 'PASS\t%s\tactual=%q\texpected=%q\t%s\n' "$name" "$actual" "$expected" "$detail" >>"$out_dir/checks.tsv"
    else
        printf 'FAIL\t%s\tactual=%q\texpected=%q\t%s\n' "$name" "$actual" "$expected" "$detail" >>"$out_dir/checks.tsv"
        failures=$((failures + 1))
    fi
}

: >"$out_dir/checks.tsv"
set +e
"$script_dir/restore_host_state.sh" --plan "$plan" --check-plan >"$out_dir/restore_plan_check.stdout" 2>"$out_dir/restore_plan_check.stderr"
ec=$?
set -e
check_equal restore_plan_check_exit "$ec" 0

set +e
python3 "$script_dir/validate_host_profile.py" --profile "$profile" --repo-root "$repo_root" --out-dir "$out_dir/topology-validator"
ec=$?
set -e
check_equal topology_validator_exit "$ec" 0

# shellcheck disable=SC1091
source "$plan/plan.env"
check_equal profile_sha256 "$(hp_profile_sha256 "$profile")" "$PLAN_PROFILE_SHA256"
check_equal hostname "$(hostname -f 2>/dev/null || hostname)" "$EXPECTED_HOSTNAME"
hp_current_tunable_state "$EXPERIMENT_IFACE" >"$out_dir/canonical_current.tsv"
hp_control_identity_state "$CONTROL_IFACE" >"$out_dir/control_current.tsv"
diff -u "$plan/control_before.tsv" "$out_dir/control_current.tsv" >"$out_dir/control.diff" || true
if cmp -s "$plan/control_before.tsv" "$out_dir/control_current.tsv"; then
    check_equal control_interface_untouched PASS PASS
else
    check_equal control_interface_untouched DIFFERENT PASS "see control.diff"
fi
check_equal experiment_peer_route "$(ip route get "$EXPERIMENT_PEER_IPV4" | awk '{for(i=1;i<=NF;i++) if($i=="dev"){print $(i+1); exit}}')" "$EXPERIMENT_IFACE"
check_equal control_peer_route "$(ip route get "$CONTROL_PEER_IPV4" | awk '{for(i=1;i<=NF;i++) if($i=="dev"){print $(i+1); exit}}')" "$CONTROL_IFACE"
set +e
ping -n -c 2 -W 2 "$CONTROL_PEER_IPV4" >"$out_dir/control_peer_ping.stdout" 2>"$out_dir/control_peer_ping.stderr"
ping_ec=$?
set -e
check_equal control_peer_ping_exit "$ping_ec" 0

if [[ "$mode" == restored ]]; then
    diff -u "$plan/canonical_before.tsv" "$out_dir/canonical_current.tsv" >"$out_dir/restored.diff" || true
    if cmp -s "$plan/canonical_before.tsv" "$out_dir/canonical_current.tsv"; then
        check_equal restored_semantic_state PASS PASS
    else
        check_equal restored_semantic_state DIFFERENT PASS "see restored.diff"
    fi
else
    check_equal combined_queues "$(hp_current_channels "$EXPERIMENT_IFACE")" "$COMBINED_QUEUES"
    read -r current_rx current_tx < <(hp_current_ring "$EXPERIMENT_IFACE")
    check_equal ring_rx "$current_rx" "$RING_RX"
    check_equal ring_tx "$current_tx" "$RING_TX"
    check_equal rx_checksum "$(hp_offload_value "$EXPERIMENT_IFACE" rx-checksumming)" "$RX_CHECKSUM"
    check_equal tx_checksum "$(hp_offload_value "$EXPERIMENT_IFACE" tx-checksumming)" "$TX_CHECKSUM"
    check_equal tso "$(hp_offload_value "$EXPERIMENT_IFACE" tcp-segmentation-offload)" "$TSO"
    check_equal gso "$(hp_offload_value "$EXPERIMENT_IFACE" generic-segmentation-offload)" "$GSO"
    check_equal gro "$(hp_offload_value "$EXPERIMENT_IFACE" generic-receive-offload)" "$GRO"
    check_equal lro "$(hp_offload_value "$EXPERIMENT_IFACE" large-receive-offload)" "$LRO"
    check_equal rmem_max "$(sysctl -n net.core.rmem_max)" "$NET_CORE_RMEM_MAX"
    check_equal wmem_max "$(sysctl -n net.core.wmem_max)" "$NET_CORE_WMEM_MAX"
    check_equal netdev_max_backlog "$(sysctl -n net.core.netdev_max_backlog)" "$NET_CORE_NETDEV_MAX_BACKLOG"
    check_equal irqbalance_active "$(systemctl is-active irqbalance 2>/dev/null || true)" inactive

    governor_lines=$(hp_governor_state)
    if [[ "$GOVERNOR_MODE" == absent ]]; then
        check_equal cpufreq_policy_count "$(grep -c . <<<"$governor_lines" || true)" 0
    else
        policy_count=0; governor_bad=0
        while IFS=$'\t' read -r _path value; do
            [[ -n "${value:-}" ]] || continue
            policy_count=$((policy_count + 1))
            [[ "$value" == "$GOVERNOR_MODE" ]] || governor_bad=$((governor_bad + 1))
        done <<<"$governor_lines"
        check_true governor_all_target "$([[ $policy_count -gt 0 && $governor_bad -eq 0 ]] && echo true || echo false)" "policies=$policy_count bad=$governor_bad" "all $GOVERNOR_MODE"
    fi

    rss_tmp=$(mktemp)
    hp_rss_indirection "$EXPERIMENT_IFACE" >"$rss_tmp"
    check_equal rss_equal_queues "$(hp_rss_equal_count "$rss_tmp" 2>/dev/null || true)" "$COMBINED_QUEUES"
    rm -f "$rss_tmp"
    check_equal rss_key_preserved "$(hp_rss_key "$EXPERIMENT_IFACE")" "$(cat "$plan/rss_key_before.txt")"

    mapfile -t irq_cpus < <(hp_expand_cpulist "$EXPERIMENT_IRQ_CPUS" | tr ',' '\n')
    async_expected=$(hp_canonical_cpulist "$ASYNC_IRQ_CPUS")
    declare -A seen_irq_cpu=()
    comp_count=0; async_count=0
    while IFS=$'\t' read -r irq name affinity; do
        actual=$(hp_canonical_cpulist "$affinity")
        if [[ "$name" =~ ^mlx5_comp([0-9]+)@ ]]; then
            index=${BASH_REMATCH[1]}
            expected=${irq_cpus[$((index % ${#irq_cpus[@]}))]}
            check_equal "completion_irq_${irq}_${index}" "$actual" "$expected" "$name"
            seen_irq_cpu[$expected]=1
            comp_count=$((comp_count + 1))
        elif [[ "$name" =~ ^mlx5_async ]]; then
            check_equal "async_irq_$irq" "$actual" "$async_expected" "$name"
            async_count=$((async_count + 1))
        else
            check_equal "unknown_experiment_irq_$irq" "$name" 'mlx5_comp* or mlx5_async*'
        fi
    done < <(hp_irq_records "$EXPERIMENT_IFACE")
    check_true completion_irq_count "$([[ $comp_count -ge $COMBINED_QUEUES ]] && echo true || echo false)" "$comp_count" ">=$COMBINED_QUEUES"
    check_equal irq_target_cpu_coverage "${#seen_irq_cpu[@]}" "${#irq_cpus[@]}"
    check_true async_irq_present "$([[ $async_count -ge 1 ]] && echo true || echo false)" "$async_count" ">=1"

    xps_expected=$(hp_canonical_cpulist "$XPS_CPUS")
    rps_files=0; xps_files=0
    while IFS=$'\t' read -r rel value; do
        case "$rel" in
            */rps_cpus)
                rps_files=$((rps_files + 1))
                check_equal "rps_${rel//\//_}" "$(hp_mask_to_cpulist "$value")" ''
                ;;
            */xps_cpus)
                xps_files=$((xps_files + 1))
                check_equal "xps_${rel//\//_}" "$(hp_mask_to_cpulist "$value")" "$xps_expected"
                ;;
        esac
    done < <(hp_queue_state "$EXPERIMENT_IFACE")
    check_true rps_files_present "$([[ $rps_files -gt 0 ]] && echo true || echo false)" "$rps_files" ">0"
    check_true xps_files_present "$([[ $xps_files -gt 0 ]] && echo true || echo false)" "$xps_files" ">0"

    set +e
    bash -c 'ulimit -n "$1" && test "$(ulimit -n)" = "$1" && ulimit -n' _ "$OPEN_FILE_LIMIT" >"$out_dir/ulimit_probe.stdout" 2>"$out_dir/ulimit_probe.stderr"
    ulimit_ec=$?
    set -e
    check_equal ulimit_probe_exit "$ulimit_ec" 0
    check_equal ulimit_probe_value "$(cat "$out_dir/ulimit_probe.stdout")" "$OPEN_FILE_LIMIT"

    set +e
    python3 - "$SOCKET_BUFFER_REQUEST_BYTES" >"$out_dir/socket_buffer_probe.stdout" 2>"$out_dir/socket_buffer_probe.stderr" <<'PY'
import socket, sys
requested = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, requested)
s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, requested)
actual_rx = s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
actual_tx = s.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
print(f"requested={requested}")
print(f"actual_rx={actual_rx}")
print(f"actual_tx={actual_tx}")
raise SystemExit(0 if actual_rx >= requested and actual_tx >= requested else 1)
PY
    socket_ec=$?
    set -e
    check_equal socket_buffer_probe_exit "$socket_ec" 0
fi

end_utc=$(hp_utc_now)
status=PASS
(( failures == 0 )) || status=FAIL
{
    printf 'status=%q\nmode=%q\nstart_utc=%q\nend_utc=%q\nchecks=%q\nfailures=%q\nprofile_sha256=%q\n' \
        "$status" "$mode" "$start_utc" "$end_utc" "$checks" "$failures" "$(hp_profile_sha256 "$profile")"
} >"$out_dir/RESULT.env"
(cd "$out_dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS)
(( failures == 0 ))
