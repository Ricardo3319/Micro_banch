#!/usr/bin/env python3
"""Fail-closed validator for the final RescueSched local physical holdout artifact.

This script never executes a benchmark and never changes parameter selection.  It
cross-checks the final CSVs, per-run status/manifests, holdout pairing, correctness
invariants, fixed M1 tuple, retained warnings/failures, and final test logs.
"""
from __future__ import annotations

import csv
import json
import os
from pathlib import Path
import subprocess
import sys
from collections import Counter, defaultdict

ART = Path(__file__).resolve().parent
REPO = ART.parents[1]
EXPECTED_COMMIT = "9fdaf5679dfecfd98b573f4ab9b14085258b7bf3"
EXPECTED_BRANCH = "codex/infocom2027-integration"
HOLDOUT = {71, 83, 97, 109, 127}
DEVELOPMENT = {11, 23, 37, 47, 59}
FIXED_TUPLE = ("4", "6", "20", "0")
EXPECTED_VARIANTS = {
    "W2-rho070-w16": {"FIFO", "M0", "L1Only", "FullHybrid"},
    "W2-rho085-w16": {"FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"},
    "W3-rho070-w16": {"FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"},
    "W3-rho085-w8": {"FIFO", "M0", "L1Only", "FullHybrid"},
    "W3-rho085-w12": {"FIFO", "M0", "L1Only", "FullHybrid"},
    "W3-rho085-w16": {"FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"},
    "W3-rho090-w16": {"FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"},
}
RPC_SETUP = "RPC-W3-rho085-w8-scale10"
RPC_VARIANTS = {"FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"}

errors: list[str] = []
checks: list[str] = []


def check(condition: bool, message: str) -> None:
    if condition:
        checks.append(message)
    else:
        errors.append(message)


def read_csv(name: str) -> list[dict[str, str]]:
    path = ART / name
    check(path.is_file(), f"required CSV exists: {name}")
    if not path.is_file():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def read_kv(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        errors.append(f"required key/value file missing: {path}")
        return result
    for line in path.read_text(errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def run_git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=REPO, text=True).strip()


# Repository identity and non-commit requirements.
check(run_git("rev-parse", "HEAD") == EXPECTED_COMMIT, "HEAD matches recorded experiment commit")
check(run_git("branch", "--show-current") == EXPECTED_BRANCH, "current branch matches experiment branch")
check(bool(run_git("status", "--porcelain")), "repository remains dirty")
check(subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=REPO).returncode == 0,
      "no staged changes")
tracked_modified = set(run_git("diff", "--name-only").splitlines())
expected_modified = {
    "include/physical/runtime.h",
    "src/app/physical_runtime_main.cpp",
    "src/app/rpc_server_main.cpp",
    "src/physical/runtime.cpp",
    "tests/unit/physical_runtime_tests.cpp",
    "tests/unit/wp2_runtime_concurrency_tests.cpp",
}
check(expected_modified <= tracked_modified, "all six expected tracked runtime/test changes remain in the dirty diff")
check(HOLDOUT.isdisjoint(DEVELOPMENT), "holdout and development seed sets are disjoint")

metadata = read_kv(ART / "run_metadata.env")
check(metadata.get("commit") == EXPECTED_COMMIT, "run metadata commit matches")
check(metadata.get("dirty") == "1", "run metadata records dirty=1")
check(metadata.get("holdout_seeds") == "71,83,97,109,127", "run metadata records exact holdout seeds")
check(metadata.get("parameter_scan") == "NO", "run metadata records no parameter scan")
check(metadata.get("s3_cloudlab_host_kernel_tuning") == "NO", "run metadata excludes remote/tuning work")
check((metadata.get("m1_moves_per_check"), metadata.get("m1_epsilon_us"),
       metadata.get("m1_rescue_lookahead_us"), metadata.get("m1_rescue_idle_pulls_per_check"))
      == FIXED_TUPLE, "run metadata records fixed M1 tuple 4/6/20/0")

raw = read_csv("raw_metrics.csv")
check(len(raw) == 185, "raw_metrics.csv has exactly 185 data rows")
scopes = Counter(row.get("scope") for row in raw)
check(scopes == Counter({"local_synthetic_runtime": 160, "loopback_udp_rpc": 25}),
      "raw scope counts are 160 synthetic and 25 RPC")
