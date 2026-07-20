#!/usr/bin/env python3
"""Regression tests for frozen non-formal WP4 port/profile/ring identity."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml

NODE0_SHA = "50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b"
NODE1_SHA = "fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_validator(script: Path, config: Path, node0: Path, node1: Path,
                  port: int, workers: int, output: Path) -> tuple[subprocess.CompletedProcess[str], dict]:
    completed = subprocess.run(
        [sys.executable, str(script), "--config", str(config),
         "--node0-profile", str(node0), "--node1-profile", str(node1),
         "--actual-port", str(port), "--workers", str(workers),
         "--output", str(output)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if not output.is_file():
        raise AssertionError(
            f"validator did not write output\nstdout={completed.stdout}\nstderr={completed.stderr}"
        )
    return completed, json.loads(output.read_text(encoding="ascii"))


def require_pass(result: tuple[subprocess.CompletedProcess[str], dict], label: str) -> None:
    completed, artifact = result
    if completed.returncode != 0 or artifact.get("status") != "PASS":
        raise AssertionError(
            f"{label} unexpectedly failed\nstdout={completed.stdout}\nstderr={completed.stderr}\n"
            f"artifact={artifact}"
        )


def require_fail(result: tuple[subprocess.CompletedProcess[str], dict], label: str,
                 expected_failure: str | None = None) -> None:
    completed, artifact = result
    if completed.returncode == 0 or artifact.get("status") != "FAIL":
        raise AssertionError(f"{label} unexpectedly passed: {artifact}")
    if expected_failure is not None and expected_failure not in artifact.get("failures", []):
        raise AssertionError(
            f"{label} did not report {expected_failure!r}: {artifact.get('failures')}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_wp4_nonformal_config.py REPO_ROOT")
    root = Path(sys.argv[1]).resolve()
    script = root / "scripts/validate_wp4_nonformal_config.py"
    config = root / "config/infocom2027-physical.yaml"
    node0 = root / "config/host-profiles/infocom2027-node0.env"
    node1 = root / "config/host-profiles/infocom2027-node1.env"
    original_hashes = (sha256(node0), sha256(node1))
    if original_hashes != (NODE0_SHA, NODE1_SHA):
        raise AssertionError(f"frozen profile drift before test: {original_hashes}")

    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="wp4-nonformal-config-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)
        require_pass(
            run_validator(script, config, node0, node1, 19040, 16, temp / "pass.json"),
            "frozen non-formal configuration",
        )
        require_fail(
            run_validator(script, config, node0, node1, 9000, 16, temp / "port-9000.json"),
            "formal port 9000",
            "actual_port_matches_nonformal",
        )
        require_fail(
            run_validator(script, config, node0, node1, 19041, 16, temp / "wrong-port.json"),
            "non-frozen non-formal port",
            "actual_port_matches_nonformal",
        )
        require_fail(
            run_validator(script, config, node0, node1, 19040, 12, temp / "workers-12.json"),
            "unauthorized 12-worker fallback",
            "actual_worker_count_matches_primary",
        )

        drifted_node0 = temp / "node0-drift.env"
        shutil.copyfile(node0, drifted_node0)
        drifted_node0.write_bytes(drifted_node0.read_bytes() + b"# synthetic drift\n")
        require_fail(
            run_validator(
                script, config, drifted_node0, node1, 19040, 16,
                temp / "profile-drift.json"
            ),
            "profile drift",
            "node0_profile_sha256",
        )

        ring_config = temp / "ring-4096.yaml"
        data = yaml.safe_load(config.read_text(encoding="utf-8"))
        data["nic_irq_profile"]["ring_entries_default"] = 4096
        data["nic_irq_profile"]["ring_4096_used"] = True
        ring_config.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        require_fail(
            run_validator(
                script, ring_config, node0, node1, 19040, 16,
                temp / "ring-4096.json"
            ),
            "ring 4096",
            "config_ring_4096_not_used",
        )

    final_hashes = (sha256(node0), sha256(node1))
    if final_hashes != original_hashes:
        raise AssertionError(f"test modified frozen profiles: {final_hashes}")
    print("WP4 non-formal config/profile/port/ring tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
