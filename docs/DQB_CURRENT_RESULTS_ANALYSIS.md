# DQB Current Experimental Results Analysis

Date: 2026-05-08

## Scope

This document summarizes the current `M2_DQB_PM` experimental evidence and the
next closed-loop experiment plan.

The current implemented target is DQB-v1:

- current method name: `M2_DQB_PM`
- implemented abstraction: distribution-aware queue-batch proactive migration
- control unit: queue batch or distribution region
- simulator statistics unit: request/task

The planned next design, DQB-v2, is documented separately in
`docs/DQB_V2_EXPERIMENT_METHOD.md`.

## Current Result Summary

Median results over seeds `{11,23,37,47,59}`:

| Scenario | Method | P99 us | P999 us | SLO violation | Migration rate | Invalid ratio |
|---|---:|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | B2 | 1610 | 2270 | 0.468508 | 0.0123568 | 0.221303 |
| W2 burst rho=0.85 | M0 | 1420 | 2010 | 0.386676 | 0.0400124 | 0.145056 |
| W2 burst rho=0.85 | M1-AQB | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| W2 burst rho=0.85 | M2-DQB | 358 | 572 | 0.311241 | 0.0115643 | 0.0290325 |
| W1 saturation rho=0.95 | B2 | 1380 | 1600 | 0.996324 | 0.000411213 | 0.402612 |
| W1 saturation rho=0.95 | M0 | 1500 | 2700 | 0.991473 | 0.0394141 | 0.379253 |
| W1 saturation rho=0.95 | M1-AQB | 1370 | 1580 | 0.996731 | 0.0175483 | 0.249164 |
| W1 saturation rho=0.95 | M2-DQB | 1380 | 1590 | 0.996014 | 0 | 0 |
| W3 heavy-tail rho=0.85 | B2 | 200 | 394 | 0.096214 | 0.0221686 | 0.297758 |
| W3 heavy-tail rho=0.85 | M0 | 186 | 360 | 0.088628 | 0.0402885 | 0.0508681 |
| W3 heavy-tail rho=0.85 | M1-AQB | 176 | 338 | 0.084086 | 0.0451337 | 0.035445 |
| W3 heavy-tail rho=0.85 | M2-DQB | 202 | 398 | 0.096336 | 0.00000666 | 0 |

## Interpretation

### W2 Burst Is The Main Positive Result

DQB-v1 reduces W2 burst median P99 from `1100us` under `M1_AQB_PM` to `358us`,
and P999 from `1730us` to `572us`.

The important point is that DQB-v1 also uses less migration:

- `M1_AQB_PM migration_rate = 0.0453816`
- `M2_DQB_PM migration_rate = 0.0115643`

The invalid migration ratio also falls:

- `M1_AQB_PM invalid_ratio = 0.100943`
- `M2_DQB_PM invalid_ratio = 0.0290325`

This supports the core claim that DQB is not winning by moving more requests.
It wins by identifying better queue-batch repair opportunities.

The DQB batch diagnostics support that interpretation:

- `batch_candidate_median = 840591`
- `batch_selected_median = 1201`
- `batch_move_median = 14446`
- `avg_move_batch_size_median = 8.59`
- `batch_size_8_31_median = 1201`

The selected W2 batches are real `8-31` request batches, not size-1 fallbacks.

### W1 Saturation Validates No-Migrate Protection

At W1 saturation, DQB-v1 performs zero migration:

- `migration_rate = 0`
- `invalid_ratio = 0`
- `batch_selected_median = 0`
- `batch_move_median = 0`

This is the correct behavior. Under near-saturation, migration cannot create
capacity; it can only move queueing delay from one host to another. The result
shows that DQB avoids the proactive-migration backlash visible in `M0`, where
P999 grows to `2700us`.

The current implementation still generates many candidates and target rejects:

- `batch_candidate_median = 25488653`
- `target_plan_reject_median = 100354081`
- `saturation_guard_median = 1631169`

This means the next implementation should move the saturation/no-migrate check
earlier in the control path to reduce wasted candidate and target-plan work.

### W3 Heavy-Tail Is A Useful Negative Result

W3 heavy-tail remains unfavorable for large-batch DQB:

- `M1_AQB_PM P99 = 176us`
- `M2_DQB_PM P99 = 202us`
- `M1_AQB_PM P999 = 338us`
- `M2_DQB_PM P999 = 398us`

The Step-09 host-level aggregation path can form legal W3 batches, but the
event rate is too low:

- `batch_candidate_median = 1`
- `batch_selected_median = 1`
- `batch_move_median = 8`
- `avg_move_batch_size_median = 8`

