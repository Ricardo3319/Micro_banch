# RescueSched CloudLab Preregistration

## Status and evidence boundary

This document freezes the first CloudLab evaluation protocol before any final
CloudLab repetitions are inspected. It is an experiment plan, not a result.
The current repository has an in-process synthetic request runtime, but no
networked RPC server/client or NIC/RSS data path. Therefore every CloudLab
result field remains `[PHYSICAL RESULT REQUIRED]` until a committed RPC runtime
and the protocol below produce archived raw evidence.

Protocol changes after the first final repetition require a new version of
this document, a stated reason, and a complete rerun of every affected point.
No safeguard, threshold, estimator, or inclusion rule may be changed after
examining final outcomes and then applied only to selected traces.

## Hypotheses

Primary hypothesis: at W3 rho 0.85 and 0.90, `M1_RescueSched` reduces
server-side deadline violation relative to both `L1_WorkStealingPolling` and
`M0_AltoThreshold` on paired frozen traces without moving more descriptor work.

Boundary hypotheses, reported regardless of direction:

- W3 rho 0.70 may favor polling work stealing.
- W2 rho 0.85 may reduce deadline violations while degrading unconditional
  server-side P99/P999.
- Client-observed end-to-end RTT may differ from server-side completion time.

## Frozen experiment matrix

Primary methods in one runtime and resource budget:

- `L0_RandomCore`
- `L1_WorkStealingPolling`
- `M0_AltoThreshold`
- `M1_RescueSched`

Anchors:

| Workload | Rho label | Role |
| --- | ---: | --- |
| W3 | 0.85 | Primary confirmation |
| W3 | 0.90 | Primary confirmation |
| W3 | 0.70 | Required negative boundary |
| W2 | 0.85 | Required burst/tail boundary |

Each anchor uses the ten frozen seeds `11,23,37,47,59,71,83,97,109,127`.
Each seed is one paired unit. All four policies must consume byte-identical
trace input with matching embedded and file SHA-256 values.

The default trace cohort is 200,000 warmup requests followed by 1,000,000
measurement requests. If the RPC implementation cannot sustain this cohort,
the cohort size must be revised before final runs and then frozen for every
method and anchor. A reduced cohort may not be chosen after observing one
method's tail result.

## Runtime and estimator contract

- Workers are pinned to distinct physical cores. Logical siblings are not used
  as separate workers in the primary configuration.
- Service is non-preemptive. Only `QUEUED` descriptors may migrate.
- All methods share request execution, queues, handoff primitive, logging,
  clocks, worker count, and CPU allocation.
- The deployable estimator is shared-global, method-keyed EWMA with alpha 0.05,
  updated only after completion. It cannot read the current request's hidden
  service demand.
- Destination insertion is FIFO append-tail.
- A successful move records source removal, in-flight reservation, destination
  insertion, measured primitive duration, and request migration count.
- Server-side completion and client-observed RTT are separate fields. Neither
  may be inferred from the other.

Any difference from these implementation-alignment settings is
`[IMPLEMENTATION DETAIL REQUIRED]` and must be frozen before final execution.

## Offered-load calibration

The W2/W3 rho value is first a source-trace label. Physical realized load is
reported separately and is not assumed to equal the simulator label.

Before final repetitions:

1. Run isolated per-method service calibration on a separate calibration trace
   that does not use the ten final seeds.
2. Measure completed service work, runtime overhead, and effective worker
   capacity with the final worker count and CPU allocation.
3. Derive one arrival-time scaling factor per workload/rho anchor.
4. Freeze the factors in a versioned calibration manifest before final runs.
5. Apply the same factor to all four methods for a paired trace.

Do not recalibrate from a method's final deadline or tail outcome. Report both
the configured trace label and realized submitted/completed work per unit time.

## Run order and pairing

For each workload/rho/seed block, generate one permutation of the four methods
before execution using a committed script and the fixed ordering seed string:

```text
rescuesched-cloudlab-order-v1|workload|rho|seed
```

The script must derive the permutation from SHA-256, archive the complete
schedule before any final run, and never consult performance output. Blocks
may execute in any host-availability order, but the within-block method order
must match the archived schedule.

