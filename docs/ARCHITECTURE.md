# Architecture

## Purpose

This repository is a C++ discrete-event simulator for microsecond-scale RPC
queue repair. The current research target is `M2_DQB_PM`
(Distribution-aware Queue-Batch Proactive Migration).

The next planned evaluation target is `DQB-v2`, documented in
`docs/DQB_V2_EXPERIMENT_METHOD.md`. DQB-v2 keeps `M2_DQB_PM` as the v1
implementation reference and evaluates a prior-calibrated distribution-batch
design: workload priors plus local queue summaries form the control-plane batch
descriptor, while per-request events remain the simulator statistics unit.

`M0_Proactive` and `M1_AQB_PM` are retained as baselines:

- `M0_Proactive`: single-task predictive migration.
- `M1_AQB_PM`: task-candidate scoring with batched selection.
- `M2_DQB_PM`: distribution-aware queue-batch migration.

## Module Boundaries

- `src/app`: experiment entry points and CSV export.
- `src/core`: event loop, event handlers, migration commit, and stale views.
- `include/sim/model`: task, event, node, core, and intrusive queue models.
- `include/sim/algorithms`: host-level scheduling and migration policies.
- `include/sim/workloads`: W1/W2/W3 arrival and service-time generators.
- `include/sim/metrics`: latency, migration, and batch diagnostics.
- `config`: frozen constants snapshots.
- `artifacts`: experiment outputs.

Planning artifacts for the DQB-v2 study are emitted under
`artifacts/step-09-dqb-v2-plan/` by
`scripts/emit_dqb_v2_experiment_plan.py`. These artifacts define the planned
scenario matrix, ablations, metrics schema, figure plan, and acceptance
criteria; they do not run simulations.

## DQB-PM Data Path

```text
Core wait queue
  -> fixed-size QueueSummary
  -> contiguous batch regions
  -> QueueBatchCandidate
  -> FIFO region expansion under task/work caps
  -> optional W3 host-level fragment aggregation
  -> target virtual-core batch placement estimate
  -> incoming host/core reservation update
  -> whole-batch commit or no-migrate
  -> per-request forwarding events
```

The online control path must not sort or score the full task queue. A task is
still the execution and statistics unit, but the migration control unit is a
queue batch or distribution region.

## Core Constraints

- Time unit is `us`.
- Same-timestamp event order: `TASK_FINISH > TASK_ARRIVE > TASK_GENERATE`.
- Cross-host realtime queue reads are disallowed.
- Remote decisions use stale workload plus `incoming_reservation`.
- DQB target feasibility uses virtual core slots derived from stale workload and
  scheduled incoming core reservations.
- W3 can aggregate multiple blocked fragments from different local cores into
  one host-level batch before destination scoring.
- W1 saturation uses a strict guard that can force `M2_DQB_PM` into full
  no-migrate mode.
- Waiting request metadata can migrate; running execution state cannot.