check({int(row["seed"]) for row in raw} == HOLDOUT, "raw metrics use only the five holdout seeds")
check(all(row.get("commit") == EXPECTED_COMMIT and row.get("dirty") == "1" for row in raw),
      "every final run records the same dirty commit")
check(all(row.get("classification") == "VALID_COMPLETE" and row.get("valid") == "1" for row in raw),
      "all 185 final runs are VALID_COMPLETE")
check(all(not row.get("validity_errors") for row in raw), "no final run has a validity error")
check({(row.get("moves_per_check"), row.get("epsilon_us"), row.get("rescue_lookahead_us"),
            row.get("rescue_idle_pulls_per_check")) for row in raw} == {FIXED_TUPLE},
      "the only recorded parameter tuple is 4/6/20/0")
check(all(row.get("decision_records_dropped") == "0" and row.get("all_decisions_captured") == "1"
          for row in raw), "all decision records are captured with zero drops")
check(all(Path(row["run_path"]).is_dir() for row in raw), "all recorded run paths exist")

cells: dict[tuple[str, str, int], list[dict[str, str]]] = defaultdict(list)
for row in raw:
    cells[(row["scope"], row["setup"], int(row["seed"]))].append(row)
check(len(cells) == 40, "there are exactly 40 paired setup/seed cells")
for setup, variants in EXPECTED_VARIANTS.items():
    for seed in HOLDOUT:
        got = {r["variant"] for r in cells.get(("local_synthetic_runtime", setup, seed), [])}
        check(got == variants, f"synthetic paired variants complete: {setup} seed {seed}")
for seed in HOLDOUT:
    got = {r["variant"] for r in cells.get(("loopback_udp_rpc", RPC_SETUP, seed), [])}
    check(got == RPC_VARIANTS, f"RPC paired variants complete: seed {seed}")
for key, rows in cells.items():
    check(len({r["trace_embedded_sha256"] for r in rows}) == 1, f"embedded trace hash paired for {key}")
    check(len({r["trace_input_file_sha256"] for r in rows}) == 1, f"input trace hash paired for {key}")

trace_audit = read_csv("holdout_trace_audit.csv")
check(len(trace_audit) == 40, "holdout trace audit has 40 setup/seed rows")
check(all(row.get("same_trace_across_variants") == "1" and row.get("all_valid") == "1"
          and row.get("fixed_m1_tuples") == "4/6/20/0" for row in trace_audit),
      "holdout trace audit passes pairing, validity, and fixed-tuple checks")

invariants = read_csv("correctness_invariant_detail.csv")
check(len(invariants) == 185, "correctness detail has exactly 185 rows")
check(all(row.get("correctness_audit_pass") == "1" for row in invariants),
      "all 185 correctness audits pass")
zero_fields = [
    "cancelled_requests", "duplicate_execution_count", "duplicate_completion_count",
    "lost_descriptor_count", "nonzero_reservation_count", "affinity_failure_count",
    "decision_records_dropped",
]
check(all(all(row.get(field) == "0" for field in zero_fields) for row in invariants),
      "all runtime cancellation/duplicate/loss/reservation/affinity/drop counters are zero")
check(all(row.get("completed_requests") == row.get("total_requests") == "5500" for row in invariants),
      "all runtimes complete all 5500 requests")
check(all(row.get("measurement_requests") == "5000" for row in invariants),
      "all runs contain 5000 measured requests")

rpc_raw = [row for row in raw if row["scope"] == "loopback_udp_rpc"]
check(len(rpc_raw) == 25, "exactly 25 RPC rows are present")
check(all(row.get("validity_warnings") == "client_send_lag_p99_gt_4us" for row in rpc_raw),
      "all and only final RPC rows retain the send-lag warning")
check(all(float(row["client_send_lag_p99_us"]) > 4.0 for row in rpc_raw),
      "all 25 RPC send-lag P99 values exceed the retained 4-us warning threshold")
check(all((not row.get("scheduler_missed_fraction")) or float(row["scheduler_missed_fraction"]) < 0.05
          for row in rpc_raw), "no final RPC run violates the 5% scheduler-miss validity gate")