The committed generator hashes each frozen block seed, partitions the 40
blocks into ten deterministic groups of four, hashes one base method order per
group, and assigns its four cyclic rotations. The resulting design remains
fully SHA-256-derived while placing every method exactly ten times in each
within-block position. Generate the immutable schedule once and verify it
before execution:

```bash
python3 scripts/generate_cloudlab_run_order.py \
  --output physical-results/cloudlab-final/run-order.csv
python3 scripts/generate_cloudlab_run_order.py \
  --verify physical-results/cloudlab-final/run-order.csv
```

The generated CSV contains 160 rows: four positions for each of 40 paired
blocks. Its checksum sidecar and the exact CSV bytes are required archived
evidence. Do not use `--force` for a preregistered final schedule.

Warmup and measurement use the frozen trace markers. After submission ends,
the runtime drains all accepted descriptors to `DONE`, explicit `CANCELLED`, or
recorded timeout/drop state before the next method starts. No process or queue
state is reused across methods.

## Timeout, failure, and rerun rules

- Application overload or a policy failing to drain by the frozen application
  timeout is a retained outcome. Incomplete requests are reported separately
  as timed out/dropped and count as deadline failures where the contract says
  they were accepted.
- Infrastructure failures include node loss, SSH/control-plane loss, kernel
  panic, unavailable NIC, corrupt output, trace SHA mismatch, affinity failure,
  or a runtime invariant failure. These do not become performance samples.
- If any method in a paired block has an infrastructure failure, retain the
  failed logs and rerun the complete four-method block under a new attempt ID.
- At most three infrastructure attempts are allowed per block. Exhaustion
  aborts that anchor; it does not permit replacing the seed.
- The first complete valid attempt is included. Later duplicate successful
  attempts are diagnostics and cannot replace it based on favorable results.

## Stopping rule

Run all ten preselected paired units for every anchor. Do not stop early for
statistical significance, a desired effect size, or an unfavorable direction.
Do not add seeds after looking at confidence intervals. An anchor is complete
only when all ten paired units pass trace identity, manifest, and runtime
invariant checks.

## Metrics and statistical analysis

Primary outcome:

- Paired difference in server-side deadline violation rate, baseline minus
  RescueSched, for each strong baseline.

Primary efficiency constraint:

- RescueSched median migrated-work rate is no greater than the compared
  baseline's median migrated-work rate.

Secondary outcomes, reported independently:

- server-side goodput;
- server-side P50, P99, and P999 completion time;
- client-observed end-to-end RTT P50, P99, and P999;
- submitted, completed, timed-out, dropped, and cancelled counts;
- migrated requests, migration count, and migrated service work;
- decision duration/cycles, polling cycles, handoff distribution;
- CPU utilization, context switches, cache misses, and NUMA movement;
- NIC/RSS configuration and packet/drop counters.

Use paired bootstrap 95% intervals over the ten frozen seeds for deadline-rate
differences. Report medians over paired units for tails and moved work. P99 and
P999 are not substituted for deadline violation or goodput. BMR, UMR, and other
counterfactual simulator diagnostics are unavailable as physical observables
unless a separately justified measurement method is added before final runs.

## Acceptance and interpretation

The primary physical hypothesis is supported only if W3 rho 0.85 or 0.90 has a
paired 95% interval above zero against both strong baselines and RescueSched
moves no more work under the frozen constraint. This does not imply universal
tail improvement or production readiness.

W3 rho 0.70 and W2 rho 0.85 are published in the result package regardless of
direction. A W2 deadline improvement does not cancel a P99/P999 regression.
Any simulator-to-physical reversal is reported and investigated, not hidden by
post hoc parameter changes.

## Required archived evidence

- commit, dirty status, compiler, build flags, kernel, firmware, CPU/NUMA;
- governor, SMT, IRQ affinity, NIC driver/firmware, RSS key and indirection;
- client/server CPU allocation and process affinity;
- command line, environment, trace identities, run-order schedule;
- request, decision, migration, client RTT, perf, and NIC logs;
- summary CSV, paired-analysis CSV, checksums, and failure-attempt logs;
- `[PHYSICAL RESULT REQUIRED]` for every reported numerical result.
