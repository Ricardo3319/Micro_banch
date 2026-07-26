# Failure diagnosis and retained abnormal results

This document records every failed/invalid attempt relevant to the final local physical holdout artifact. None of these results was silently deleted, cherry-picked, or mixed into the final paired estimates.

## 1. Synthetic attempt 1: M0 terminal liveness hang

**Observed:** the first 160-run matrix completed 24 valid runs, then W3-rho070-w16 seed 127 M0 ran for more than ten minutes. Worker threads were mostly blocked while one scheduler thread consumed approximately one CPU. The partial attempt is preserved at:

`/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/failed-attempt1-before-liveness-fix`

**Diagnosis:** shutdown/terminal completion did not provide a robust terminal handshake to the scheduler wait path. A timing-sensitive scheduler could continue after all requests were terminal while the main/worker path waited for orderly termination.

**Fix:** terminal completion is now published through `terminal_run_complete` with release/acquire semantics and wakes the scheduler wait condition. Terminal accounting is centralized so completion and submission-done transitions update the handshake consistently.

**Verification:**

* GDB timing did not reproduce the original hang; this negative reproduction is preserved and was not treated as proof of correctness.
* The post-fix exact configuration passed 100/100 Release iterations with a 5-second hard timeout, all `VALID_COMPLETE` with invariants passing:  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/failure-diagnosis/m0-W3-rho070-w16-seed127-attempt3-postfix-release/STATUS.txt`
* A diagnostic harness initially looked for `summary_metrics.csv` instead of `summary.csv`; that harness failure is separately recorded and the full 100-iteration sequence was restarted.
* The complete 160-run final matrix was restarted from scratch; no attempt-1 metrics entered the final CSVs.

## 2. RPC attempt 1: 5-us physical scheduler period invalid

Preserved at:

`/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/failed-rpc-attempt1-checkperiod5`

The first CentralOnly run was correctness-valid but failed the predeclared scheduler timing gate:

* scheduled epochs: 122,046;
* executed epochs: 14,823;
* missed fraction: 0.8785458;
* decision P50: 0.712 us;
* observed execution cadence: roughly one epoch per 41 us over a 611-ms run;
* client send-lag P99: 6.115 us, above the 4-us warning gate.

The root cause is host wake/scheduling granularity at a real 5-us period, not central decision compute. Network ingress requires `time_scale=1`, so the synthetic logical 5-us period cannot be used as a literal loopback physical wake period on this host.

**Corrective protocol:** restart all RPC pairs with `check_period_us=100`, keep M1 fixed at 4/6/20/0, preserve the failed attempt, and retain send-lag exceedance as a warning rather than a correctness exclusion. No host/kernel tuning was performed.

## 3. Analyzer attempt 1: send-lag warning misclassified as fatal

Preserved at:

`/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/failed-analysis-attempt1-send-lag-classification`

The first analyzer treated `client_send_lag_p99_us > 4` as a correctness failure despite the attempt-2 protocol designating it as a retained timing warning. This rejected all 25 RPC runs even though they had:

* complete request/response delivery;
* zero timeouts/network errors/duplicates;
* valid affinity;
* `VALID_COMPLETE` runtime status;
* all runtime invariants passing.

**Fix:** keep correctness and scheduler-miss gates fail-closed, require the recorded 100-us RPC period, classify send lag as `validity_warnings`, and reanalyze all 185 runs. The original analyzer outputs remain preserved.

## 4. TSan false positive and wait-path adjustment

The initial targeted TSan logs reported a double lock through GCC 11/glibc's `pthread_cond_clockwait` path when multiple condition variables shared a mutex:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/tsan-targeted-ctest.log`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/tsan-targeted-ctest-after-lifetime-fix.log`

The runtime keeps steady-clock epoch accounting but translates each blocking deadline to a realtime `condition_variable::wait_until`, which GCC 11 TSan models correctly. A callback-lifetime issue in the concurrency test was also fixed so captured synchronization objects outlive the runtime callback owner.

Final targeted TSan passes 2/2:

`/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-tsan-build-and-targeted-ctest.log`

This is described as a tool-model compatibility adjustment, not evidence that TSan can prove absence of all races.

## 5. Retained final anomalies and regressions

### RPC seed 97 FullHybrid

* server P99 approximately 7,592.4 us;
* client P99 approximately 8,002.1 us;
* scheduler max lag 10,924.219 us.

### RPC seed 71 M0

* scheduler max lag above 5,393 us;
* accompanying P999 abnormality.

Both remain in raw metrics, means, CIs, exact tests, and effect sizes.

### Synthetic regressions

* FullHybrid versus L1Only regresses W3-rho070 miss rate, goodput, and P99.
* Multiple request-class P99/P999 rows regress versus L1Only.
* W2 P999 versus M0 is unresolved and slightly regressive in mean.
* CentralOnly has substantial tail regressions versus L1Only on all four primary ablation setups.

These are enumerated in `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FINAL_REPORT.md` and `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_regressions.csv`.

## 6. Exclusion policy

Only runs from protocols declared invalid before analysis or harness outputs that did not constitute a completed run are excluded. The reasons and raw files are preserved. Within the final attempt-2 matrices, all 185 runs—including warnings and outliers—are retained.