rpc_detail = [row for row in invariants if row["scope"] == "loopback_udp_rpc"]
rpc_zero_fields = [
    "invalid_packets", "duplicate_packets", "unknown_requests", "flow_mismatch_requests",
    "response_enqueue_failures", "response_send_failures", "receiver_internal_failures",
    "client_timeouts", "client_send_failures", "client_invalid_responses", "client_duplicate_responses",
]
rpc_one_fields = [
    "receiver_affinity_pass", "response_sender_affinity_pass", "server_main_affinity_pass",
    "client_sender_affinity_pass", "client_receiver_affinity_pass", "scheduler_affinity_ok",
]
check(all(row.get("expected_requests") == row.get("accepted_requests") ==
          row.get("responses_enqueued") == row.get("responses_sent") ==
          row.get("client_responses") == "5500" for row in rpc_detail),
      "every RPC run accepts, enqueues, sends, and receives all 5500 requests")
check(all(all(row.get(field) == "0" for field in rpc_zero_fields) for row in rpc_detail),
      "all RPC packet/queue/send/client failure counters are zero")
check(all(all(row.get(field) == "1" for field in rpc_one_fields) for row in rpc_detail),
      "all RPC client/server/scheduler affinity checks pass")

# Cross-check per-run status/manifests rather than trusting only derived CSVs.
for row in rpc_raw:
    server = Path(row["run_path"])
    root = server.parent
    client_summary_rows = list(csv.DictReader((root / "client" / "client_summary.csv").open(newline="")))
    check(len(client_summary_rows) == 1, f"single client summary row: {root}")
    if client_summary_rows:
        c = client_summary_rows[0]
        check(c.get("status") == "PASS" and c.get("responses") == "5500" and
              c.get("measurement_responses") == "5000" and c.get("timeouts") == "0" and
              c.get("send_failures") == "0" and c.get("invalid_responses") == "0" and
              c.get("duplicate_responses") == "0" and c.get("sender_affinity_pass") == "1" and
              c.get("receiver_affinity_pass") == "1", f"client RPC counters/affinity pass: {root}")
    server_status = read_kv(server / "RPC_SERVER_STATUS.txt")
    check(server_status.get("status") == "PASS" and server_status.get("classification") == "VALID_COMPLETE"
          and server_status.get("expected_requests") == "5500" and
          server_status.get("accepted_requests") == "5500" and
          server_status.get("responses_enqueued") == "5500" and
          server_status.get("responses_sent") == "5500" and
          all(server_status.get(f) == "0" for f in ["invalid_packets", "duplicate_packets",
              "unknown_requests", "flow_mismatch_requests", "response_enqueue_failures",
              "response_send_failures", "receiver_internal_failures"]) and
          all(server_status.get(f) == "1" for f in ["receiver_affinity_pass",
              "response_sender_affinity_pass", "scheduler_affinity_pass", "server_main_affinity_pass",
              "runtime_invariants_pass"]), f"server RPC counters/affinity/invariants pass: {root}")
    manifest = read_kv(server / "manifest.env")
    check(manifest.get("worker_count") == "8" and manifest.get("time_scale") == "1" and
          manifest.get("check_period_us") == "100" and
          (manifest.get("moves_per_check"), manifest.get("epsilon_us"),
           manifest.get("rescue_lookahead_us"), manifest.get("rescue_idle_pulls_per_check")) == FIXED_TUPLE,
          f"RPC manifest environment and fixed tuple pass: {root}")
    validity = read_kv(root / "EXPERIMENT_VALIDITY.txt")
    check(validity.get("correctness_status") == "PASS" and
          validity.get("timing_status") == "VALID_WITH_TIMING_WARNING" and
          validity.get("warning_count") == "1", f"RPC validity classification retained: {root}")

# Mechanism ablation identities.
check(all(row.get("rescue_distributed_l1_enabled") == ("0" if row["variant"] == "CentralOnly" else "1")
          for row in raw), "CentralOnly uniquely disables distributed L1")
check(all(int(row.get("l1_poll_attempts") or 0) == 0 and int(row.get("l1_poll_successes") or 0) == 0
          for row in raw if row["variant"] == "CentralOnly"),
      "CentralOnly has zero distributed-L1 poll attempts/successes")
check(all(int(row.get("central_commits") or 0) == 0 for row in raw if row["variant"] == "L1Only"),
      "L1Only has zero central commits")
