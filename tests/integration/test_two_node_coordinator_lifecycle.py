#!/usr/bin/env python3
"""Mocked coordinator launch-state and exact-PID cleanup regression tests."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

TRACE_BYTES = b"synthetic trace for coordinator lifecycle test\n"


def write_executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def run_scenario(root: Path, temp: Path, scenario: str) -> tuple[subprocess.CompletedProcess[str], Path, Path, Path]:
    fake_bin = temp / f"fake-bin-{scenario}"
    fake_build = temp / f"fake-build-{scenario}"
    fake_bin.mkdir()
    fake_build.mkdir()
    ssh_calls = temp / f"ssh-calls-{scenario}.log"
    scp_calls = temp / f"scp-calls-{scenario}.log"
    client_marker = temp / f"client-called-{scenario}"
    out_dir = temp / f"out-{scenario}"
    local_commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    trace_sha = hashlib.sha256(TRACE_BYTES).hexdigest()

    write_executable(
        fake_bin / "git",
        """#!/usr/bin/env bash
set -eu
case "$*" in
  *"rev-parse HEAD"*) printf '%s\n' "$FAKE_LOCAL_COMMIT" ;;
  *"status --porcelain"*) exit 0 ;;
  *) echo "unexpected git call: $*" >&2; exit 98 ;;
esac
""",
    )
    write_executable(
        fake_bin / "ssh",
        """#!/usr/bin/env bash
set -eu
printf '%s\n' "$*" >> "$FAKE_SSH_CALLS"
case "$*" in
  *"pwd -P"*)
    printf '%s\n' '/fake/server-repo'
    ;;
  *"git -C "*" rev-parse HEAD"*)
    printf '%s\n' "$FAKE_LOCAL_COMMIT"
    ;;
  *"git -C "*" status --porcelain"*)
    exit 0
    ;;
  *"test -x "*)
    exit 0
    ;;
  *"two-node-input"*"mkdir -p"*)
    exit 0
    ;;
  *"sha256sum -- "*)
    printf '%s\n' "$FAKE_TRACE_SHA"
    ;;
  *"two-node-active"*"mkdir -p"*)
    exit 0
    ;;
  *"bash scripts/run_background_job.sh"*)
    if [[ "$FAKE_SCENARIO" == ambiguous ]]; then
      exit 42
    fi
    exit 0
    ;;
  *"manifest_status=PASS"*"for i in"*|*"for i in"*"manifest_status=PASS"*)
    exit 1
    ;;
  *"if test -f "*"stop_background_job.sh"*)
    if [[ "$FAKE_SCENARIO" == pid-capture-failed ]]; then
      exit 0
    fi
    exit 1
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
printf '%s\n' "$*" >> "$FAKE_SCP_CALLS"
last="${!#}"
case " $* " in
  *" -r "*)
    mkdir -p -- "$last"
    printf '%s\n' 'synthetic remote partial audit' > "$last/MOCK_REMOTE_AUDIT.txt"
    ;;
esac
exit 0
""",
    )
    write_executable(
        fake_build / "rescuesched_trace_generator",
        """#!/usr/bin/env python3
import pathlib, sys
args = sys.argv[1:]
out = pathlib.Path(args[args.index('--out') + 1])
out.write_bytes(b'synthetic trace for coordinator lifecycle test\\n')
""",
    )
    write_executable(
        fake_build / "rescuesched_rpc_client",
        """#!/usr/bin/env bash
