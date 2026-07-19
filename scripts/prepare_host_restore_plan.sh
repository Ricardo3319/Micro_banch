#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=scripts/lib/host_profile_common.sh
source "$script_dir/lib/host_profile_common.sh"

usage() {
    echo "Usage: $0 --profile FILE --out-dir DIR [--repo-root DIR]" >&2
}
profile=""
out_dir=""
repo_root=$(git -C "$script_dir/.." rev-parse --show-toplevel 2>/dev/null || pwd)
while (( $# )); do
    case "$1" in
        --profile) profile=$2; shift 2 ;;
        --out-dir) out_dir=$2; shift 2 ;;
        --repo-root) repo_root=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ -n "$profile" && -n "$out_dir" ]] || { usage; exit 2; }
[[ ! -e "$out_dir" ]] || hp_die "restore-plan output already exists: $out_dir"
profile=$(readlink -f "$profile")
repo_root=$(readlink -f "$repo_root")
hp_load_profile "$profile"
for cmd in ethtool ip systemctl sysctl sha256sum python3 sudo; do hp_require_command "$cmd"; done
sudo -n true

parent=$(dirname "$out_dir")
mkdir -p "$parent"
tmp=$(mktemp -d "$parent/.restore-plan.tmp.XXXXXX")
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT
start_utc=$(hp_utc_now)

python3 "$script_dir/validate_host_profile.py" --profile "$profile" --repo-root "$repo_root" --out-dir "$tmp/validator"
cp "$profile" "$tmp/profile.env"

hp_current_tunable_state "$EXPERIMENT_IFACE" >"$tmp/canonical_before.tsv"
hp_control_identity_state "$CONTROL_IFACE" >"$tmp/control_before.tsv"
hp_irq_records "$EXPERIMENT_IFACE" >"$tmp/irqs_before.tsv"
hp_queue_state "$EXPERIMENT_IFACE" >"$tmp/queues_before.tsv"
hp_governor_state >"$tmp/governors_before.tsv"
hp_service_state irqbalance >"$tmp/irqbalance_before.env"
hp_rss_key "$EXPERIMENT_IFACE" >"$tmp/rss_key_before.txt"
hp_rss_indirection "$EXPERIMENT_IFACE" >"$tmp/rss_indirection_before.txt"
hp_rss_equal_count "$tmp/rss_indirection_before.txt" >"$tmp/rss_equal_before.txt"

read -r ring_rx ring_tx < <(hp_current_ring "$EXPERIMENT_IFACE")
{
    printf 'channels=%q\n' "$(hp_current_channels "$EXPERIMENT_IFACE")"
    printf 'ring_rx=%q\nring_tx=%q\n' "$ring_rx" "$ring_tx"
    printf 'rx_checksum=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" rx-checksumming)"
    printf 'tx_checksum=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" tx-checksumming)"
    printf 'tso=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" tcp-segmentation-offload)"
    printf 'gso=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" generic-segmentation-offload)"
    printf 'gro=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" generic-receive-offload)"
    printf 'lro=%q\n' "$(hp_offload_value "$EXPERIMENT_IFACE" large-receive-offload)"
    printf 'rmem_max=%q\n' "$(sysctl -n net.core.rmem_max)"
    printf 'wmem_max=%q\n' "$(sysctl -n net.core.wmem_max)"
    printf 'netdev_max_backlog=%q\n' "$(sysctl -n net.core.netdev_max_backlog)"
} >"$tmp/scalars_before.env"

{
    printf 'branch=%q\n' "$(git -C "$repo_root" branch --show-current)"
    printf 'head=%q\n' "$(git -C "$repo_root" rev-parse HEAD)"
    printf 'tag_object=%q\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG")"
    printf 'tag_target=%q\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG^{}")"
    printf 'status_porcelain_sha256=%q\n' "$(git -C "$repo_root" status --porcelain=v1 | sha256sum | awk '{print $1}')"
} >"$tmp/git_before.env"

end_utc=$(hp_utc_now)
{
    printf 'PLAN_FORMAT=%q\n' 'rescuesched-wp3-restore-plan-v1'
    printf 'PLAN_CREATED_START_UTC=%q\n' "$start_utc"
    printf 'PLAN_CREATED_END_UTC=%q\n' "$end_utc"
    printf 'PLAN_HOSTNAME=%q\n' "$(hostname -f 2>/dev/null || hostname)"
    printf 'PLAN_NODE_NAME=%q\n' "$NODE_NAME"
    printf 'PLAN_EXPERIMENT_IFACE=%q\n' "$EXPERIMENT_IFACE"
    printf 'PLAN_CONTROL_IFACE=%q\n' "$CONTROL_IFACE"
    printf 'PLAN_EXPERIMENT_IPV4=%q\n' "$EXPECTED_EXPERIMENT_IPV4"
    printf 'PLAN_CONTROL_IPV4=%q\n' "$EXPECTED_CONTROL_IPV4"
    printf 'PLAN_PROFILE_SHA256=%q\n' "$(hp_profile_sha256 "$profile")"
    printf 'PLAN_REPO_HEAD=%q\n' "$(git -C "$repo_root" rev-parse HEAD)"
    printf 'PLAN_REPO_BRANCH=%q\n' "$(git -C "$repo_root" branch --show-current)"
    printf 'PLAN_TAG_OBJECT=%q\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG")"
    printf 'PLAN_TAG_TARGET=%q\n' "$(git -C "$repo_root" rev-parse "$EXPECTED_TAG^{}")"
} >"$tmp/plan.env"

(
    cd "$tmp"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
    sha256sum -c SHA256SUMS >/dev/null
)
mv "$tmp" "$out_dir"
trap - EXIT
printf 'restore plan prepared: %s\n' "$out_dir"
