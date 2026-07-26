# RescueSched INFOCOM Local Physical Evaluation Closure

Date: 2026-07-26
Branch: `codex/infocom2027-integration`
Experiment source commit: `9fdaf5679dfecfd98b573f4ab9b14085258b7bf3` with recorded dirty runtime/test changes
Full local artifact: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z`

## 1. Closure decision

The local physical evaluation is closed with a **qualified synthetic success and a negative loopback-RPC result**.

1. The holdout synthetic matrix supports claiming that FullHybrid improves misses, SLO goodput, P99, and P999 over FIFO on the tested W2/W3 traces, loads, and worker counts.
2. FullHybrid improves misses/goodput over M0 throughout the tested synthetic matrix, but W2 P999 remains unresolved/slightly regressive.
3. FullHybrid is not universally better than L1Only: it regresses W3-rho070 miss/goodput and frequently regresses P99/P999.
4. Strict mechanism ablation confirms that adding L1 to CentralOnly improves all four synthetic outcomes on the four ablation setups, but incurs high migration, repeat-migration, control-path, and scheduler-lag costs.
5. The single-host loopback UDP result is negative for L1Only and FullHybrid. The evidence points to pre-submit ingress delay on a shared runtime mutex, but mutex wait was not directly instrumented.
6. The evidence therefore closes the local physical stage without supporting universal tail, low-overhead, or deployment-general claims.

## 2. Fixed protocol and provenance

- Holdout seeds: `71,83,97,109,127`.
- Development seeds: `11,23,37,47,59`; the two sets are disjoint.
- No final parameter scan was performed.
- M1 was fixed in every final row at `moves=4`, `epsilon=6 us`, `lookahead=20 us`, `idle-pulls=0`.
- Final data: 160 synthetic runs plus 25 loopback RPC runs, 185/185 correctness-valid.
- The 40 setup/seed comparison cells use matching input and embedded trace hashes across variants.
- Scope was local synthetic runtime and single-host loopback UDP only. No S3, CloudLab, remote host, host tuning, or kernel tuning was used.

## 3. Requirement-to-evidence closure

| Requirement | Evidence | Result | Closure |
|---|---|---|---|
| Untuned holdout validation | Five disjoint holdout seeds over seven synthetic setups | FullHybrid vs FIFO improves all four outcomes in 35/35 pairs | Closed for the tested matrix only |
| Same-environment FIFO/M0/L1/M1 comparison | Hash-paired traces and identical runtime setup per cell | Complete 7-setup synthetic matrix and one RPC setup | Closed |
| L1Only/CentralOnly/FullHybrid ablation | Four strict CentralOnly synthetic setups plus RPC | FullHybrid beats CentralOnly synthetically; CentralOnly has worse tails than L1Only | Closed, with overhead tradeoff |
| Real end-to-end RPC | 25 real loopback UDP client/server runs | FullHybrid regresses miss, goodput, client P99, and client P999 | Closed as a negative result |
| Worker/load coverage | W2 rho 0.70/0.85 at 16 workers; W3 rho 0.70/0.85/0.90 at 16 workers; W3 rho 0.85 at 8/12/16 workers | FIFO improvement direction stable; L1 comparison heterogeneous | Closed within tested coverage |
| Paired uncertainty/significance/effect size | 10,000-draw paired bootstrap, exact sign-flip/sign tests, Cohen dz, paired rank-biserial | Complete aggregate tables produced | Closed; exact n=5 limitation retained |
| Tail decomposition | Class and rescue/migration group P99/P999 tables | Multiple class-level FullHybrid-vs-L1Only regressions identified | Closed diagnostically, not causally |
| Mechanism cost | Migration, repeats, central commits/success, scan counts, control duration, epoch miss, lag | High migration and overhead observed | Closed; no low-cost claim |
| Correctness and full tests | Per-run invariant audit, Release CTest, targeted TSan, fail-closed artifact validator | 185/185 valid, 32/32 Release, 2/2 TSan, 304/304 validator | Closed |

## 4. Synthetic result boundary

### FullHybrid versus FIFO

Across seven setups and five holdout seeds per setup:

- deadline violation: 35 wins / 0 losses;
- SLO goodput: 35/0;
- server P99: 35/0;
- server P999: 35/0;
- all setup-level paired-bootstrap 95% intervals exclude zero in the improvement direction.

This supports a workload- and host-qualified comparison against FIFO. It does not establish universal deployment generality.

### FullHybrid versus M0

- Miss/goodput improve in all seven setups.
- P99 improves at the bootstrap-CI level in all seven setups, with 34/35 seed-pair wins.
- W2 P999 is unresolved:
  - W2-rho070 mean regression 18.885 us; candidate-improvement CI `[-40.951, 5.546]`.
  - W2-rho085 mean regression 11.634 us; CI `[-55.235, 25.934]`.

### FullHybrid versus L1Only

- Miss/goodput improve in six of seven setups.
- W3-rho070 regresses by 0.472 percentage points of violation and 1,958.5 rps of goodput.
- P99 has 4 wins / 31 losses across seed pairs.
- P999 has 8 wins / 27 losses.
- Confirmed class-level regressions include W3-rho070 short P99, W2-rho085 long P99/P999, W3-rho085-w12 short P999, W3-rho085-w8 long P999, and W3-rho090 long P99/short P999.

The full hybrid therefore trades tail latency for miss reduction in several setups and cannot be presented as uniformly superior to L1Only.

## 5. Mechanism and overhead closure

Synthetic FullHybrid medians span:

- 3,346--4,035 migrations/run;
- 96--457 repeat migrations/run;
- 635--1,524 central commits/run;
- 0.867--1.000 central rescue success;
- 1.068--2.023 migrations per net avoided miss versus FIFO;
- 0.331--0.539 central commits per net avoided miss;
- approximately 5.0--11.6 us/request control-path wall duration;
- 2.786--11.251 scanned entries/request;
- approximately 21.7--79.3 us median maximum scheduler lag.

These values close the mechanism accounting but do not support low-migration or low-control-overhead claims. Scheduler CPU time was not directly measured.

## 6. Loopback RPC negative-result closure

For the real single-host loopback UDP setup, FullHybrid versus FIFO has paired mean candidate-improvement values of:

- deadline violation: `-8.480 pp`, 95% CI `[-10.288,-6.956]`;
- SLO goodput: `-2,208.1 rps`, CI `[-2,718.0,-1,818.4]`;
- client P99 RTT: `-4,350.6 us`, CI `[-6,074.1,-3,238.1]`;
- client P999 RTT: `-5,594.6 us`, CI `[-7,271.9,-4,594.9]`.

FullHybrid has median 2,513 migrations, 610 repeats, 0.203 central success, 38.93 us/request control-path duration, and 4,778.8 us scheduler maximum lag.

Per-request non-runtime residual P99 is approximately 85--88 us for FIFO/M0/CentralOnly, 3,037 us for L1Only, and 3,400 us for FullHybrid. Together with low response-queue occupancy and the shared-mutex code path, this supports the inference that L1 polling delays receiver submission before runtime timing begins. It is not a direct mutex-wait measurement.

The RPC outcome is retained as a first-class negative result and closes the current implementation path as unsuitable for a positive end-to-end claim without lock-path redesign and direct instrumentation.

## 7. Statistical interpretation

- Pairing unit: holdout seed.
- Bootstrap: 10,000 paired resamples with seed `2026072603`.
- Exact paired test: two-sided sign-flip; exact sign test is also recorded.
- Effect sizes: Cohen dz and paired rank-biserial.
- With five pairs, the minimum attainable two-sided exact p-value is 0.0625, even for a 5/5 sign pattern.
- A bootstrap interval excluding zero is treated as directional evidence within the five pairs, not as exact alpha=0.05 significance.
- Tail/class analyses are diagnostic and were not protected by a predeclared multiple-comparison family.
- P999 uses 5,000 measured requests/run and remains noisy.

## 8. Failure closure

All failed attempts remain documented:

1. M0 terminal scheduler handshake hang: fixed and followed by 100/100 hard-timeout Release regressions.
2. Initial 5-us physical RPC scheduler period: invalid because host wake/scheduling granularity caused 87.85% missed epochs; final RPC uses 100 us without changing the M1 tuple.
3. Initial analyzer send-lag classification: corrected from correctness failure to retained timing warning.
4. GCC 11/glibc TSan condition-wait false-positive path: translated through a TSan-visible wait path while preserving steady-clock accounting; final targeted TSan passes.

No failed run or high-tail seed was silently removed from the relevant final estimate.

## 9. Validation and publication scope

Final validation:

- Release CTest: 32/32 pass.
- Targeted TSan: 2/2 pass.
- Final artifact validator: 304/304 pass.
- Full local artifact checksum: 2,145/2,145 listed files pass.

The full local artifact contains 2,146 files and is approximately 581 MiB. Because the repository has no Git LFS configuration, the Git publication contains the source changes plus the compact reports, aggregate/raw run-level CSVs, plans, analysis scripts, provenance manifests, validation summaries, and selected validation logs. Large per-request decision traces and run directories remain in the full local artifact and are indexed by its manifest and checksum files; they are intentionally not inserted into normal Git history.

Primary report:

`artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FINAL_REPORT.md`

Primary machine-readable tables:

- `artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/raw_metrics.csv`
- `artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/paired_metrics.csv`
- `artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/aggregate.csv`
- `artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv`
- `artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/correctness_invariant_detail.csv`

## 10. Final claim set

Supported:

- Stable FullHybrid-over-FIFO synthetic improvements on the tested holdout matrix.
- Synthetic miss/goodput improvements over M0 with W2 P999 caveats.
- Complementary synthetic benefit of combining L1 and central rescue relative to CentralOnly.
- High, explicitly measured migration/control overhead.
- Negative loopback-RPC evidence for the current distributed-L1/full-hybrid implementation.

Not supported:

- Universal P99/P999 superiority.
- Universal superiority over L1Only.
- Low migration or scheduler overhead.
- Direct scheduler-CPU or mutex-wait claims.
- Generalization beyond the tested local host, traces, loads, and worker counts.
- Exact alpha=0.05 significance with five paired seeds.
