#!/usr/bin/env python3
"""Synthetic corruption tests for the blind WP4 infrastructure validator."""

from __future__ import annotations

import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Callable

POLICIES = ("L1_WorkStealingPolling", "M1_RescueSched")
UDP_FIELDS = ("InErrors", "RcvbufErrors", "SndbufErrors", "InCsumErrors", "MemErrors")


def write_env(path: Path, values: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{key}={value}\n" for key, value in values.items()), encoding="ascii")


def write_csv(path: Path, fields: list[str], rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def build_fixture(root: Path, forbidden_marker: str = "baseline") -> None:
    for policy in POLICIES:
        policy_dir = root / policy
        write_env(policy_dir / "server/RPC_SERVER_STATUS.txt", {
            "status": "PASS",
            "classification": "VALID_COMPLETE",
            "expected_requests": 4,
            "accepted_requests": 4,
            "responses_enqueued": 4,
            "responses_sent": 4,
            "invalid_packets": 0,
            "duplicate_packets": 0,
            "unknown_requests": 0,
            "flow_mismatch_requests": 0,
            "response_enqueue_failures": 0,
            "response_send_failures": 0,
            "receiver_affinity_pass": 1,
            "response_sender_affinity_pass": 1,
            "scheduler_affinity_pass": 1,
            "server_main_affinity_pass": 1,
            "receiver_internal_failures": 0,
            "idle_termination": 0,
            "runtime_invariants_pass": 1,
            "deadline_misses_FORBIDDEN": forbidden_marker,
            "tail_latency_FORBIDDEN": forbidden_marker,
            "method_effect_FORBIDDEN": forbidden_marker,
        })
        runtime_fields = [
            "policy", "total_requests", "measurement_requests", "completed_requests",
            "cancelled_requests", "duplicate_execution_count", "duplicate_completion_count",
            "lost_descriptor_count", "nonzero_reservation_count", "affinity_failure_count",
            "scheduler_cpu_id", "scheduler_affinity_ok", "scheduler_epochs_scheduled",
            "scheduler_epochs_executed", "scheduler_epochs_missed",
            "l1_poll_epochs_scheduled", "l1_poll_epochs_missed", "classification",
            "invariants_pass", "deadline_violation_rate_FORBIDDEN",
            "P999_server_completion_us_FORBIDDEN", "goodput_rps_FORBIDDEN",
            "method_improvement_FORBIDDEN",
        ]
        is_l1 = policy == "L1_WorkStealingPolling"
        write_csv(policy_dir / "server/summary.csv", runtime_fields, [{
            "policy": policy,
            "total_requests": 4,
            "measurement_requests": 4,
            "completed_requests": 4,
            "cancelled_requests": 0,
            "duplicate_execution_count": 0,
            "duplicate_completion_count": 0,
            "lost_descriptor_count": 0,
            "nonzero_reservation_count": 0,
            "affinity_failure_count": 0,
            "scheduler_cpu_id": -1 if is_l1 else 20,
            "scheduler_affinity_ok": 1,
            "scheduler_epochs_scheduled": 0 if is_l1 else 100,
            "scheduler_epochs_executed": 0 if is_l1 else 99,
            "scheduler_epochs_missed": 0 if is_l1 else 1,
            "l1_poll_epochs_scheduled": 100 if is_l1 else 0,
            "l1_poll_epochs_missed": 1 if is_l1 else 0,
            "classification": "VALID_COMPLETE",
            "invariants_pass": 1,
            "deadline_violation_rate_FORBIDDEN": forbidden_marker,
            "P999_server_completion_us_FORBIDDEN": forbidden_marker,
            "goodput_rps_FORBIDDEN": forbidden_marker,
            "method_improvement_FORBIDDEN": forbidden_marker,
        }])
        client_rows = {
            0: [(1, 0.0, 0.5, 0), (3, 200.0, 200.5, 2)],
            1: [(2, 100.0, 100.5, 1), (4, 300.0, 300.5, 3)],
        }
        for client_index, rows in client_rows.items():
            client_dir = policy_dir / f"client-{client_index}"
            write_env(client_dir / "RPC_CLIENT_STATUS.txt", {
                "status": "PASS",
                "total_requests": 2,
                "responses": 2,
                "timeouts": 0,
                "send_failures": 0,
                "invalid_responses": 0,
                "duplicate_responses": 0,
                "sender_cpu": client_index * 2,
                "receiver_cpu": client_index * 2 + 1,
                "sender_affinity_pass": 1,
                "receiver_affinity_pass": 1,
                "send_lag_p99_us": 0.5,
                "deadline_result_FORBIDDEN": forbidden_marker,
            })
            request_fields = [
                "request_id", "planned_send_us", "actual_send_us", "response_status",
                "ingress_shard", "server_deadline_violation_FORBIDDEN",
                "client_rtt_p99_FORBIDDEN", "policy_effect_FORBIDDEN",
            ]
            write_csv(client_dir / "client_requests.csv", request_fields, [
                {
                    "request_id": request_id,
                    "planned_send_us": planned,
                    "actual_send_us": actual,
                    "response_status": "received",
                    "ingress_shard": shard,
                    "server_deadline_violation_FORBIDDEN": forbidden_marker,
                    "client_rtt_p99_FORBIDDEN": forbidden_marker,
                    "policy_effect_FORBIDDEN": forbidden_marker,
                }
                for request_id, planned, actual, shard in rows
            ])
        for node in ("node0", "node1"):
            for when in ("before", "after"):
                capture = policy_dir / "network-counters" / f"{node}-{when}"
                write_env(capture / "CAPTURE_STATUS.txt", {
                    "status": "PASS", "normalization_exit_code": 0,
                })
                counters = {
                    "format": "rescuesched-network-counters-v2",
                    "nic": {"rx_out_of_buffer": 10, "tx_queue_dropped": 0},
                    "udp": {name: 20 for name in UDP_FIELDS},
                }
                capture.mkdir(parents=True, exist_ok=True)
                (capture / "normalized-counters.json").write_text(
                    json.dumps(counters, indent=2, sort_keys=True) + "\n", encoding="ascii"
                )


def run_validator(repo: Path, run_dir: Path, expected_commit: str | None = None) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(repo / "scripts/validate_wp4_infrastructure_run.py"),
        str(run_dir),
        "--policies", ",".join(POLICIES),
        "--repo-root", str(repo),
    ]
    if expected_commit is not None:
        command += ["--expected-commit", expected_commit]
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def assert_status(run_dir: Path, expected: str) -> dict[str, object]:
    output = json.loads((run_dir / "wp4_blind_validator_output.json").read_text(encoding="ascii"))
    if output["status"] != expected:
        raise AssertionError(f"expected {expected}, got {output}\n")
    return output


def mutate_env(path: Path, key: str, value: str) -> None:
    lines = path.read_text(encoding="ascii").splitlines()
    replaced = False
    for index, line in enumerate(lines):
        if line.startswith(key + "="):
            lines[index] = f"{key}={value}"
            replaced = True
    if not replaced:
        raise AssertionError(f"missing key {key} in {path}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def mutate_counter(path: Path, scope: str, key: str, value: int | None) -> None:
    data = json.loads(path.read_text(encoding="ascii"))
    if value is None:
        del data[scope][key]
    else:
        data[scope][key] = value
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="ascii")


def failing_case(base: Path, name: str, repo: Path, mutation: Callable[[Path], None], reason: str) -> None:
    case = base / name
    build_fixture(case)
    mutation(case)
    completed = run_validator(repo, case)
    if completed.returncode == 0:
        raise AssertionError(f"{name} unexpectedly passed")
    output = assert_status(case, "FAIL")
    if not any(reason in item for item in output["failures"]):
        raise AssertionError(f"{name} did not report {reason!r}: {output['failures']!r}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_wp4_infrastructure_validator.py REPO_ROOT")
    repo = Path(sys.argv[1]).resolve()
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    physical_results = repo / "physical-results"
    physical_results.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wp4-blind-validator-", dir=physical_results) as text:
        base = Path(text)
        valid = base / "valid"
        build_fixture(valid)
        completed = run_validator(repo, valid, commit)
        if completed.returncode != 0:
            raise AssertionError(completed.stdout + completed.stderr)
        assert_status(valid, "PASS")

        forbidden_a = base / "forbidden-a"
        forbidden_b = base / "forbidden-b"
        build_fixture(forbidden_a, "AAA")
        build_fixture(forbidden_b, "completely-different-forbidden-value")
        for path in (forbidden_a, forbidden_b):
            result = run_validator(repo, path, commit)
            if result.returncode != 0:
                raise AssertionError(result.stdout + result.stderr)
        if (forbidden_a / "wp4_sanitized_blind_input.json").read_bytes() != \
                (forbidden_b / "wp4_sanitized_blind_input.json").read_bytes():
            raise AssertionError("forbidden deadline/tail/method fields changed sanitized input")
        if (forbidden_a / "wp4_blind_validator_output.json").read_bytes() != \
                (forbidden_b / "wp4_blind_validator_output.json").read_bytes():
            raise AssertionError("forbidden deadline/tail/method fields changed validator output")

        failing_case(base, "send-failure", repo, lambda root: mutate_env(
            root / POLICIES[0] / "client-0/RPC_CLIENT_STATUS.txt", "send_failures", "1"
        ), "send_failures=1")
        failing_case(base, "nic-delta", repo, lambda root: mutate_counter(
            root / POLICIES[0] / "network-counters/node0-after/normalized-counters.json",
            "nic", "rx_out_of_buffer", 11,
        ), "NIC drop/error delta is 1")
        failing_case(base, "udp-delta", repo, lambda root: mutate_counter(
            root / POLICIES[0] / "network-counters/node0-after/normalized-counters.json",
            "udp", "InErrors", 21,
        ), "UDP drop/error delta is 1")
        failing_case(base, "counter-reset", repo, lambda root: mutate_counter(
            root / POLICIES[0] / "network-counters/node0-after/normalized-counters.json",
            "nic", "rx_out_of_buffer", 9,
        ), "counter reset/wrap")
        failing_case(base, "missing-memerrors", repo, lambda root: mutate_counter(
            root / POLICIES[0] / "network-counters/node0-before/normalized-counters.json",
            "udp", "MemErrors", None,
        ), "required UDP counters missing")
        failing_case(base, "capture-fail", repo, lambda root: mutate_env(
            root / POLICIES[0] / "network-counters/node0-before/CAPTURE_STATUS.txt",
            "status", "FAIL",
        ), "counter capture status is not PASS")

        def remove_expected(root: Path) -> None:
            path = root / POLICIES[0] / "server/RPC_SERVER_STATUS.txt"
            lines = [line for line in path.read_text(encoding="ascii").splitlines()
                     if not line.startswith("expected_requests=")]
            path.write_text("\n".join(lines) + "\n", encoding="ascii")
        failing_case(base, "missing-allowed", repo, remove_expected, "missing allowlisted fields")

        mismatch = base / "commit-mismatch"
        build_fixture(mismatch)
        completed = run_validator(repo, mismatch, "0" * 40)
        if completed.returncode == 0:
            raise AssertionError("expected-commit mismatch unexpectedly passed")
        output = assert_status(mismatch, "FAIL")
        if not any("validator commit mismatch" in item for item in output["failures"]):
            raise AssertionError(f"commit mismatch was not recorded: {output['failures']!r}")

    print("WP4 blind infrastructure validator synthetic corruption tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
