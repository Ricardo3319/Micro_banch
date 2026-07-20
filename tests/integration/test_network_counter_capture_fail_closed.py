#!/usr/bin/env python3
"""Fail-closed tests for required network-counter capture sources."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


def write_executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def status_value(path: Path, key: str) -> str | None:
    prefix = key + "="
    for line in path.read_text(encoding="ascii").splitlines():
        if line.startswith(prefix):
            return line[len(prefix):]
    return None


def run_capture(script: Path, temp: Path, name: str, ip_script: str,
                ethtool_script: str) -> tuple[subprocess.CompletedProcess[str], Path]:
    case = temp / name
    fake_bin = case / "fake-bin"
    out = case / "capture"
    fake_bin.mkdir(parents=True)
    write_executable(fake_bin / "ip", ip_script)
    write_executable(fake_bin / "ethtool", ethtool_script)
    env = os.environ.copy()
    env["PATH"] = f"{fake_bin}:{env['PATH']}"
    completed = subprocess.run(
        ["bash", str(script), "--interface", "lo", "--out-dir", str(out),
         "--label", name],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if not (out / "CAPTURE_STATUS.txt").is_file():
        raise AssertionError(
            f"{name}: CAPTURE_STATUS.txt missing\nstdout={completed.stdout}\n"
            f"stderr={completed.stderr}"
        )
    return completed, out


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_network_counter_capture_fail_closed.py REPO_ROOT")
    root = Path(sys.argv[1]).resolve()
    script = root / "scripts/capture_network_counters.sh"
    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)

    valid_ip = """#!/usr/bin/env bash
set -eu
printf '%s\n' '1: lo: <LOOPBACK,UP> mtu 65536 state UNKNOWN'
printf '%s\n' '    RX: bytes packets errors dropped missed mcast'
printf '%s\n' '    0 0 0 0 0 0'
printf '%s\n' '    TX: bytes packets errors dropped carrier collsns'
printf '%s\n' '    0 0 0 0 0 0'
"""
    valid_ethtool = """#!/usr/bin/env bash
set -eu
case "${1-}" in
  -S)
    printf '%s\n' 'NIC statistics:' '     tx_queue_dropped: 0' '     rx_discards_phy: 0'
    ;;
  -i)
    printf '%s\n' 'driver: synthetic-test' 'version: 1'
    ;;
  *) exit 9 ;;
esac
"""

    with tempfile.TemporaryDirectory(
        prefix="network-counter-fail-closed-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)

        failed_ip, failed_ip_out = run_capture(
            script,
            temp,
            "required-ip-failure",
            "#!/usr/bin/env bash\nexit 7\n",
            valid_ethtool,
        )
        if failed_ip.returncode == 0:
            raise AssertionError("required ip failure was accepted")
        if status_value(failed_ip_out / "CAPTURE_STATUS.txt", "status") != "FAIL":
            raise AssertionError("required ip failure did not produce status=FAIL")
        if status_value(failed_ip_out / "ip-link.metadata.env", "exit_code") != "7":
            raise AssertionError("required ip failure exit code was not preserved")

        malformed_ethtool = """#!/usr/bin/env bash
set -eu
case "${1-}" in
  -S) printf '%s\n' 'NIC statistics:' '     not-an-integer: unavailable' ;;
  -i) printf '%s\n' 'driver: synthetic-test' 'version: 1' ;;
  *) exit 9 ;;
esac
"""
        malformed, malformed_out = run_capture(
            script, temp, "malformed-ethtool", valid_ip, malformed_ethtool
        )
        if malformed.returncode == 0:
            raise AssertionError("unparseable ethtool statistics were accepted")
        if status_value(malformed_out / "CAPTURE_STATUS.txt", "status") != "FAIL":
            raise AssertionError("malformed ethtool did not produce status=FAIL")
        if status_value(
            malformed_out / "normalization.metadata.env", "exit_code"
        ) == "0":
            raise AssertionError("malformed ethtool normalization unexpectedly passed")

        valid, valid_out = run_capture(
            script, temp, "valid-synthetic", valid_ip, valid_ethtool
        )
        if valid.returncode != 0:
            raise AssertionError(
                f"valid capture failed\nstdout={valid.stdout}\nstderr={valid.stderr}"
            )
        if status_value(valid_out / "CAPTURE_STATUS.txt", "status") != "PASS":
            raise AssertionError("valid capture did not produce status=PASS")
        normalized = json.loads(
            (valid_out / "normalized-counters.json").read_text(encoding="ascii")
        )
        if normalized.get("nic", {}).get("tx_queue_dropped") != 0:
            raise AssertionError("allowlisted NIC counter was not normalized")
        required_udp = {"InErrors", "RcvbufErrors", "SndbufErrors", "InCsumErrors", "MemErrors"}
        if not required_udp.issubset(normalized.get("udp", {})):
            raise AssertionError("required UDP counters were not normalized")

    print("network counter required-source fail-closed tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
