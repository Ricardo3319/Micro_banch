#!/usr/bin/env python3
"""Exact-PID launch/stop and PID-reuse rejection tests."""

from __future__ import annotations

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def proc_ticks(pid: int) -> int | None:
    try:
        return int(Path(f"/proc/{pid}/stat").read_text(encoding="ascii").split()[21])
    except (FileNotFoundError, IndexError, ValueError):
        return None


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="ascii").splitlines():
        if "=" in raw:
            key, value = raw.split("=", 1)
            values[key] = value
    return values


def wait_manifest(path: Path, exit_path: Path, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            values = read_env(path)
            if values.get("manifest_status") == "PASS":
                return
        if exit_path.is_file():
            break
        time.sleep(0.02)
    raise AssertionError(f"atomic PASS launch manifest was not published: {path}")


def identity_matches(pid: int, ticks: int) -> bool:
    return proc_ticks(pid) == ticks


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_background_job_exact_pid.py REPO_ROOT")
    root = Path(sys.argv[1]).resolve()
    launcher = root / "scripts/run_background_job.sh"
    stopper = root / "scripts/stop_background_job.sh"
    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(
        prefix="background-job-exact-pid-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)
        job_dir = temp / "normal"
        launched = subprocess.run(
            ["bash", str(launcher), "--job-dir", str(job_dir), "--name", "long-job",
             "--", "sleep", "300"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if launched.returncode != 0:
            raise AssertionError(
                f"launcher failed\nstdout={launched.stdout}\nstderr={launched.stderr}"
            )
        manifest = job_dir / "long-job.launch.env"
        wait_manifest(manifest, job_dir / "long-job.exit")
        values = read_env(manifest)
        if values.get("format") != "rescuesched-background-job-v2":
            raise AssertionError("unexpected launch manifest format")
        wrapper_pid = int(values["wrapper_pid"])
        wrapper_ticks = int(values["wrapper_start_ticks"])
        child_pid = int(values["child_pid"])
        child_ticks = int(values["child_start_ticks"])
        if not identity_matches(wrapper_pid, wrapper_ticks):
            raise AssertionError("wrapper identity did not match published manifest")
        if not identity_matches(child_pid, child_ticks):
            raise AssertionError("child identity did not match published manifest")
        if list(job_dir.glob("*.tmp.*")):
            raise AssertionError("temporary PID/manifest files remained after atomic publish")

        stopped = subprocess.run(
            ["bash", str(stopper), "--job-dir", str(job_dir), "--name", "long-job",
             "--wait-seconds", "2"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if stopped.returncode != 0:
            raise AssertionError(
                f"exact stopper failed\nstdout={stopped.stdout}\nstderr={stopped.stderr}"
            )
        if identity_matches(child_pid, child_ticks) or identity_matches(wrapper_pid, wrapper_ticks):
            raise AssertionError("exact-PID stopper left a matching process identity alive")
        stop_files = sorted(job_dir.glob("long-job.stop-*.env"))
        if len(stop_files) != 1 or read_env(stop_files[0]).get("status") != "PASS":
            raise AssertionError("exact stopper did not preserve one PASS stop artifact")

        sentinel = subprocess.Popen(["sleep", "300"])
        try:
            deadline = time.monotonic() + 2.0
            sentinel_ticks = None
            while time.monotonic() < deadline and sentinel_ticks is None:
                sentinel_ticks = proc_ticks(sentinel.pid)
                time.sleep(0.01)
            if sentinel_ticks is None:
                raise AssertionError("could not capture sentinel identity")
            forged_dir = temp / "forged"
            forged_dir.mkdir()
            host = socket.getfqdn() or socket.gethostname()
            forged = forged_dir / "forged.launch.env"
            forged.write_text(
                "format=rescuesched-background-job-v2\n"
                "manifest_status=PASS\n"
                "name=forged\n"
                f"hostname={host}\n"
                "started_at_utc=2026-07-20T00:00:00Z\n"
                f"wrapper_pid={sentinel.pid}\n"
                f"wrapper_start_ticks={sentinel_ticks + 1}\n"
                f"child_pid={sentinel.pid}\n"
                f"child_start_ticks={sentinel_ticks + 1}\n",
                encoding="ascii",
            )
            rejected = subprocess.run(
                ["bash", str(stopper), "--job-dir", str(forged_dir), "--name", "forged",
                 "--wait-seconds", "1"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if rejected.returncode == 0:
                raise AssertionError("stopper accepted forged PID start ticks")
            if sentinel.poll() is not None:
                raise AssertionError("stopper signaled a PID whose start ticks did not match")
            forged_stops = sorted(forged_dir.glob("forged.stop-*.env"))
            if len(forged_stops) != 1:
                raise AssertionError("PID mismatch did not produce one stop evidence artifact")
            forged_values = read_env(forged_stops[0])
            if forged_values.get("status") != "FAIL" \
                    or forged_values.get("child_action") != "IDENTITY_REJECTED" \
                    or forged_values.get("wrapper_action") != "IDENTITY_REJECTED":
                raise AssertionError("PID mismatch evidence was not fail-closed")
        finally:
            if sentinel.poll() is None:
                sentinel.terminate()
                try:
                    sentinel.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    sentinel.kill()
                    sentinel.wait(timeout=2)

    print("background job exact-PID lifecycle tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
