# Module Spec

## Implemented Methods

- `B0_IdealCFCFS`: global FIFO reference with pull cost.
- `B1_PowerOf2`: stale-view power-of-two dispatch.
- `B2_Reactive`: threshold-triggered reactive migration.
- `M0_Proactive`: per-task predictive migration baseline.
- `M1_AQB_PM`: bounded task-candidate scan with batched selection.
- `M2_DQB_PM`: distribution-aware queue-batch migration MVP.

## DQB-PM MVP Interfaces

- `QueueSummary`: fixed-size local queue distribution summary.
- `QueueBatchCandidate`: batch descriptor generated from a core queue, including
  structural type, distribution confidence, and source-side risk mass.
- `DqbProactiveMigrationScheduler::collect_batch_candidate(...)`: creates at
  most one batch candidate per core per control epoch.
- `DqbProactiveMigrationScheduler::collect_host_batch_candidate(...)`: creates
  one host-level W3 batch candidate by aggregating blocked fragments from
  multiple cores.
- `Simulator::handle_check_migration(...)`: performs batch-level target
  selection, estimates virtual-core batch placement, commits migrated request
  descriptors, and updates host/core incoming reservations.
- `MetricsCollector`: records batch candidates, selected batches, moved tasks,
  summary updates, reservation rejects, saturation guard activations, batch-size
  buckets, batch-type counts, and target-plan rejects.

## Current MVP Boundary

DQB-PM currently implements batch-level control with per-request forwarding.
It does not yet implement transport-level batch serialization or a calibrated
`T_batch(k)` cost model.

## DQB-v2 Evaluation Plan

`DQB-v2` is a planned evaluation target rather than an implemented method in
the current simulator. Its experimental contract is captured in
`docs/DQB_V2_EXPERIMENT_METHOD.md` and the generated planning bundle under
`artifacts/step-09-dqb-v2-plan/`.

The DQB-v2 study must compare the current `M2_DQB_PM` implementation against a
prior-calibrated distribution-batch design and must export additional metrics:
short/long and mice/elephant SLO violation rates, migration work rate,
target-harm estimates, exact batch-size histograms, no-migrate reason counters,
and control-plane cost estimates.

The current revision adds contiguous segment construction, target-side
batch-aware placement estimation, and a W3 host-level aggregation path. In W2
burst runs, committed DQB batches are centered on the `8-31` bucket instead of
size `1`. In W3 heavy-tail runs, host-level aggregation can now form real
8-task batches, but those batches are too sparse to improve median tail
latency, which exposes a real workload boundary rather than a missing
implementation feature.

## Required Next Diagnostics

- Exact batch-size histogram beyond the current coarse buckets.
- Source queue depth and destination virtual-core occupancy distribution.
- Per-destination incoming reserved work.
- Short/long and mice/elephant SLO violation rates.
- `no-migrate` reason counters.
- Hybrid sparse-risk repair or richer arrival-epoch host aggregation for
  heavy-tail workloads.