The interpretation is that W3 risk is sparse heavy-tail blocking, not bursty
spatial imbalance. Risky mice exist, but they are scattered across many shallow
queues. Host-level aggregation can form a legal batch, but not frequently enough
to shift median P99/P999.

This is not a failed experiment. It defines an algorithm boundary:

- W2 pressure repair should use large contiguous DQB batches.
- W3 sparse blocking needs a hybrid sparse-risk or arrival-epoch batch design.

## Closed-Loop Validation Run

On 2026-05-08, the following validation commands were run after confirming the
build was up to date:

```powershell
cmake --build build-aqb-check
.\build-aqb-check\simulator.exe dqb-w2-only
.\build-aqb-check\simulator.exe dqb-w3-only
.\build-aqb-check\simulator.exe dqb-w1-only
```

The focused validation outputs are:

- `artifacts/step-08-dqb-batch/dqb_w2_only.csv`
- `artifacts/step-08-dqb-batch/dqb_w3_only.csv`
- `artifacts/step-08-dqb-batch/dqb_w1_only.csv`

Focused median results from the rerun:

| Focused run | P99 us | P999 us | SLO violation | Migration rate | Invalid ratio | Selected batches | Moved requests |
|---|---:|---:|---:|---:|---:|---:|---:|
| W2 burst | 358 | 572 | 0.311241 | 0.0115643 | 0.0290325 | 1201 | 14446 |
| W3 heavy-tail | 202 | 398 | 0.096336 | 0.00000666 | 0 | 1 | 8 |
| W1 saturation | 1380 | 1590 | 0.996014 | 0 | 0 | 0 | 0 |

The rerun matches the stored summary and confirms the three main claims:

1. W2 is a strong positive case for real queue-batch migration.
2. W1 validates the no-migrate guard.
3. W3 exposes sparse heavy-tail blocking as a boundary.

## Next Experiment Plan

The next experimental phase should not start by retuning thresholds. It should
first add diagnostics that make the current positive and negative results
explainable.

### Phase A: Instrumentation First

Add metrics for:

- exact batch-size histogram
- short/long SLO violation rate
- mice/elephant SLO violation rate
- no-migrate reason counters
- migration work rate
- source queue depth and source queue work
- destination virtual-core occupancy
- target harm estimate
- control-plane cost estimates

Minimum no-migrate reasons:

- `NO_BATCH_FORMED`
- `LOW_CONFIDENCE`
- `LOW_EXPECTED_GAIN`
- `DST_RESERVATION_HIGH`
- `DST_TAIL_HARM`
- `SATURATION_GUARD`
- `BUDGET_EXHAUSTED`
- `SPARSE_BLOCKING_NOT_BATCHABLE`

### Phase B: DQB-v1 Diagnostic Experiments

Run the existing DQB-v1 with new diagnostics:

- W2 burst, rho `0.85`
- W3 heavy-tail, rho `0.85`
- W1 saturation, rho `0.95`
- heterogeneous W2, rho `0.70`, `0.85`, `0.92`

Acceptance for this phase:

- W2 selected batches remain centered in `8-31`.
- W1 no-migrate is explained mainly by saturation and target harm.
- W3 no-migrate/low migration is explained by sparse blocking or low expected
  batch gain.

### Phase C: DQB-v2 Algorithm Experiment

Implement and evaluate DQB-v2:

```text
workload prior + local queue summary
  -> DistributionBatchDescriptor
  -> batch-level risk estimate
  -> batch-level destination estimate
  -> whole-batch commit or no-migrate
```

Compare:

- `M1_AQB_PM`
- current `M2_DQB_PM`
- `DQB-v2/full`
- `DQB-v2/prior-only`
- `DQB-v2/summary-only`
- `DQB-v2/no-reservation`
- `DQB-v2/no-saturation-guard`

Primary scenarios:

- W2 burst rho `0.85`
- W3 heavy-tail rho `0.85`
- W1 saturation rho `0.95`
- heterogeneous W2 rho `0.85`

### Phase D: Result Closure

For each experiment, write:

- run commands
- input configuration
- generated CSV files
- median and per-seed table
- mechanism interpretation
- failure or boundary explanation
- next action

## Immediate Conclusion

The current experimental evidence is already strong enough to support DQB-v1 as
a burst-repair algorithm:

```text
W2 burst: strong gain with lower migration rate and lower invalid ratio.
W1 saturation: correct no-migrate behavior.
W3 heavy-tail: meaningful boundary requiring a hybrid sparse-risk design.
```

The next implementation should focus on diagnostics and DQB-v2 batch
distribution descriptors, not on blind threshold tuning.
