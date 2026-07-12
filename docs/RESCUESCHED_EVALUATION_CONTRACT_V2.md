# RescueSched Corrected Evaluation Contract v2

Date: 2026-07-12

## Question

Does request-specific local-miss/remote-meet filtering improve server-side
deadline goodput over strong work stealing and threshold-style queued-descriptor
migration when every policy consumes the same trace and pays the same handoff
cost?

## Compared policies

- `L0_RandomCore`: fixed initial trace placement; no queue repair.
- `L1_WorkStealingPolling`: every idle core can poll for work; a successful
  steal pays the configured descriptor-handoff delay and reserves its target.
- `M0_AltoThreshold`: select a request predicted to miss locally when its source
  exceeds a queue-work threshold; migrate only if the least-loaded target is
  predicted to be no worse. It does not require remote deadline feasibility.
- `M1_RescueSched`: require both predicted local miss and predicted remote meet,
  with the same check period, scan bound, target bound, budget, and handoff cost.
- `B0_IdealCFCFS`: optional pooled upper bound, clearly labeled non-deployable.

The historical one-shot `L1_WorkStealing` and immediate `M0_IntraHostProactive`
remain diagnostic modes and are not the strong paper baselines.

## Fairness controls

- One immutable `rescuesched-trace-v2` object per workload/rho/seed is shared by
  every policy.
- All descriptor moves pay `rescue_migration_cost_us`; in-flight descriptors are
  unavailable and target reservations are visible to concurrent decisions.
- Periodic policies use the same default 1 us control period. RescueSched and
  ALTO-style migration use the same scan, target, and per-period move bounds.
- Work stealing is allowed one attempt per eligible idle core per poll because
  it is a distributed pull baseline; its attempts, successes, moved work, and
  polling frequency are reported rather than hidden.
- The default deployable estimator is method-keyed EWMA. Oracle is an upper
  bound and never enters the primary comparison.

## Primary matrix

- Workloads: W3 heavy-tail primary; W2 burst boundary; W1 balanced sanity.
- Rho: W3 `{0.70, 0.85, 0.90}`; W2 `{0.70, 0.85}`; W1 `{0.70, 0.85}`.
- Seeds: development `{11,23,37,47,59}`; holdout `{71,83,97,109,127}`.
- Primary metrics: deadline violation rate, SLO goodput, migrated-work rate,
  handoff count/latency, and scheduler attempts per completed request.
- Secondary metrics: exact P99/P999, per-method violation, and queue-repair
  success diagnostics. BMR/UMR remain non-causal diagnostics.

## Go/no-go gate

The RescueSched paper claim survives only if, on holdout seeds with EWMA and
paid handoff, RescueSched improves W3 deadline violation over both strong
baselines with a paired 95% interval excluding zero at one or more non-overload
rho points, without relying on more migrated work. W2 and W1 are reported even
when negative. If this gate fails, the work is repositioned as a boundary or
estimation study before further paper writing.
