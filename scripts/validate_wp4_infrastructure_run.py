#!/usr/bin/env python3
"""Blind WP4 infrastructure validator.

The decision stage consumes only ``wp4_sanitized_blind_input.json``, which is
constructed from the explicit allowlist emitted beside it. Deadline, latency,
goodput, migration effectiveness, and method-effect columns are neither copied
into the sanitized input nor available to the decision functions.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import subprocess
from pathlib import Path
from typing import Any

VALIDATOR_VERSION = "rescuesched-wp4-blind-infrastructure-v2"
KNOWN_POLICIES = (
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
)
DEFAULT_POLICIES = ("L1_WorkStealingPolling", "M1_RescueSched")
UDP_DROP_FIELDS = ("InErrors", "RcvbufErrors", "SndbufErrors", "InCsumErrors", "MemErrors")
NIC_DROP_PATTERN_TEXT = (
    r"^tx_queue_dropped$",
    r"^rx_out_of_buffer$",
    r"^rx_crc_errors_phy$",
    r"^rx_in_range_len_errors_phy$",
    r"^rx_discards_phy$",
    r"^tx_discards_phy$",
    r"^tx_errors_phy$",
    r"^rx_prio\d+_discards$",
    r"^rx(?:\d+_)?xdp_drop$",
    r"^rx(?:\d+_)?oversize_pkts_sw_drop$",
    r"^tx\d+_dropped$",
)
NIC_DROP_PATTERNS = tuple(re.compile(text) for text in NIC_DROP_PATTERN_TEXT)

SERVER_STATUS_FIELDS = (
    "status", "classification", "expected_requests", "accepted_requests",
    "responses_enqueued", "responses_sent", "invalid_packets", "duplicate_packets",
    "unknown_requests", "flow_mismatch_requests", "response_enqueue_failures",
    "response_send_failures", "receiver_affinity_pass",
    "response_sender_affinity_pass", "scheduler_affinity_pass",
    "server_main_affinity_pass", "receiver_internal_failures", "idle_termination",
    "runtime_invariants_pass",
)
RUNTIME_SUMMARY_FIELDS = (
    "policy", "total_requests", "measurement_requests", "completed_requests",
    "cancelled_requests", "duplicate_execution_count", "duplicate_completion_count",
    "lost_descriptor_count", "nonzero_reservation_count", "affinity_failure_count",
    "scheduler_cpu_id", "scheduler_affinity_ok", "scheduler_epochs_scheduled",
    "scheduler_epochs_executed", "scheduler_epochs_missed",
    "l1_poll_epochs_scheduled", "l1_poll_epochs_missed", "classification",
    "invariants_pass",
)
CLIENT_STATUS_FIELDS = (
    "status", "total_requests", "responses", "timeouts", "send_failures",
    "invalid_responses", "duplicate_responses", "sender_cpu", "receiver_cpu",
    "sender_affinity_pass", "receiver_affinity_pass", "send_lag_p99_us",
)
CLIENT_REQUEST_FIELDS = (
    "request_id", "planned_send_us", "actual_send_us", "response_status",
    "ingress_shard",
)
CAPTURE_STATUS_FIELDS = ("status", "normalization_exit_code")

FORBIDDEN_CATEGORIES = (
    "deadline miss/result or deadline violation fields",
    "tail or percentile latency fields",
    "goodput or application-performance fields",
    "migration/method improvement fields",
    "policy-effect comparison fields",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="ascii")


def repo_commit(repo_root: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repo_root), "rev-parse", "HEAD"], text=True
    ).strip()


def read_status_selected(path: Path, fields: tuple[str, ...]) -> dict[str, str]:
    selected: dict[str, str] = {}
    wanted = set(fields)
    for line in path.read_text(encoding="ascii").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in wanted:
            selected[key] = value
    missing = [name for name in fields if name not in selected]
    if missing:
        raise ValueError(f"missing allowlisted fields in {path}: {missing}")
    return {name: selected[name] for name in fields}


def read_one_csv_selected(path: Path, fields: tuple[str, ...]) -> dict[str, str]:
    with path.open(newline="", encoding="ascii") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"missing CSV header in {path}")
        missing = [name for name in fields if name not in reader.fieldnames]
        if missing:
            raise ValueError(f"missing allowlisted columns in {path}: {missing}")
        rows = list(reader)
    if len(rows) != 1:
        raise ValueError(f"expected one data row in {path}, found {len(rows)}")
    return {name: rows[0][name] for name in fields}


def read_request_timing_selected(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="ascii") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"missing CSV header in {path}")
        missing = [name for name in CLIENT_REQUEST_FIELDS if name not in reader.fieldnames]
        if missing:
            raise ValueError(f"missing allowlisted columns in {path}: {missing}")
        return [
            {name: row[name] for name in CLIENT_REQUEST_FIELDS}
            for row in reader
        ]


def is_nic_drop_counter(name: str) -> bool:
    return any(pattern.fullmatch(name) for pattern in NIC_DROP_PATTERNS)


def read_counter_selected(path: Path) -> dict[str, dict[str, int]]:
    data = json.loads(path.read_text(encoding="ascii"))
    raw_nic = data.get("nic")
    raw_udp = data.get("udp")
    if not isinstance(raw_nic, dict) or not isinstance(raw_udp, dict):
        raise ValueError(f"counter scopes are missing in {path}")
    nic = {
        str(name): int(value)
        for name, value in raw_nic.items()
        if is_nic_drop_counter(str(name))
    }
    if not nic:
        raise ValueError(f"no allowlisted NIC drop/error counters in {path}")
    missing_udp = [name for name in UDP_DROP_FIELDS if name not in raw_udp]
    if missing_udp:
        raise ValueError(f"required UDP counters missing in {path}: {missing_udp}")
    udp = {name: int(raw_udp[name]) for name in UDP_DROP_FIELDS}
    return {"nic_drop_or_error": dict(sorted(nic.items())), "udp_drop_or_error": udp}


def allowlist_artifact() -> dict[str, Any]:
    return {
        "format": "rescuesched-wp4-blind-allowlist-v1",
        "validator_version": VALIDATOR_VERSION,
        "decision_input": "wp4_sanitized_blind_input.json only",
        "sources": {
            "*/server/RPC_SERVER_STATUS.txt": list(SERVER_STATUS_FIELDS),
            "*/server/summary.csv": list(RUNTIME_SUMMARY_FIELDS),
            "*/client-{0,1}/RPC_CLIENT_STATUS.txt": list(CLIENT_STATUS_FIELDS),
            "*/client-{0,1}/client_requests.csv": list(CLIENT_REQUEST_FIELDS),
            "*/network-counters/node{0,1}-{before,after}/CAPTURE_STATUS.txt": list(
                CAPTURE_STATUS_FIELDS
            ),
            "*/network-counters/node{0,1}-{before,after}/normalized-counters.json": {
                "nic": list(NIC_DROP_PATTERN_TEXT),
                "udp": list(UDP_DROP_FIELDS),
            },
        },
        "forbidden_categories": list(FORBIDDEN_CATEGORIES),
    }


def collect_sanitized(root: Path, policies: tuple[str, ...], commit: str,
                      thresholds: dict[str, float]) -> dict[str, Any]:
    collection_failures: list[str] = []
    policy_data: dict[str, Any] = {}
    for policy in policies:
        policy_dir = root / policy
        entry: dict[str, Any] = {"clients": {}, "network_counters": {}}
        readers = (
            ("server_status", policy_dir / "server" / "RPC_SERVER_STATUS.txt",
             lambda path: read_status_selected(path, SERVER_STATUS_FIELDS)),
            ("runtime_summary", policy_dir / "server" / "summary.csv",
             lambda path: read_one_csv_selected(path, RUNTIME_SUMMARY_FIELDS)),
        )
        for key, path, reader in readers:
            try:
                entry[key] = reader(path)
            except (OSError, UnicodeError, csv.Error, ValueError, TypeError,
                    json.JSONDecodeError) as error:
                collection_failures.append(f"{policy}: cannot sanitize {path.name}: {error}")
        for client_index in (0, 1):
            client_dir = policy_dir / f"client-{client_index}"
            client: dict[str, Any] = {}
            try:
                client["status"] = read_status_selected(
                    client_dir / "RPC_CLIENT_STATUS.txt", CLIENT_STATUS_FIELDS
                )
            except (OSError, UnicodeError, ValueError) as error:
                collection_failures.append(
                    f"{policy} client-{client_index}: cannot sanitize status: {error}"
                )
            try:
                client["requests"] = read_request_timing_selected(
                    client_dir / "client_requests.csv"
                )
            except (OSError, UnicodeError, csv.Error, ValueError) as error:
                collection_failures.append(
                    f"{policy} client-{client_index}: cannot sanitize requests: {error}"
                )
            entry["clients"][str(client_index)] = client
        for node in ("node0", "node1"):
            snapshots: dict[str, Any] = {}
            for when in ("before", "after"):
                capture_dir = policy_dir / "network-counters" / f"{node}-{when}"
                snapshot: dict[str, Any] = {}
                try:
                    snapshot["capture_status"] = read_status_selected(
                        capture_dir / "CAPTURE_STATUS.txt", CAPTURE_STATUS_FIELDS
                    )
                except (OSError, UnicodeError, ValueError) as error:
                    collection_failures.append(
                        f"{policy} {node}-{when}: cannot sanitize capture status: {error}"
                    )
                try:
                    snapshot["counters"] = read_counter_selected(
                        capture_dir / "normalized-counters.json"
                    )
                except (OSError, UnicodeError, ValueError, TypeError,
                        json.JSONDecodeError) as error:
                    collection_failures.append(
                        f"{policy} {node}-{when}: cannot sanitize counters: {error}"
                    )
                snapshots[when] = snapshot
            entry["network_counters"][node] = snapshots
        policy_data[policy] = entry
    return {
        "format": "rescuesched-wp4-sanitized-blind-input-v1",
        "validator_version": VALIDATOR_VERSION,
        "validator_commit": commit,
        "policies": list(policies),
        "thresholds": thresholds,
        "collection_failures": collection_failures,
        "policy_data": policy_data,
    }


def to_int(value: Any, label: str, failures: list[str]) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        failures.append(f"{label} is not an integer: {value!r}")
        return -1


def to_float(value: Any, label: str, failures: list[str]) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        failures.append(f"{label} is not numeric: {value!r}")
        return math.inf
    if not math.isfinite(result):
        failures.append(f"{label} is not finite: {value!r}")
        return math.inf
    return result


def require_zero(mapping: dict[str, str], key: str, prefix: str,
                 failures: list[str]) -> None:
    if to_int(mapping.get(key), f"{prefix} {key}", failures) != 0:
        failures.append(f"{prefix} {key}={mapping.get(key, 'MISSING')}, expected 0")


def nearest_rank(values: list[float], quantile: float) -> float:
    if not values:
        return math.inf
    ordered = sorted(values)
    rank = max(1, math.ceil(quantile * len(ordered)))
    return ordered[min(rank - 1, len(ordered) - 1)]


def validate_sanitized(data: dict[str, Any]) -> tuple[list[str], list[dict[str, Any]],
                                                     list[dict[str, Any]]]:
    failures = list(data.get("collection_failures", []))
    metrics: list[dict[str, Any]] = []
    counter_rows: list[dict[str, Any]] = []
    shard_maps: dict[str, dict[int, str]] = {}
    thresholds = data["thresholds"]

    for policy in data["policies"]:
        entry = data["policy_data"].get(policy, {})
        server = entry.get("server_status")
        runtime = entry.get("runtime_summary")
        clients = entry.get("clients", {})
        if not isinstance(server, dict) or not isinstance(runtime, dict):
            failures.append(f"{policy}: sanitized server/runtime data is incomplete")
            continue
        if server.get("status") != "PASS" or server.get("classification") != "VALID_COMPLETE":
            failures.append(f"{policy}: server status/classification is not valid complete")
        for key in (
            "invalid_packets", "duplicate_packets", "unknown_requests",
            "flow_mismatch_requests", "response_enqueue_failures", "response_send_failures",
            "receiver_internal_failures", "idle_termination",
        ):
            require_zero(server, key, policy, failures)
        for key in (
            "receiver_affinity_pass", "response_sender_affinity_pass",
            "server_main_affinity_pass", "runtime_invariants_pass",
        ):
            if server.get(key) != "1":
                failures.append(f"{policy}: {key} is not 1")
        if policy in ("M0_AltoThreshold", "M1_RescueSched") \
                and server.get("scheduler_affinity_pass") != "1":
            failures.append(f"{policy}: scheduler_affinity_pass is not 1")

        for key in (
            "cancelled_requests", "duplicate_execution_count", "duplicate_completion_count",
            "lost_descriptor_count", "nonzero_reservation_count", "affinity_failure_count",
        ):
            require_zero(runtime, key, f"{policy} runtime", failures)
        if runtime.get("classification") != "VALID_COMPLETE" or runtime.get("invariants_pass") != "1":
            failures.append(f"{policy}: runtime invariants/classification failed")
        total = to_int(runtime.get("total_requests"), f"{policy} total_requests", failures)
        completed = to_int(runtime.get("completed_requests"), f"{policy} completed_requests", failures)
        if completed != total:
            failures.append(f"{policy}: runtime request completion is incomplete")

        combined: dict[int, tuple[float, float, str, str]] = {}
        for client_index in (0, 1):
            client = clients.get(str(client_index), {})
            status = client.get("status")
            rows = client.get("requests")
            if not isinstance(status, dict) or not isinstance(rows, list):
                failures.append(f"{policy} client-{client_index}: sanitized data is incomplete")
                continue
            if status.get("status") != "PASS":
                failures.append(f"{policy} client-{client_index}: status is not PASS")
            for key in ("timeouts", "send_failures", "invalid_responses", "duplicate_responses"):
                require_zero(status, key, f"{policy} client-{client_index}", failures)
            for key in ("sender_affinity_pass", "receiver_affinity_pass"):
                if status.get(key) != "1":
                    failures.append(f"{policy} client-{client_index}: {key} is not 1")
            for row in rows:
                request_id = to_int(row.get("request_id"), f"{policy} request_id", failures)
                if request_id in combined:
                    failures.append(f"{policy}: duplicate client request ID {request_id}")
                    continue
                combined[request_id] = (
                    to_float(row.get("planned_send_us"), f"{policy} planned_send_us", failures),
                    to_float(row.get("actual_send_us"), f"{policy} actual_send_us", failures),
                    str(row.get("response_status", "")),
                    str(row.get("ingress_shard", "")),
                )

        expected = to_int(server.get("expected_requests"), f"{policy} expected_requests", failures)
        accepted = to_int(server.get("accepted_requests"), f"{policy} accepted_requests", failures)
        enqueued = to_int(server.get("responses_enqueued"), f"{policy} responses_enqueued", failures)
        sent = to_int(server.get("responses_sent"), f"{policy} responses_sent", failures)
        if not (len(combined) == expected == accepted == enqueued == sent == total == completed):
            failures.append(
                f"{policy}: request reconciliation mismatch clients={len(combined)} "
                f"expected={expected} accepted={accepted} enqueued={enqueued} sent={sent} "
                f"runtime_total={total} completed={completed}"
            )
        if any(row[2] != "received" for row in combined.values()):
            failures.append(f"{policy}: at least one client request is not received")
        if any(row[3] == "" for row in combined.values()):
            failures.append(f"{policy}: at least one ingress shard is missing")
        shard_maps[policy] = {request_id: row[3] for request_id, row in combined.items()}

        planned_values = [row[0] for row in combined.values()]
        actual_values = [row[1] for row in combined.values()]
        lags = [max(0.0, actual - planned) for planned, actual, _, _ in combined.values()]
        planned_span = max(planned_values) - min(planned_values) if planned_values else 0.0
        actual_span = max(actual_values) - min(actual_values) if actual_values else 0.0
        offered_error = abs(actual_span - planned_span) / planned_span if planned_span > 0 else math.inf
        lag_p99 = nearest_rank(lags, 0.99)
        if offered_error > thresholds["offered_load_relative_error_max"]:
            failures.append(f"{policy}: offered-load relative error {offered_error:.9g} exceeds limit")
        if lag_p99 > thresholds["send_lag_p99_us_max"]:
            failures.append(f"{policy}: send-lag P99 {lag_p99:.9g} us exceeds limit")

        if policy == "L1_WorkStealingPolling":
            scheduled = to_int(runtime.get("l1_poll_epochs_scheduled"), f"{policy} scheduled", failures)
            missed = to_int(runtime.get("l1_poll_epochs_missed"), f"{policy} missed", failures)
        elif policy in ("M0_AltoThreshold", "M1_RescueSched"):
            scheduled = to_int(runtime.get("scheduler_epochs_scheduled"), f"{policy} scheduled", failures)
            missed = to_int(runtime.get("scheduler_epochs_missed"), f"{policy} missed", failures)
        else:
            scheduled = 0
            missed = 0
        if policy in ("L1_WorkStealingPolling", "M0_AltoThreshold", "M1_RescueSched"):
            missed_fraction = missed / scheduled if scheduled > 0 else math.inf
            if missed_fraction > thresholds["scheduler_missed_fraction_max"]:
                failures.append(
                    f"{policy}: scheduler missed fraction {missed_fraction:.9g} exceeds limit"
                )
        else:
            missed_fraction = 0.0

        nic_drop_delta = 0
        udp_drop_delta = 0
        network = entry.get("network_counters", {})
        for node in ("node0", "node1"):
            snapshots = network.get(node, {})
            before = snapshots.get("before", {})
            after = snapshots.get("after", {})
            for when, snapshot in (("before", before), ("after", after)):
                capture_status = snapshot.get("capture_status")
                if not isinstance(capture_status, dict) or capture_status.get("status") != "PASS" \
                        or capture_status.get("normalization_exit_code") != "0":
                    failures.append(f"{policy} {node}-{when}: counter capture status is not PASS")
            before_counters = before.get("counters")
            after_counters = after.get("counters")
            if not isinstance(before_counters, dict) or not isinstance(after_counters, dict):
                failures.append(f"{policy} {node}: sanitized counters are incomplete")
                continue
            before_nic = before_counters["nic_drop_or_error"]
            after_nic = after_counters["nic_drop_or_error"]
            if set(before_nic) != set(after_nic):
                failures.append(f"{policy} {node}: NIC counter key set changed")
            for name in sorted(set(before_nic) | set(after_nic)):
                if name not in before_nic or name not in after_nic:
                    continue
                delta = after_nic[name] - before_nic[name]
                counter_rows.append({
                    "policy": policy, "node": node, "scope": "nic",
                    "counter": name, "delta": delta,
                })
                if delta < 0:
                    failures.append(f"{policy} {node}: NIC counter reset/wrap for {name}: {delta}")
                elif delta > 0:
                    nic_drop_delta += delta
            before_udp = before_counters["udp_drop_or_error"]
            after_udp = after_counters["udp_drop_or_error"]
            for name in UDP_DROP_FIELDS:
                delta = after_udp[name] - before_udp[name]
                counter_rows.append({
                    "policy": policy, "node": node, "scope": "udp",
                    "counter": name, "delta": delta,
                })
                if delta < 0:
                    failures.append(f"{policy} {node}: UDP counter reset/wrap for {name}: {delta}")
                elif delta > 0:
                    udp_drop_delta += delta
        if nic_drop_delta != 0:
            failures.append(f"{policy}: NIC drop/error delta is {nic_drop_delta}, expected 0")
        if udp_drop_delta != 0:
            failures.append(f"{policy}: UDP drop/error delta is {udp_drop_delta}, expected 0")

        metrics.append({
            "policy": policy,
            "expected_requests": expected,
            "request_coverage": len(combined),
            "offered_load_relative_error": offered_error,
            "send_lag_p99_us": lag_p99,
            "scheduler_epochs_scheduled": scheduled,
            "scheduler_epochs_missed": missed,
            "scheduler_missed_fraction": missed_fraction,
            "nic_drop_delta": nic_drop_delta,
            "udp_drop_delta": udp_drop_delta,
        })

    if len(shard_maps) == len(data["policies"]) and data["policies"]:
        baseline_policy = data["policies"][0]
        baseline = shard_maps[baseline_policy]
        for policy in data["policies"][1:]:
            if shard_maps[policy] != baseline:
                failures.append(f"{policy}: ingress mapping differs from {baseline_policy}")
    return failures, metrics, counter_rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Blindly validate one WP4 infrastructure RPC run")
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--policies", default=",".join(DEFAULT_POLICIES))
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--expected-commit")
    parser.add_argument("--offered-load-relative-error-max", type=float, default=0.02)
    parser.add_argument("--send-lag-p99-us-max", type=float, default=4.0)
    parser.add_argument("--scheduler-missed-fraction-max", type=float, default=0.05)
    args = parser.parse_args()

    root = args.run_dir.resolve()
    repo_root = args.repo_root.resolve()
    policies = tuple(item for item in args.policies.split(",") if item)
    if not policies:
        parser.error("--policies must not be empty")
    if len(set(policies)) != len(policies):
        parser.error("--policies contains duplicates")
    unknown = sorted(set(policies) - set(KNOWN_POLICIES))
    if unknown:
        parser.error(f"unknown policies: {unknown}")
    thresholds = {
        "offered_load_relative_error_max": args.offered_load_relative_error_max,
        "send_lag_p99_us_max": args.send_lag_p99_us_max,
        "scheduler_missed_fraction_max": args.scheduler_missed_fraction_max,
    }
    if any(not math.isfinite(value) or value < 0 for value in thresholds.values()):
        parser.error("all thresholds must be finite and non-negative")

    commit = repo_commit(repo_root)
    pre_failures: list[str] = []
    if args.expected_commit is not None and args.expected_commit != commit:
        pre_failures.append(
            f"validator commit mismatch expected={args.expected_commit} actual={commit}"
        )

    allowlist_path = root / "wp4_blind_allowlist.json"
    sanitized_path = root / "wp4_sanitized_blind_input.json"
    output_path = root / "wp4_blind_validator_output.json"
    write_json(allowlist_path, allowlist_artifact())
    sanitized = collect_sanitized(root, policies, commit, thresholds)
    sanitized["collection_failures"] = pre_failures + sanitized["collection_failures"]
    write_json(sanitized_path, sanitized)

    # Security boundary: the decision functions below receive only sanitized data.
    decision_input = json.loads(sanitized_path.read_text(encoding="ascii"))
    failures, metrics, counter_rows = validate_sanitized(decision_input)

    output = {
        "format": "rescuesched-wp4-blind-validator-output-v1",
        "validator_version": VALIDATOR_VERSION,
        "validator_commit": commit,
        "validator_script_sha256": sha256(Path(__file__).resolve()),
        "allowlist_sha256": sha256(allowlist_path),
        "sanitized_input_sha256": sha256(sanitized_path),
        "status": "PASS" if not failures else "FAIL",
        "classification": "INFRASTRUCTURE_GATE",
        "blind_to_deadline_tail_and_method_effect": True,
        "policies": list(policies),
        "thresholds": thresholds,
        "metrics": metrics,
        "counter_deltas": counter_rows,
        "failures": failures,
    }
    write_json(output_path, output)
    output_sha = sha256(output_path)

    metric_fields = (
        "policy", "expected_requests", "request_coverage", "offered_load_relative_error",
        "send_lag_p99_us", "scheduler_epochs_scheduled", "scheduler_epochs_missed",
        "scheduler_missed_fraction", "nic_drop_delta", "udp_drop_delta",
    )
    with (root / "wp4_infrastructure_metrics.csv").open("w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=metric_fields)
        writer.writeheader()
        writer.writerows(metrics)
    with (root / "wp4_counter_deltas.csv").open("w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=("policy", "node", "scope", "counter", "delta"))
        writer.writeheader()
        writer.writerows(counter_rows)

    hashes_path = root / "wp4_blind_artifact_hashes.json"
    write_json(hashes_path, {
        "allowlist_sha256": sha256(allowlist_path),
        "sanitized_input_sha256": sha256(sanitized_path),
        "validator_output_sha256": output_sha,
    })
    status_path = root / "WP4_INFRASTRUCTURE_STATUS.txt"
    with status_path.open("w", encoding="ascii") as handle:
        handle.write(f"status={output['status']}\n")
        handle.write("classification=INFRASTRUCTURE_GATE\n")
        handle.write("blind_to_deadline_tail_and_method_effect=1\n")
        handle.write(f"validator_version={VALIDATOR_VERSION}\n")
        handle.write(f"validator_commit={commit}\n")
        handle.write(f"allowlist_sha256={output['allowlist_sha256']}\n")
        handle.write(f"sanitized_input_sha256={output['sanitized_input_sha256']}\n")
        handle.write(f"validator_output_sha256={output_sha}\n")
        handle.write(f"policies={','.join(policies)}\n")
        for index, failure in enumerate(failures, 1):
            handle.write(f"failure_{index}={failure}\n")

    sums_path = root / "SHA256SUMS"
    files = sorted(path for path in root.rglob("*") if path.is_file() and path != sums_path)
    with sums_path.open("w", encoding="ascii") as handle:
        for path in files:
            handle.write(f"{sha256(path)}  {path.relative_to(root)}\n")

    print(status_path.read_text(encoding="ascii"), end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
