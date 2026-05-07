# DQB-v2 Experiment Method

## Purpose

This document fixes the evaluation method for `DQB-v2`, a prior-calibrated
distribution-aware queue-batch proactive migration design.

The central claim is:

> In microsecond-scale RPC systems, the online migration control path should
> avoid per-task full scoring. It should estimate the risk of a bounded queue
> batch from workload priors plus local queue summaries, then migrate only when
> the whole batch is expected to reduce tail latency under stale remote views
> and target-side reservation constraints.

`M2_DQB_PM` in the current codebase remains the implemented DQB-v1 reference.
`DQB-v2` is the planned experimental target and should be evaluated as an
incremental design over DQB-v1.

## Research Questions

- **Q1 Effectiveness**: Does DQB-v2 reduce `P99`, `P999`, and SLO violation
  rate compared with `B1`, `B2`, `M0`, `M1`, and current `M2_DQB_PM`?
- **Q2 Mechanism**: Are the gains caused by prior-calibrated batch risk,
  local queue summaries, incoming reservation, and saturation guard, rather
  than simply by moving more requests?
- **Q3 Overhead**: Does the control path remain bounded and compatible with
  microsecond-scale checks?
- **Q4 Boundary**: Which workload classes are suitable for large batch repair,
  and where does batch migration become the wrong abstraction?
- **Q5 Realism**: Do CloudLab-calibrated network, host, summary, and forwarding
  costs preserve the simulator conclusions?

## Methods

The main comparison set is:

| Method | Role |
|---|---|
| `B1_PowerOf2` | Initial-dispatch baseline with no migration. |
| `B2_Reactive` | Threshold-triggered reactive migration baseline. |
| `M0_Proactive` | Single-task proactive prediction baseline. |
| `M1_AQB_PM` | Task-candidate batch-selection transitional baseline. |
| `M2_DQB_PM` | Current DQB-v1 queue-batch implementation. |
| `DQB-v2` | Planned prior + local-summary distribution-batch design. |

The core workloads are:

| Workload | Purpose |
|---|---|
| `W1 Poisson+Bimodal` | Saturation and no-migrate boundary. |
| `W2 MMPP+Bimodal` | Burst hotspot and pressure-batch repair. |
| `W3 Poisson+Lognormal` | Heavy-tail, mice-behind-elephant, sparse blocking. |
| `Heterogeneous W2/W3` | Slow-node pressure and slow-to-fast repair. |

The default protocol is:

- warmup: `200k` requests
- measurement: `1M` requests
- seeds: `{11,23,37,47,59}`
- report: median plus bootstrap 95% confidence interval
- stable-gain gate: at least `4/5` seeds improve in the same direction and
  median improvement is greater than `5%`

## Required Ablations

Distribution mechanism:

- `DQB-v2/full`
- `DQB-v2/prior-only`
- `DQB-v2/summary-only`
- `DQB-v2/queue-length-only`
- `DQB-v2/no-confidence`
- `DQB-v2/no-pattern-type`

Batch migration mechanism:

- `DQB-v2/single-move`
- `DQB-v2/batch-size={2,4,8,16,32,64}`
- `DQB-v2/no-host-aggregation`
- `DQB-v2/no-arrival-epoch-binning`
- `DQB-v2/pressure-batch-only`
- `DQB-v2/blocking-batch-only`

Target protection:

- `DQB-v2/no-reservation`
- `DQB-v2/host-reservation-only`
- `DQB-v2/core-reservation`
- `DQB-v2/no-target-harm-guard`
- `DQB-v2/no-saturation-guard`

Realism and robustness:

- `Oracle-E`
- `EWMA-E`
- `Noisy-E`
- `eligible_fraction={1.0,0.75,0.50}`
- `sync_period_us={5,10,20,50}`
- `check_period_us={0.5,1,2,5}`

## Metrics Schema

Primary metrics:

- `P99_us`
- `P999_us`
- `slo_violation_rate`
- `short_slo_violation_rate`
- `long_slo_violation_rate`
- `mice_slo_violation_rate`
- `elephant_slo_violation_rate`

Migration side effects:

- `migration_rate`
- `migration_work_rate`
- `invalid_migration_ratio`
- `target_harm_est_us`
- `dst_queue_harm_after_arrival`
- `per_dst_migrated_work_us`

Batch diagnostics:

- `batch_candidate_count`
- `batch_selected_count`
- `batch_move_count`
- exact batch-size histogram
- `batch_type`
- `batch_confidence`
- `risk_mass_est`
- `risk_reduction_est`
- `source_queue_depth`
- `source_queue_work_us`
- `destination_virtual_occupancy`

No-migrate reasons:

- `NO_BATCH_FORMED`
- `LOW_CONFIDENCE`
- `LOW_EXPECTED_GAIN`
- `DST_RESERVATION_HIGH`
- `DST_TAIL_HARM`
- `SATURATION_GUARD`
- `BUDGET_EXHAUSTED`
- `SPARSE_BLOCKING_NOT_BATCHABLE`

Control-plane overhead:

- `summary_update_cost_est_us`
- `batch_estimation_cost_est_us`
- `target_selection_cost_est_us`
- `candidates_per_check`
- `summaries_per_check`
- `control_messages_per_migration`
- `decision_cost_over_check_period`

## Evidence Plan

1. **Main gain**: W2 burst comparison across `B1/B2/M0/M1/M2-v1/DQB-v2`,
   reporting both tail latency and migration rate.
2. **Mechanism ablation**: full vs prior-only, summary-only, queue-length-only,
   no-reservation, and no-saturation-guard.
3. **Batch behavior**: exact batch-size histogram, batch type distribution, and
   risk mass vs selected probability.
4. **W3 boundary**: no-migrate reason distribution under heavy-tail sparse
   blocking.
5. **Heterogeneous slow-node behavior**: fast/slow source-destination migration
   matrix and target congestion diagnostics.
6. **Control-plane feasibility**: decision cost, candidate count, and summary
   count versus load and queue depth.
7. **CloudLab calibration replay**: rerun key simulator points with measured
   `T_net`, `T_host`, forwarding, summary, and batch-metadata costs.

## Acceptance Criteria

- W2 burst: DQB-v2 improves at least one key P99/P999 point by more than `5%`
  over both `M1_AQB_PM` and current `M2_DQB_PM`, with migration rate below
  `5%`.
- W1 saturation: DQB-v2 is not meaningfully worse than `B2_Reactive` and keeps
  migration low or disabled.
- W3 heavy-tail: the result must be explainable. A positive result supports
  hybrid blocking-batch repair; a negative result must be backed by no-migrate
  reasons that show sparse blocking is a batch-migration boundary.
- Heterogeneous scenarios: DQB-v2 must not repeat M1's known `rho=0.85`
  budget-misalignment failure without a diagnostic explanation.
- Control overhead: estimated control cost should stay below `20%` of the
  check period for the main configuration.
- CloudLab replay: method ranking should not invert, and the knee point should
  shift by no more than one rho bucket.

## Generated Planning Artifacts

Run:

```powershell
python scripts\emit_dqb_v2_experiment_plan.py
```

This writes the planning bundle under:

```text
artifacts/step-09-dqb-v2-plan/
```

The generated bundle contains:

- `README.md`
- `experiment_matrix.csv`
- `ablations.csv`
- `metrics_schema.csv`
- `figure_plan.csv`
- `acceptance_criteria.md`

These files are planning artifacts only. They do not run experiments.
