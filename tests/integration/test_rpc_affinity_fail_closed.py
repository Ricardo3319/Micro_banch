#!/usr/bin/env python3
"""RPC client/server affinity failures must stop before readiness or traffic."""

from __future__ import annotations

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


INVALID_CPU = 1_000_000


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def read_status(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if "=" in raw:
            key, value = raw.split("=", 1)
            values[key] = value
    return values


def run_checked(command: list[str], label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=20,
    )
    if completed.returncode == 0:
        raise AssertionError(
            f"{label} unexpectedly passed\nstdout={completed.stdout}\nstderr={completed.stderr}"
        )
    return completed


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: test_rpc_affinity_fail_closed.py REPO_ROOT TRACE_GENERATOR RPC_CLIENT RPC_SERVER"
        )
    root = Path(sys.argv[1]).resolve()
    generator = Path(sys.argv[2]).resolve()
    client = Path(sys.argv[3]).resolve()
    server = Path(sys.argv[4]).resolve()
    for path in (generator, client, server):
        if not path.is_file() or not os.access(path, os.X_OK):
            raise AssertionError(f"required executable is unavailable: {path}")

    allowed = sorted(os.sched_getaffinity(0))
    if len(allowed) < 7:
        raise AssertionError(f"at least seven process-allowed CPUs are required, got {allowed}")
    worker, receiver0, receiver1, sender0, sender1, scheduler, main_cpu = allowed[:7]

    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="rpc-affinity-fail-closed-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)
        trace = temp / "trace.csv"
        generated = subprocess.run(
            [str(generator), "--out", str(trace), "--workload", "W3", "--rho", "0.85",
             "--seed", "11", "--warmup", "0", "--requests", "4", "--workers", "1",
             "--flow-count", "2"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=20,
        )
        if generated.returncode != 0:
            raise AssertionError(
                f"trace generation failed\nstdout={generated.stdout}\nstderr={generated.stderr}"
            )

        for dimension in ("sender", "receiver"):
            monitor = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            monitor.bind(("127.0.0.1", 0))
            monitor.settimeout(0.2)
            server_port = int(monitor.getsockname()[1])
            source_port = free_udp_port()
            out = temp / f"client-invalid-{dimension}"
            command = [
                str(client), "--trace", str(trace), "--server", "127.0.0.1",
                "--out-dir", str(out), "--port", str(server_port), "--workers", "1",
                "--flow-sockets", "1", "--source-port-base", str(source_port),
                "--bind", "127.0.0.1", "--warmup-requests", "0",
                "--response-timeout-seconds", "1", "--sender-cpu",
                str(INVALID_CPU if dimension == "sender" else worker),
                "--receiver-cpu", str(INVALID_CPU if dimension == "receiver" else receiver0),
            ]
            completed = run_checked(command, f"client invalid {dimension} affinity")
            status_path = out / "RPC_CLIENT_STATUS.txt"
            if not status_path.is_file():
                raise AssertionError(f"client invalid {dimension} did not write status")
            status = read_status(status_path)
            if status.get("status") != "FAIL" \
                    or status.get("classification") != "INFRASTRUCTURE_FAILURE":
                raise AssertionError(f"client invalid {dimension} status is not fail-closed: {status}")
            try:
                packet = monitor.recvfrom(65535)
            except socket.timeout:
                packet = None
            finally:
                monitor.close()
            if packet is not None:
                raise AssertionError(
                    f"client invalid {dimension} transmitted a packet before affinity gate"
                )
            if "RPC client PASS" in completed.stdout:
                raise AssertionError("client printed PASS after affinity failure")

        base = [
            str(server), "--trace", str(trace), "--bind", "127.0.0.1",
            "--policy", "M1_RescueSched", "--workers", "1", "--cpus", str(worker),
            "--warmup-requests", "0", "--idle-timeout-seconds", "1",
        ]
        cases = {
            "server-main": {
                "receiver": f"{receiver0},{receiver1}",
                "sender": f"{sender0},{sender1}",
                "scheduler": scheduler,
                "main": INVALID_CPU,
            },
            "receiver": {
                "receiver": f"{INVALID_CPU},{receiver1}",
                "sender": f"{sender0},{sender1}",
                "scheduler": scheduler,
                "main": main_cpu,
            },
            "response-sender": {
                "receiver": f"{receiver0},{receiver1}",
                "sender": f"{INVALID_CPU},{sender1}",
                "scheduler": scheduler,
                "main": main_cpu,
            },
            "scheduler": {
                "receiver": f"{receiver0},{receiver1}",
                "sender": f"{sender0},{sender1}",
                "scheduler": INVALID_CPU,
                "main": main_cpu,
            },
        }
        for label, values in cases.items():
            out = temp / f"server-invalid-{label}"
            command = base + [
                "--out-dir", str(out), "--port", str(free_udp_port()),
                "--receiver-cpus", str(values["receiver"]),
                "--response-sender-cpus", str(values["sender"]),
                "--scheduler-cpu", str(values["scheduler"]),
                "--server-main-cpu", str(values["main"]),
            ]
            completed = run_checked(command, f"server invalid {label} affinity")
            combined = completed.stdout + completed.stderr
            if "RPC_SERVER_READY" in combined:
                raise AssertionError(f"server printed readiness after invalid {label} affinity")
            status_path = out / "RPC_SERVER_STATUS.txt"
            if not status_path.is_file():
                raise AssertionError(f"server invalid {label} did not write structured status")
            status = read_status(status_path)
            if status.get("status") != "FAIL" \
                    or status.get("classification") != "INFRASTRUCTURE_FAILURE":
                raise AssertionError(f"server invalid {label} status is not fail-closed: {status}")

    print("RPC client/server affinity fail-closed tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
