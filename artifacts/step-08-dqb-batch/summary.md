# Step-09 DQB Host-Aggregation Summary

## Scope

- Goal: extend the new large-batch `M2_DQB_PM` so that `W3 heavy-tail` is not
  limited by the "one core queue segment" batch definition.
- Build: `build-aqb-check`
- Commands:
  - `cmake --build build-aqb-check`
  - `.\build-aqb-check\simulator.exe dqb-w2-only`
  - `.\build-aqb-check\simulator.exe dqb-w3-only`
  - `.\build-aqb-check\simulator.exe dqb-w1-only`
  - `.\build-aqb-check\simulator.exe dqb-eval`
- Output:
  - `artifacts/step-08-dqb-batch/dqb_eval.csv`
  - `artifacts/step-08-dqb-batch/dqb_w2_only.csv`
  - `artifacts/step-08-dqb-batch/dqb_w3_only.csv`
  - `artifacts/step-08-dqb-batch/dqb_w1_only.csv`
  - `artifacts/step-08-dqb-batch/metrics_summary.csv`

## What Was Implemented

The new DQB revision keeps the successful W2 large contiguous segment path, and
adds a **host-level heavy-tail aggregation path** for `W3`:

1. each core still builds bounded queue regions locally;
2. W3 extracts small mice-like blocked fragments from multiple cores of the
   same host;
3. fragments are aggregated into one host-level batch inside an age window;
4. the destination still evaluates and commits the batch as a whole.

This means the online unit is no longer "one core queue segment only". W3 can
now form a real batch from multiple shallow queues without falling back to
single-request migration.

## Representative Results

Median over the frozen seeds `{11,23,37,47,59}`:

| Scenario | Method | P99 | P999 | SLO violation | migration_rate | invalid_ratio |
|---|---:|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | B2 | 1610 | 2270 | 0.468508 | 0.0123568 | 0.221303 |
| W2 burst rho=0.85 | M0 | 1420 | 2010 | 0.386676 | 0.0400124 | 0.145056 |
| W2 burst rho=0.85 | M1-AQB | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| W2 burst rho=0.85 | M2-DQB | 358 | 572 | 0.311241 | 0.0115643 | 0.0290325 |
| W3 heavy-tail rho=0.85 | B2 | 200 | 394 | 0.096214 | 0.0221686 | 0.297758 |
| W3 heavy-tail rho=0.85 | M0 | 186 | 360 | 0.088628 | 0.0402885 | 0.0508681 |
| W3 heavy-tail rho=0.85 | M1-AQB | 176 | 338 | 0.084086 | 0.0451337 | 0.035445 |
| W3 heavy-tail rho=0.85 | M2-DQB | 202 | 398 | 0.096336 | 0.00000666 | 0 |
| W1 saturation rho=0.95 | B2 | 1380 | 1600 | 0.996324 | 0.000411213 | 0.402612 |
| W1 saturation rho=0.95 | M0 | 1500 | 2700 | 0.991473 | 0.0394141 | 0.379253 |
| W1 saturation rho=0.95 | M1-AQB | 1370 | 1580 | 0.996731 | 0.0175483 | 0.249164 |
| W1 saturation rho=0.95 | M2-DQB | 1380 | 1590 | 0.996014 | 0 | 0 |

## DQB Diagnostics

### W2 burst rho=0.85

- `batch_candidate_median = 840,591`
- `batch_selected_median = 1,201`
- `batch_move_median = 14,446`
- `avg_move_batch_size_median = 8.59`
- `batch_size_8_31_median = 1,201`
- `target_plan_reject_median = 3,299,774`

### W1 saturation rho=0.95

- `batch_candidate_median = 25,488,653`
- `batch_selected_median = 0`
- `batch_move_median = 0`
- `migration_rate_median = 0`
- `target_plan_reject_median = 100,354,081`
- `saturation_guard_median = 1,631,169`

### W3 heavy-tail rho=0.85

- `batch_candidate_median = 1`
- `batch_selected_median = 1`
- `batch_move_median = 8`
- `avg_move_batch_size_median = 8`
- `batch_size_8_31_median = 1`
- `batch_type_mice_median = 1`
- `target_plan_reject_median = 2`

## Interpretation

1. **W2 remains the strongest positive result.** The host-aggregation work did
   not damage the burst path. DQB still delivers real `8-31` task batches and
   large tail-latency gains over `M1_AQB_PM` with much lower migration cost.

2. **W1 still validates the saturation guard.** The new code keeps the strict
   no-migrate semantics under saturation. This protects the system from the
   classic proactive-migration backlash at high load.

3. **W3 now proves a sharper boundary than before.** The old result said:
   "single-core large batches do not form." The new result says something
   stronger: even after lifting the batch definition to host-level aggregation,
   DQB can only form a tiny number of `8`-task mice batches, and those batches
   are too sparse to improve median P99/P999.

4. **Why W3 still does not improve.** The heavy-tail problem here is mainly
   sparse blocking, not bursty spatial imbalance. Risky short tasks exist, but
   they are scattered across many shallow queues. Aggregating them at host
   scope is enough to create a legal batch, but not enough to create a large
   cross-host pressure gap that amortizes the migration cost for the batch as a
   whole.

5. **This negative result is useful.** It means the next W3 step should not be
   "keep tuning thresholds." It should be one of:
   - a hybrid policy that preserves large-batch DQB for burst repair and uses a
     different sparse-risk repair path for heavy-tail queues; or
   - a higher-level batch definition with stronger shared context, such as
     host-level arrival-epoch bins plus explicit no-migrate reasoning.

## Bottom Line

The project now has a clean closed loop:

- **implemented**: large contiguous batch DQB plus host-level W3 aggregation;
- **verified**: W2 gain is stable, W1 guard is stable, W3 host-level batches do
  form;
- **measured**: W3 still does not gain tail improvement from honest large-batch
  migration;
- **concluded**: burst repair and heavy-tail repair should no longer be forced
  into the exact same batch definition.

## Next Work

- add explicit `no_migrate_reason` counters;
- add a hybrid `W3 sparse-risk fallback` that remains above per-task size-1;
- compare host-level aggregation against a host-level arrival-epoch bin design;
- add exact batch-size and source-depth histograms for the new W3 path.
