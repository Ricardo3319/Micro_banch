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


def read_single_csv_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="ascii") as handle:
        rows = list(csv.DictReader(handle))
    if len(rows) != 1:
        raise ValueError(f"expected exactly one data row in {path}, found {len(rows)}")
    return rows[0]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a four-policy, two-partition physical RPC run."
    )
    parser.add_argument("run_dir", type=Path)
    args = parser.parse_args()
    root = args.run_dir.resolve()

    failures: list[str] = []
    shard_maps: dict[str, dict[int, str]] = {}
    trace_hashes: set[str] = set()
    start_times: dict[str, set[str]] = {}
    source_port_bases: dict[str, set[str]] = {}
    row_counts: dict[str, int] = {}

    for policy in POLICIES:
        policy_dir = root / policy
        server_status_path = policy_dir / "server" / "RPC_SERVER_STATUS.txt"
        if not server_status_path.exists():
            failures.append(f"missing {server_status_path}")
            continue
        try:
            server_status = read_status(server_status_path)
        except (OSError, UnicodeError) as error:
            failures.append(f"cannot read {server_status_path}: {error}")
            continue
        if server_status.get("status") != "PASS":
            failures.append(f"{policy} server status is not PASS")

        combined: dict[int, dict[str, str]] = {}
        for client_index in (0, 1):
            client_dir = policy_dir / f"client-{client_index}"
            client_status_path = client_dir / "RPC_CLIENT_STATUS.txt"
            if not client_status_path.exists():
                failures.append(f"missing {client_status_path}")
                continue
            try:
                client_status = read_status(client_status_path)
            except (OSError, UnicodeError) as error:
                failures.append(f"cannot read {client_status_path}: {error}")
                continue
            if client_status.get("status") != "PASS":
                failures.append(f"{policy} client-{client_index} status is not PASS")
            rows_path = client_dir / "client_requests.csv"
            if not rows_path.exists():
                failures.append(f"missing {rows_path}")
                continue
            try:
                rows = read_client_rows(rows_path)
            except (csv.Error, OSError, UnicodeError, KeyError, ValueError) as error:
                failures.append(f"cannot parse {rows_path}: {error}")
                continue
            overlap = combined.keys() & rows.keys()
            if overlap:
                failures.append(
                    f"{policy} client partitions overlap at request {min(overlap)}"
                )
            combined.update(rows)
            summary_path = client_dir / "client_summary.csv"
            if not summary_path.exists():
                failures.append(f"missing {summary_path}")
                continue
            try:
                summary = read_single_csv_row(summary_path)
                if summary.get("status") != "PASS":
                    failures.append(
                        f"{policy} client-{client_index} summary status is not PASS"
                    )
                if summary.get("client_index") != str(client_index):
                    failures.append(
                        f"{policy} client-{client_index} summary has wrong client_index"
                    )
                if summary.get("client_count") != "2":
                    failures.append(
                        f"{policy} client-{client_index} summary has wrong client_count"
                    )
                trace_hash = summary.get("trace_input_file_sha256", "")
                if not trace_hash:
                    failures.append(
                        f"{policy} client-{client_index} summary lacks trace hash"
                    )
                else:
                    trace_hashes.add(trace_hash)
                start_time = summary.get("start_at_unix_ns", "")
                if not start_time:
                    failures.append(
                        f"{policy} client-{client_index} summary lacks start time"
                    )
                else:
                    start_times.setdefault(policy, set()).add(start_time)
                source_port_base = summary.get("source_port_base", "")
                if not source_port_base:
                    failures.append(
                        f"{policy} client-{client_index} summary lacks source port base"
                    )
                else:
                    source_port_bases.setdefault(policy, set()).add(source_port_base)
            except (csv.Error, OSError, UnicodeError, KeyError, ValueError) as error:
                failures.append(f"cannot parse {summary_path}: {error}")

        try:
            expected = int(server_status.get("expected_requests", "-1"))
        except ValueError:
            expected = -1
            failures.append(f"{policy} server has invalid expected_requests")
        if len(combined) != expected:
            failures.append(
                f"{policy} clients cover {len(combined)} requests, expected {expected}"
            )
        if any(row.get("response_status") != "received" for row in combined.values()):
            failures.append(f"{policy} has timed-out client requests")
        if any(not row.get("ingress_shard") for row in combined.values()):
            failures.append(f"{policy} has client rows without ingress_shard")
        shard_maps[policy] = {
            request_id: row.get("ingress_shard", "")
            for request_id, row in combined.items()
        }
        row_counts[policy] = len(combined)
        if len(start_times.get(policy, set())) != 1:
            failures.append(f"{policy} clients do not share one absolute start time")
        if len(source_port_bases.get(policy, set())) != 1:
            failures.append(f"{policy} clients do not share one source port base")

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

    status_path = root / "TWO_NODE_RPC_STATUS.txt"
    with status_path.open("w", encoding="ascii") as handle:
        handle.write(f"status={'PASS' if not failures else 'FAIL'}\n")
        handle.write("scope=physical_network_rpc_two_node_smoke\n")
        handle.write(f"policies_validated={len(shard_maps)}\n")
        handle.write(
            f"trace_input_file_sha256={next(iter(trace_hashes), 'UNAVAILABLE')}\n"
        )
        handle.write(
            "ingress_mapping_identical="
            + (
                "1"
                if not any(
                    "ingress mapping differs" in failure for failure in failures
                )
                else "0"
            )
            + "\n"
        )
        for policy, count in row_counts.items():
            handle.write(f"requests_{policy}={count}\n")
        for index, failure in enumerate(failures, 1):
            handle.write(f"failure_{index}={failure}\n")

    sums_path = root / "SHA256SUMS"
    files = sorted(
        path for path in root.rglob("*") if path.is_file() and path != sums_path
    )
    with sums_path.open("w", encoding="ascii") as handle:
        for path in files:
            handle.write(f"{sha256(path)}  {path.relative_to(root)}\n")

    print(status_path.read_text(encoding="ascii"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
