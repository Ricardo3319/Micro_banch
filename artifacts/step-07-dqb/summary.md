# Step-07 DQB-PM Optimization Summary

## Scope

- Goal: optimize the new `M2_DQB_PM` path against the current `B2/M0/M1`
  baselines.
- Build: `build-aqb-check`
- Commands:
  - `cmake --build build-aqb-check`
  - `.\build-aqb-check\simulator.exe aqb-smoke`
  - `.\build-aqb-check\simulator.exe aqb-eval`
- Output CSV: `artifacts/step-06-aqb/aqb_eval.csv`

## Algorithm Changes

`M2_DQB_PM` now uses a conservative distribution-aware queue-batch path:

- builds a fixed-size `QueueSummary` from each core queue;
- generates at most one `QueueBatchCandidate` per core per check epoch;
- detects `ShortBehindLong`, `MiceBehindElephant`, `SlowNodeBatchPressure`,
  and `GenericPressure` batch types;
- selects one destination per batch, not per task;
- accounts for global `incoming_reservation`;
- commits the selected FIFO/work prefix as per-request forwarding events.

During tuning, aggressive batch widening increased target-side harm and made
P99 worse. The retained configuration is intentionally conservative: it keeps
batch-level control and summary-based risk detection, but allows the move
prefix to shrink when the target cannot safely absorb a wider batch.

## Representative Results

All medians use the frozen five seeds `{11,23,37,47,59}` with warmup `200k`
and measurement `1M`.

| Scenario | Method | P99 median | P999 median | SLO violation median | migration_rate median | invalid_ratio median |
|---|---:|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | B2 | 1610 | 2270 | 0.468508 | 0.0123568 | 0.221303 |
| W2 burst rho=0.85 | M0 | 1420 | 2010 | 0.386676 | 0.0400124 | 0.145056 |
| W2 burst rho=0.85 | M1-AQB | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| W2 burst rho=0.85 | M2-DQB | 430 | 884 | 0.231632 | 0.0156658 | 0.0380709 |
| W3 heavy-tail rho=0.85 | B2 | 200 | 394 | 0.096214 | 0.0221686 | 0.297758 |
| W3 heavy-tail rho=0.85 | M0 | 186 | 360 | 0.088628 | 0.0402885 | 0.0508681 |
| W3 heavy-tail rho=0.85 | M1-AQB | 176 | 338 | 0.084086 | 0.0451337 | 0.035445 |
| W3 heavy-tail rho=0.85 | M2-DQB | 192 | 386 | 0.089972 | 0.0135295 | 0.00647843 |
| W1 saturation rho=0.95 | B2 | 1380 | 1600 | 0.996324 | 0.000411213 | 0.402612 |
| W1 saturation rho=0.95 | M0 | 1500 | 2700 | 0.991473 | 0.0394141 | 0.379253 |
| W1 saturation rho=0.95 | M1-AQB | 1370 | 1580 | 0.996731 | 0.0175483 | 0.249164 |
| W1 saturation rho=0.95 | M2-DQB | 1350 | 1590 | 0.995763 | 0 | 0 |

## DQB Diagnostics

| Scenario | candidate median | selected batch median | moved request median | avg move size | saturation guard median |
|---|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | 13,644,619 | 19,357 | 19,357 | 1.00 | 0 |
| W3 heavy-tail rho=0.85 | 2,307,288 | 16,360 | 16,363 | 1.00 | 0 |
| W1 saturation rho=0.95 | 25,918,946 | 0 | 0 | 0 | 1,559,211 |

## Interpretation

1. **W2 burst is the strongest positive case.** DQB cuts median P99 from
   M1's `1100us` to `430us` and from B2's `1610us` to `430us`, while using
   far less migration budget than M1. This supports the distribution-aware
   risk detector: many candidates are inspected as queue summaries, but only
   a small number of high-confidence batches are committed.

2. **W1 saturation behavior is improved.** DQB detects saturation and performs
   no migrations in the representative saturated W1 scenario. This avoids the
   M0 failure mode where proactive migration spends budget even when no target
   can help.

3. **W3 heavy-tail exposes a conservative tradeoff.** DQB is safer than B2/M0/M1
   in side effects, but M1 still has better median P99/P999. The current DQB
   target-completion guard is conservative and tends to reject wider heavy-tail
   batches.

4. **Move batch size remains the main unresolved issue.** The control path is
   batch-level, but the committed move prefix often shrinks to one request.
   Experiments with wider move batches increased target-side harm and degraded
   P99, so the current safe point favors selective queue-batch repair over
   large migration batches.

## Failed/Deferred Run

`aqb-hetero` was started but exceeded the 30-minute limit and produced a partial
CSV. The partial file was renamed to
`artifacts/step-06-aqb/aqb_heterogeneous.partial-timeout.csv` and should not be
used as a formal result.

## Next Optimization Target

The next code step should add destination-side batch placement rather than only
host-level forwarding:

- group tasks by `(src,dst,batch_id)`;
- assign the batch across destination cores using batch-aware placement;
- estimate `remote_tail` using destination-core slots rather than host average;
- keep `incoming_reservation` by total work;
- report batch-size distribution and batch type distribution.

That is the likely path to make the committed move batch larger without
reintroducing target-side harm.
