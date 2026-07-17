#!/usr/bin/env python3

import argparse
import csv
import hashlib
from pathlib import Path


POLICIES = (
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
)


def read_status(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def read_client_rows(path: Path) -> dict[int, dict[str, str]]:
    rows: dict[int, dict[str, str]] = {}
    with path.open(newline="", encoding="ascii") as handle:
        for row in csv.DictReader(handle):
            request_id = int(row["request_id"])
            if request_id in rows:
                raise ValueError(f"duplicate request_id {request_id} in {path}")
            rows[request_id] = row
    return rows


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a four-policy, two-client physical RPC smoke run."
    )
    parser.add_argument("run_dir", type=Path)
    args = parser.parse_args()
    root = args.run_dir.resolve()

    failures: list[str] = []
    shard_maps: dict[str, dict[int, str]] = {}
    trace_hashes: set[str] = set()
    row_counts: dict[str, int] = {}

    for policy in POLICIES:
        policy_dir = root / policy
        server_status_path = policy_dir / "server" / "RPC_SERVER_STATUS.txt"
        if not server_status_path.exists():
            failures.append(f"missing {server_status_path}")
            continue
        server_status = read_status(server_status_path)
        if server_status.get("status") != "PASS":
            failures.append(f"{policy} server status is not PASS")

        combined: dict[int, dict[str, str]] = {}
        for client_index in (0, 1):
            client_dir = policy_dir / f"client-{client_index}"
            client_status_path = client_dir / "RPC_CLIENT_STATUS.txt"
            if not client_status_path.exists():
                failures.append(f"missing {client_status_path}")
                continue
            client_status = read_status(client_status_path)
            if client_status.get("status") != "PASS":
                failures.append(f"{policy} client-{client_index} status is not PASS")
            rows_path = client_dir / "client_requests.csv"
            if not rows_path.exists():
                failures.append(f"missing {rows_path}")
                continue
            rows = read_client_rows(rows_path)
            overlap = combined.keys() & rows.keys()
            if overlap:
                failures.append(
                    f"{policy} client partitions overlap at request {min(overlap)}"
                )
            combined.update(rows)
            summary_path = client_dir / "client_summary.csv"
            if summary_path.exists():
                with summary_path.open(newline="", encoding="ascii") as handle:
                    summary = next(csv.DictReader(handle))
                trace_hashes.add(summary["trace_input_file_sha256"])

        expected = int(server_status.get("expected_requests", "-1"))
        if len(combined) != expected:
            failures.append(
                f"{policy} clients cover {len(combined)} requests, expected {expected}"
            )
        if any(row["response_status"] != "received" for row in combined.values()):
            failures.append(f"{policy} has timed-out client requests")
        shard_maps[policy] = {
            request_id: row["ingress_shard"] for request_id, row in combined.items()
        }
        row_counts[policy] = len(combined)

    if len(trace_hashes) != 1:
        failures.append(f"trace input SHA set is not singular: {sorted(trace_hashes)}")
    if len(shard_maps) == len(POLICIES):
        baseline = shard_maps[POLICIES[0]]
        for policy in POLICIES[1:]:
            if shard_maps[policy] != baseline:
                differing = sorted(
                    request_id
                    for request_id in baseline.keys() | shard_maps[policy].keys()
                    if baseline.get(request_id) != shard_maps[policy].get(request_id)
                )
                failures.append(
                    f"{policy} ingress mapping differs from L0 at "
                    f"{len(differing)} requests; first={differing[:5]}"
                )

    status_path = root / "THREE_NODE_RPC_STATUS.txt"
    with status_path.open("w", encoding="ascii") as handle:
        handle.write(f"status={'PASS' if not failures else 'FAIL'}\n")
        handle.write("scope=physical_network_rpc_three_node_smoke\n")
        handle.write(f"policies_validated={len(shard_maps)}\n")
        handle.write(f"trace_input_file_sha256={next(iter(trace_hashes), 'UNAVAILABLE')}\n")
        handle.write("ingress_mapping_identical=" + ("1" if not any(
            "ingress mapping differs" in failure for failure in failures
        ) else "0") + "\n")
        for policy, count in row_counts.items():
            handle.write(f"requests_{policy}={count}\n")
        for index, failure in enumerate(failures, 1):
            handle.write(f"failure_{index}={failure}\n")

    sums_path = root / "SHA256SUMS"
    files = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path not in {sums_path}
    )
    with sums_path.open("w", encoding="ascii") as handle:
        for path in files:
            handle.write(f"{sha256(path)}  {path.relative_to(root)}\n")

    print(status_path.read_text(encoding="ascii"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
