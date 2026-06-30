# Step-10 DQB Closed-Loop Summary

Date: 2026-05-08

## Goal

Write the current DQB experimental results into project documentation, plan the
next experiment phase, run a focused validation pass, and close the loop with
analysis.

## Commands Run

```powershell
.\build-aqb-check\simulator.exe dqb-w2-only
.\build-aqb-check\simulator.exe dqb-w3-only
.\build-aqb-check\simulator.exe dqb-w1-only
```

Note: the local Ninja build directory had stale lock/dependency-file behavior,
so the later diagnostic build was refreshed with single-step compile, archive,
and link commands instead of repeatedly running full `ninja -v`.

## Validation Outputs

- `artifacts/step-08-dqb-batch/dqb_w2_only.csv`
- `artifacts/step-08-dqb-batch/dqb_w3_only.csv`
- `artifacts/step-08-dqb-batch/dqb_w1_only.csv`

## Focused Validation Results

| Scenario | P99 median | P999 median | SLO violation median | migration_rate median | invalid_ratio median | selected batches median | moved requests median |
|---|---:|---:|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | 358 | 572 | 0.311241 | 0.0115643 | 0.0290325 | 1201 | 14446 |
| W3 heavy-tail rho=0.85 | 202 | 398 | 0.096336 | 0.00000666 | 0 | 1 | 8 |
| W1 saturation rho=0.95 | 1380 | 1590 | 0.996014 | 0 | 0 | 0 | 0 |

After the Phase-A diagnostic patch, the focused W1 median remained a no-migrate
boundary while the control path became bounded:

| Scenario | P99 median | P999 median | migration_rate median | batch candidates median | selected batches median | saturation guards median |
|---|---:|---:|---:|---:|---:|---:|
| W1 saturation rho=0.95 | 1310 | 1560 | 0 | 0 | 0 | 3264062 |

## Analysis

The focused rerun confirms the stored Step-09 summary.

W2 is the strongest positive case. DQB forms real `8-31` request batches and
reduces tail latency while using less migration budget than M1.

W1 validates the saturation guard. The algorithm selects no batches and moves
no requests, avoiding the proactive migration failure mode observed in M0.

W3 is a useful negative result. Host-level aggregation can form a legal
8-request batch, but the event is too sparse to move median P99/P999. This
supports the conclusion that heavy-tail sparse blocking needs a hybrid
sparse-risk repair path or a richer arrival-epoch batch definition.

## Documentation Updated

- `docs/DQB_CURRENT_RESULTS_ANALYSIS.md`

## Phase-A Follow-Up

The diagnostics requested for the next phase were implemented:

- no-migrate reason counters
- short/long and mice/elephant SLO violation rates
- exact batch-size histogram
- source queue depth and source queue work
- destination virtual-core occupancy
- target harm estimate
- migration work rate
- control-plane cost estimate

During validation, W1 initially exposed a control-plane ordering bug: the
algorithm set `batch_cap=0` under saturation but still scanned queue summaries
and target plans. The guard was moved before candidate construction, which
preserves the no-migrate result and reduces the W1 focused run to normal time.

Next, run the DQB-v1 diagnostic matrix for W2/W3/heterogeneous cases and then
compare against DQB-v2.
