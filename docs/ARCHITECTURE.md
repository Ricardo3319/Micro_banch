# RescueSched Architecture

## Purpose

The current research target is `M1_RESCUE_SCHED`, a single-host multicore RPC
queue-repair policy. Each worker core owns a non-preemptive FIFO queue. A queued
request descriptor may move between cores; a running request may not move.

The paper path contains three layers:

1. a policy-independent workload trace;
2. per-core FIFO execution and bounded queue inspection;
3. request-level local-miss/remote-meet migration decisions.

`L0_RANDOM_CORE`, `L1_WORK_STEALING_POLLING`, and `M0_ALTO_THRESHOLD` are the
paper comparison methods. The one-shot work-stealing and immediate proactive
modes remain diagnostics. AQB/DQB host-level migration code remains available
only for CLI compatibility and is outside the RescueSched paper path.

## Module boundaries

- `src/app`: RescueSched experiment entry points and versioned CSV export.
- `src/core`: event loop, FIFO execution, estimator updates, and migration.
- `include/sim/model`: requests, events, cores, nodes, and intrusive queues.
- `include/sim/workloads`: versioned trace generation and service models.
- `include/sim/metrics`: measurement-cohort latency and migration statistics.
- `include/sim/algorithms`: current baselines plus legacy host-level methods.
- `artifacts`: corrected schema-versioned pilot, holdout, and full runs.

## RescueSched data path

```text
Versioned workload trace
  -> initial per-core FIFO placement
  -> bounded source scan
  -> local deadline-miss prediction
  -> bounded target scan
  -> remote deadline-feasibility prediction
  -> migration budget and target reservation
  -> descriptor handoff delay
  -> target FIFO arrival
  -> request completion and cohort accounting
```

## Invariants

- Time is measured in microseconds.
- A request carries an observable RPC method and an explicit server-side
  deadline budget; hidden service time never selects the estimator bucket.
- Workload, routing, estimator, and control randomness use independent streams.
- All methods compared at one workload/rho/seed consume the same trace.
- Migration cost is paid as elapsed simulated time, not only as a score penalty.
- Strong work stealing, ALTO-style migration, and RescueSched pay the same
  descriptor-handoff cost.
- In-flight descriptors are absent from both queues and cannot migrate twice.
- Target reservations are visible to later decisions in the same control epoch.
- Warmup requests do not enter paper metrics; all measurement requests drain.

## Legacy boundary

DQB/AQB modes remain compiled to avoid an unrelated high-risk code removal.
Their old planning documents, analysis scripts, figures, and Step 00-18
artifacts are no longer part of the active repository. Do not mix their output
with RescueSched v2 schemas or paper claims.
