# CloudLab Preregistration

## Boundary

The repository implements a real Linux UDP request/response path. The server
uses `SO_REUSEPORT` ingress shards and pinned workers; clients submit frozen
flow-affine traces open-loop over the experiment network. This implementation
closes the network/runtime blocker, but smoke results are not final physical
evidence.

Any protocol change after inspecting a final repetition requires a documented
version change and a complete rerun of every affected paired block.

## Hypotheses And Matrix

Primary hypothesis: at W3 rho 0.85 and 0.90, `M1_RescueSched` reduces
server-side deadline violation relative to `L1_WorkStealingPolling` and
`M0_AltoThreshold` without moving more descriptor work.

Required anchors:

| Workload | Rho | Role |
| --- | ---: | --- |
| W3 | 0.85 | Primary confirmation |
| W3 | 0.90 | Primary confirmation |
| W3 | 0.70 | Negative boundary |
| W2 | 0.85 | Burst/tail boundary |

Methods are `L0_RandomCore`, `L1_WorkStealingPolling`, `M0_AltoThreshold`, and
`M1_RescueSched`. Seeds are
`11,23,37,47,59,71,83,97,109,127`. Each seed is one paired unit, and all four
methods consume byte-identical trace input.

The target cohort is 200,000 warmup plus 1,000,000 measurement requests. Any
smaller cohort must be chosen before final runs and frozen for every method and
anchor.

## Frozen Runtime Rules

- Workers use distinct physical cores; primary runs do not use SMT siblings as
  independent workers.
- Service is non-preemptive and only queued descriptors migrate.
- All methods share UDP protocol, ingress sockets, worker count, CPU list,
  queues, estimator, handoff primitive, logging, and trace.
- Kernel `SO_REUSEPORT` selection defines the initial worker. Stable client
  source ports and the same trace preserve flow-to-ingress mapping.
- Two clients partition by `flow_id % 2`; both use the same source-port base on
  their separate hosts.
- Clients use one shared absolute start timestamp.
- The estimator is method-keyed EWMA with alpha 0.05, updated after completion.
- Destination insertion is FIFO append-tail.
- Server completion and client RTT are separate observations.

## Load Calibration

The trace rho is a source label, not a claim about realized physical load.
Before final repetitions, use non-final seeds to derive one arrival scaling
factor per workload/rho anchor. Freeze the factors in a checksummed manifest
and apply the same factor to every method in a paired block. Do not recalibrate
from final deadline or tail outcomes.

## Order, Failure, And Stopping

Generate the committed SHA-256-derived schedule with
`generate_cloudlab_run_order.py` before final execution. The archived CSV fixes
the within-block order and balances every method across positions.

Application overload, accepted-request timeout, or failure to drain is a
retained result. Node loss, control-plane loss, kernel failure, unavailable
NIC, corrupt output, trace mismatch, affinity failure, or runtime invariant
failure is infrastructure failure.

If one method has infrastructure failure, retain its logs and rerun the full
four-method block under a new attempt ID. Allow at most three infrastructure
attempts. Include the first complete valid attempt. Run all ten seeds; do not
stop early or add seeds after inspecting confidence intervals.

## Outcomes

Primary outcome: paired difference in server-side deadline violation rate,
baseline minus RescueSched, against each strong baseline.

Primary efficiency constraint: RescueSched median migrated-work rate is no
greater than the compared baseline.

Secondary outcomes include server goodput and P50/P99/P999 completion, client
RTT P50/P99/P999, submitted/completed/timed-out/dropped counts, migration and
handoff distributions, decision cost, CPU/perf counters, and NIC/RSS packet and
drop counters.

Use paired bootstrap 95% intervals over the ten seeds for deadline-rate
differences. Publish the two boundary anchors regardless of direction. Report
simulator-to-physical reversals rather than tuning them away.

## Required Evidence

Archive commit and dirty status, compiler/build flags, kernel and CPU/NUMA,
governor and SMT, process/IRQ affinity, NIC driver/firmware/RSS, exact command
lines, CPU allocation, trace and schedule hashes, request/decision/migration
logs, client RTT logs, summaries, checksums, and failed attempts.