check(all(int(row.get("central_commits") or 0) > 0 for row in raw if row["variant"] == "FullHybrid"),
      "FullHybrid exercises central rescue in every final run")

paired = read_csv("paired_metrics.csv")
aggregate = read_csv("aggregate.csv")
bootstrap = read_csv("bootstrap_95ci.csv")
check(len(paired) == 195, "paired_metrics.csv has 195 paired rows")
check(len(aggregate) == 285, "aggregate.csv has 285 aggregate rows")
check(len(bootstrap) == 285, "bootstrap_95ci.csv has 285 aggregate rows")
check(all(row.get("n_pairs") == "5" and row.get("bootstrap_draws") == "10000" and
          row.get("bootstrap_seed") == "2026072603" for row in aggregate),
      "every aggregate uses n=5, 10,000 bootstrap draws, and the fixed bootstrap seed")
check(all(row.get("n_pairs") == "5" and row.get("n5_minimum_exact_two_sided_p_note") ==
          "minimum_is_0.0625_when_all_5_pairs_align" for row in bootstrap),
      "bootstrap audit records the exact-test n=5 limitation")

manifest = json.loads((ART / "derived_analysis_manifest.json").read_text())
check(manifest.get("benchmark_execution") is False and manifest.get("parameter_selection_or_scan") is False,
      "derived analysis declares no benchmark execution or parameter selection")
check(manifest.get("scheduler_cpu_directly_measured") is False,
      "derived analysis explicitly records scheduler CPU as not directly measured")

# Explicitly retain the high-lag/high-tail observations rather than silently excluding them.
def find_run(seed: int, variant: str) -> dict[str, str]:
    matches = [r for r in rpc_raw if int(r["seed"]) == seed and r["variant"] == variant]
    return matches[0] if len(matches) == 1 else {}

seed97_full = find_run(97, "FullHybrid")
seed71_m0 = find_run(71, "M0")
check(bool(seed97_full) and float(seed97_full["server_p99_us"]) > 7000 and
      float(seed97_full["scheduler_max_epoch_lag_us"]) > 10000,
      "RPC seed 97 FullHybrid high-tail/high-lag observation is retained")
check(bool(seed71_m0) and float(seed71_m0["scheduler_max_epoch_lag_us"]) > 5000,
      "RPC seed 71 M0 scheduler-lag outlier is retained")

required_failures = [
    ART / "failed-attempt1-before-liveness-fix" / "ATTEMPT_STATUS.txt",
    ART / "failed-rpc-attempt1-checkperiod5" / "ATTEMPT_STATUS.txt",
    ART / "failed-analysis-attempt1-send-lag-classification" / "ATTEMPT_STATUS.txt",
    ART / "failure-diagnosis" / "m0-W3-rho070-w16-seed127-attempt3-postfix-release" / "STATUS.txt",
]
check(all(path.is_file() for path in required_failures), "all failed attempts and M0 post-fix regression are preserved")

required_docs = ["FINAL_REPORT.md", "VALIDATION_SUMMARY.md", "FAILURE_DIAGNOSIS.md", "README.md"]
check(all((ART / name).is_file() for name in required_docs), "all final documentation files exist")

release_log = ART / "validation-logs" / "final-release-build-and-full-ctest.log"
tsan_log = ART / "validation-logs" / "final-tsan-build-and-targeted-ctest.log"
release_text = release_log.read_text(errors="replace") if release_log.is_file() else ""
tsan_text = tsan_log.read_text(errors="replace") if tsan_log.is_file() else ""
check("100% tests passed, 0 tests failed out of 32" in release_text and
      "build_returncode=0" in release_text and "ctest_returncode=0" in release_text and
      "dirty=1" in release_text, "final Release build and 32-test CTest pass on dirty tree")
check("100% tests passed, 0 tests failed out of 2" in tsan_text and
      "build_returncode=0" in tsan_text and "ctest_returncode=0" in tsan_text and
      "dirty=1" in tsan_text, "final targeted TSan build and 2-test CTest pass on dirty tree")

print(f"artifact={ART}")
print(f"repo={REPO}")
print(f"checks_passed={len(checks)}")
print(f"errors={len(errors)}")
for message in checks:
    print(f"PASS {message}")
for message in errors:
    print(f"ERROR {message}")
print("status=PASS" if not errors else "status=FAIL")
sys.exit(0 if not errors else 1)
