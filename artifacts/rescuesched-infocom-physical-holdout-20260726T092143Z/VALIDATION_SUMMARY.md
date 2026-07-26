# Final validation summary

**Artifact:** `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z`  
**Commit/state:** `9fdaf5679dfecfd98b573f4ab9b14085258b7bf3`, dirty, uncommitted, unstaged  
**Scope:** local synthetic runtime and single-host loopback UDP RPC only

## Completion matrix

| Requirement | Status | Evidence |
|---|---|---|
| Holdout seeds/traces not used for tuning | PASS within recorded protocol | Seeds 71/83/97/109/127, disjoint from development 11/23/37/47/59; `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/holdout_trace_audit.csv` |
| Same-environment FIFO/M0/L1/M1 | PASS | 7 synthetic setups; FIFO/M0/L1Only/FullHybrid all paired on each setup/seed; `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/synthetic_run_plan.json` |
| L1-only / central-only / full-hybrid ablation | PASS | 4 primary synthetic setups plus RPC; CentralOnly uniquely has distributed L1 disabled |
| Real RPC RTT, miss, SLO goodput | PASS_WITH_TIMING_WARNINGS | 25/25 complete loopback UDP runs; `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_variant_summary.csv` |
| Worker count, W2/W3, multi-load | PASS for tested matrix | W3 workers 8/12/16; W2/W3; rho 0.70/0.85/0.90 |
| Paired bootstrap, paired tests, effect sizes | PASS | 10,000 draws; exact sign-flip/sign test; dz; rank-biserial; `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv` |
| Rescued/non-rescued and request-class P99/P999 | PASS, descriptive | `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_group_summary.csv`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_regressions.csv` |
| Migration/rescue/control/scheduler overhead | PASS except direct scheduler CPU | Counts, repeats, commits, success, cost/miss, duration/cycles, scans, lag; scheduler CPU explicitly not directly sampled |
| Correctness invariants and full CTest | PASS | 185/185 correctness; Release 32/32; targeted TSan 2/2 |

## Final run counts

* Synthetic: 160/160 valid.
* Loopback RPC: 25/25 correctness-valid, all with retained client send-lag timing warning.
* Total: 185/185 correctness-valid.
* Setup/seed paired trace cells: 40/40 hash-identical across variants.
* Fixed tuple values observed: exactly `4/6/20/0`.
* Decision capture: 185/185 complete, 0 dropped records.

## Correctness invariants

Across all final runs:

* completed = total = 5,500; measured = 5,000;
* cancelled = 0;
* duplicate execution/completion = 0/0;
* lost descriptors = 0;
* nonzero reservations = 0;
* affinity failures = 0;
* all decision records captured.

Across every RPC run:

* accepted/enqueued/sent/client responses = 5,500 each;
* timeouts, send failures, invalid/duplicate responses = 0;
* invalid/duplicate packets, unknown requests, flow mismatches = 0;
* response enqueue/send failures and receiver internal failures = 0;
* client sender/receiver, server receiver/sender, scheduler, server-main affinity all pass.

Machine-readable audit: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/correctness_invariant_detail.csv`.

## Test logs

* Release complete CTest: **32/32 PASS**, return code 0.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-release-build-and-full-ctest.log`
* Targeted TSan: **2/2 PASS**, return code 0.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-tsan-build-and-targeted-ctest.log`
* Synthetic independent validation: **PASS**.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/synthetic-attempt2-independent-validation.log`
* RPC independent validation: **PASS_WITH_SEND_TIMING_WARNINGS**.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/rpc-attempt2-independent-validation.log`

## Non-fatal warnings and retained outliers

* 25/25 RPC runs: client send-lag P99 > 4 us. No run was removed for this warning.
* RPC seed 97 FullHybrid: server/client high-tail and 10,924.219-us scheduler-lag anomaly retained.
* RPC seed 71 M0: >5,393-us scheduler-lag/P999 anomaly retained.
* Failed 5-us RPC period, failed M0 liveness attempt, analyzer classification bug, and pre-fix TSan logs remain in the artifact.

## Validation command

```bash
python3 /users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validate_final_artifact.py
```

The final saved output is `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-artifact-validation.log`.

## Scope exclusions

No S3, CloudLab, remote host, host/kernel tuning, or new parameter scan was performed. No code was staged or committed. Scheduler CPU time and mutex wait were not directly measured.