set -eu
: > "$FAKE_CLIENT_MARKER"
exit 95
""",
    )

    env = os.environ.copy()
    env["PATH"] = f"{fake_bin}:{env['PATH']}"
    env["FAKE_LOCAL_COMMIT"] = local_commit
    env["FAKE_TRACE_SHA"] = trace_sha
    env["FAKE_SSH_CALLS"] = str(ssh_calls)
    env["FAKE_SCP_CALLS"] = str(scp_calls)
    env["FAKE_CLIENT_MARKER"] = str(client_marker)
    env["FAKE_SCENARIO"] = scenario
    relative_build = fake_build.relative_to(root)
    relative_out = out_dir.relative_to(root)
    completed = subprocess.run(
        [
            "bash", str(root / "scripts/run_two_node_rpc_smoke.sh"),
            "--server-host", "fake-server", "--server-ip", "192.0.2.1",
            "--server-repo-dir", "/fake/server-repo",
            "--build-dir", str(relative_build), "--out-dir", str(relative_out),
            "--policies", "L1_WorkStealingPolling", "--workers", "16",
            "--requests", "4", "--warmup", "0", "--flow-sockets", "1",
            "--source-port-base", "24000", "--port", "19040",
            "--job-timeout-seconds", "1",
        ],
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=20,
    )
    return completed, ssh_calls, scp_calls, client_marker


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_two_node_coordinator_lifecycle.py REPO_ROOT")
    root = Path(sys.argv[1]).resolve()
    runner = root / "scripts/run_two_node_rpc_smoke.sh"
    source = runner.read_text(encoding="utf-8")

    forbidden = (r"\bpkill\b", r"\bkillall\b", r"rm\s+-rf", r"kill\s+.*\$\(")
    for pattern in forbidden:
        if re.search(pattern, source):
            raise AssertionError(f"coordinator contains forbidden broad cleanup: {pattern}")
    for marker in (
        "S3_GATE=WAIVED_BY_USER_FOR_NON_FORMAL_RUN",
        "S3_OPERATIONS=NOT_RUN",
        "EVIDENCE_DURABILITY=LOCAL_ONLY",
        "FORMAL_RESULT_ELIGIBILITY=NO",
    ):
        if marker not in source:
            raise AssertionError(f"missing auditable non-formal S3 waiver marker: {marker}")
    if source.index("capture_remote_counters \"$remote_run/network-counters/node0-after\"") \
            > source.index("collect_remote_audit remote-audit"):
        raise AssertionError("remote-audit copy occurs before remote after-counters")
    if source.index("stop_remote_server", source.index("for policy in")) \
            > source.index("collect_remote_audit remote-audit"):
        raise AssertionError("remote-audit copy occurs before exact-PID stop")

    physical_results = root / "physical-results"
    physical_results.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="coordinator-lifecycle-", dir=physical_results
    ) as temp_text:
        temp = Path(temp_text)
        ambiguous, ssh_log, scp_log, client_marker = run_scenario(root, temp, "ambiguous")
        if ambiguous.returncode != 125:
            raise AssertionError(
                "ambiguous remote launch identity did not escalate to unsafe exit 125\n"
                f"returncode={ambiguous.returncode}\nstdout={ambiguous.stdout}\n"
                f"stderr={ambiguous.stderr}\nssh_calls={ssh_log.read_text(encoding='utf-8')}"
            )
        if "Exact-PID cleanup failed; host state is unsafe" not in ambiguous.stderr:
            raise AssertionError("ambiguous launch did not report unsafe exact-PID cleanup")
        if client_marker.exists():
            raise AssertionError("clients started after ambiguous remote server launch")
        calls = ssh_log.read_text(encoding="utf-8")
        if "scripts/stop_background_job.sh" not in calls or "--name server" not in calls:
            raise AssertionError("ambiguous launch cleanup did not use exact remote stopper")
        if any(word in calls for word in ("pkill", "killall", "rm -rf")):
            raise AssertionError("mocked cleanup used a broad process/file operation")
        if "two-node-active/" not in calls or "L1_WorkStealingPolling" not in calls:
            raise AssertionError("remote active path was not unique/policy-scoped")
        if " -r " not in f" {scp_log.read_text(encoding='utf-8')} ":
            raise AssertionError("failure trap did not preserve remote partial audit")

        pid_failed, ssh_log, _, client_marker = run_scenario(
            root, temp, "pid-capture-failed"
        )
        if pid_failed.returncode == 0 or pid_failed.returncode == 125:
            raise AssertionError(
                "explicit PID_IDENTITY_CAPTURE_FAILED was not preserved as safe fail-closed\n"
                f"stdout={pid_failed.stdout}\nstderr={pid_failed.stderr}"
            )
        if "Exact-PID cleanup failed; host state is unsafe" in pid_failed.stderr:
            raise AssertionError("explicit PID identity capture failure was misclassified unsafe")
        if client_marker.exists():
            raise AssertionError("clients started without a PASS remote launch manifest")
        calls = ssh_log.read_text(encoding="utf-8")
        if "manifest_status=PASS" not in calls:
            raise AssertionError("coordinator did not require an atomic PASS launch manifest")
        if "reason=PID_IDENTITY_CAPTURE_FAILED" not in calls:
            raise AssertionError("cleanup did not restrict no-manifest acceptance to exact failure reason")

    print("two-node coordinator exact-PID lifecycle tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
