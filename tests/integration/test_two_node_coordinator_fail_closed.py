#!/usr/bin/env python3
"""Regression test: coordinator must stop before any RPC work on commit mismatch."""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


def write_executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_two_node_coordinator_fail_closed.py REPO_ROOT")
    root = Path(sys.argv[1]).resolve()
    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)
    local_commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()

    with tempfile.TemporaryDirectory(
        prefix="coordinator-fail-closed-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)
        fake_bin = temp / "fake-bin"
        fake_build = temp / "fake-build"
        fake_bin.mkdir()
        fake_build.mkdir()
        calls = temp / "ssh-calls.log"
        scp_marker = temp / "scp-called"
        out_dir = temp / "must-not-exist"

        write_executable(
            fake_bin / "git",
            """#!/usr/bin/env bash
set -eu
case "$*" in
  *"rev-parse HEAD"*)
    printf '%s\n' "$FAKE_LOCAL_COMMIT"
    ;;
  *"status --porcelain"*)
    exit 0
    ;;
  *)
    echo "unexpected git call: $*" >&2
    exit 98
    ;;
esac
""",
        )
        write_executable(
            fake_bin / "ssh",
            """#!/usr/bin/env bash
set -eu
printf '%s\\n' "$*" >> "$FAKE_SSH_CALLS"
case "$*" in
  *"pwd -P"*)
    printf '%s\\n' '/fake/server-repo'
    ;;
  *"rev-parse HEAD"*)
    printf '%064d\\n' 0
    ;;
  *)
    echo "unexpected ssh call: $*" >&2
    exit 97
    ;;
esac
""",
        )
        write_executable(
            fake_bin / "scp",
            """#!/usr/bin/env bash
set -eu
: > "$FAKE_SCP_MARKER"
exit 96
""",
        )
        for executable in ("rescuesched_trace_generator", "rescuesched_rpc_client"):
            write_executable(fake_build / executable, "#!/usr/bin/env bash\nexit 95\n")

        env = os.environ.copy()
        env["PATH"] = f"{fake_bin}:{env['PATH']}"
        env["FAKE_LOCAL_COMMIT"] = local_commit
        env["FAKE_SSH_CALLS"] = str(calls)
        env["FAKE_SCP_MARKER"] = str(scp_marker)
        relative_build = fake_build.relative_to(root)
        relative_out = out_dir.relative_to(root)
        completed = subprocess.run(
            [
                "bash",
                str(root / "scripts/run_two_node_rpc_smoke.sh"),
                "--server-host",
                "fake-server",
                "--server-ip",
                "192.0.2.1",
                "--server-repo-dir",
                "/fake/server-repo",
                "--build-dir",
                str(relative_build),
                "--out-dir",
                str(relative_out),
            ],
            cwd=root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        if completed.returncode == 0:
            raise AssertionError("coordinator accepted a mismatched server commit")
        if "Server commit mismatch:" not in completed.stderr:
            raise AssertionError(
                "coordinator did not report commit mismatch:\n" + completed.stderr
            )
        if out_dir.exists():
            raise AssertionError("coordinator created output before identity gate passed")
        if scp_marker.exists():
            raise AssertionError("coordinator invoked scp before identity gate passed")
        call_lines = calls.read_text(encoding="utf-8").splitlines()
        if len(call_lines) != 2:
            raise AssertionError(f"expected exactly two SSH calls, observed {call_lines!r}")

    print("two-node coordinator commit-mismatch fail-closed: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
