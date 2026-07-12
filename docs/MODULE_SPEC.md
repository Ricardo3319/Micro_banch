# RescueSched Module Spec

## Paper-line methods

- `L0_RandomCore`: fixed initial per-core placement without queue repair.
- `L1_WorkStealingPolling`: periodic idle-core pull with paid handoff.
- `M0_AltoThreshold`: local-miss/no-worse threshold migration with paid handoff.
- `M1_RescueSched`: local-miss/remote-meet descriptor migration.
- `M1_RescueSched_NoTargetSafety` and `M1_RescueSched_NoRescuable`: diagnostic
  ablations only.

Host-level `B0/B1/B2/M0`, AQB, and DQB modes remain implemented for historical
reproduction but are not part of the current paper interface.

`L1_WorkStealing` and `M0_IntraHostProactive` are retained as diagnostic legacy
variants and are not used as the strong paper baselines.

## Required interfaces

- `Task`: observable RPC method, explicit deadline budget, hidden actual service
  time, estimated service time, cohort identity, and migration state.
- `WorkloadTrace`: immutable, versioned requests shared by every compared method
  for one workload/rho/seed point.
- `Simulator::configure(...)`: policy configuration plus an optional shared
  trace; legacy callers may request deterministic trace construction.
- `TASK_MIGRATION_ARRIVE`: completes a paid descriptor handoff before target
  FIFO insertion.
- `MetricsCollector`: exact latency samples and measurement-cohort denominators.
- Rescue CSV v2: trace identity, workload semantics, cohort sizes, policy
  configuration, latency outcomes, and migration outcomes.

## Current boundary

Deadlines are server-side budgets. Network RTT, packet processing, transport
serialization, and a CloudLab runtime are deliberately outside this simulator
validity milestone. Oracle estimates remain available only as an explicitly
labeled upper bound.

Target-safety and migration-benefit counters remain diagnostic until a causal
counterfactual implementation exists; they are not paper claims.

## Legacy history

Detailed AQB/DQB structures, results, and proposed DQB-v2 experiments are kept
in the `DQB_*` documents. Those files describe earlier research iterations and
must not be treated as the current implementation target.
