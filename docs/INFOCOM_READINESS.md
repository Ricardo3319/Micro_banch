# INFOCOM Submission Readiness

**Assessment date:** 2026-07-15

## Decision

The repository is aligned with the narrow RescueSched experiment idea and is
ready to freeze the corrected simulation claim. It is not yet a complete,
competitive INFOCOM systems submission package.

The current evidence supports this statement only: for W3 at rho 0.85 and
0.90, request-specific local-miss/remote-meet filtering reduces server-side
deadline violations versus paid polling work stealing and paid ALTO-style
threshold migration while moving less work. It does not support universal
tail-latency improvement or real-machine deployability.

## What is complete

- Immutable per-point trace sharing across compared policies.
- Method-keyed EWMA estimator that does not inspect current hidden service time.
- Paid descriptor handoff for both strong baselines and RescueSched.
- Ten paired seeds, full warmup/measurement cohorts, and paired 95% intervals.
- Pre-registered W3 gate passes at rho 0.85 and 0.90.
- W3 rho 0.70 and W2 tail regressions are retained as required boundaries.
- CSV v2 schema, deterministic regression tests, and Linux reproduction script.
- A directly executable physical-host preflight and runbook.

## P0 blockers before submission

1. Implement a real single-host multicore RPC or request runtime with pinned
   workers and queued descriptor migration.
2. Implement frozen trace replay or an equivalent paired physical workload
   source so every policy receives the same arrivals and request classes.
3. Port `L0_RandomCore`, `L1_WorkStealingPolling`, `M0_AltoThreshold`, and
   `M1_RescueSched` into the same physical runtime and resource budget.
4. Measure end-to-end decision cost, queue handoff, CPU cycles, polling cost,
   cache misses, and NUMA effects; feed measured handoff distributions back
   into simulator sensitivity experiments.
5. Run the four pre-registered CloudLab anchors with at least ten paired trace
   repetitions: W3 rho 0.70/0.85/0.90 and W2 rho 0.85.
6. Produce physical manifests, raw logs, summary CSVs, confidence intervals,
   and simulator-versus-physical comparison tables.
7. Complete the paper's related-work positioning against ALTOCUMULUS and other
   queued-RPC scheduling systems. The novelty margin is narrow and must be
   defended by mechanism and implementation evidence, not wording alone.

## Scientific risks to resolve or disclose

- RescueSched is 124.89% worse than polling work stealing in W3 rho 0.70.
- In W2 rho 0.85 it reduces misses but raises median P99 by 6.56x to 8.55x and
  P999 by 4.68x to 8.81x versus the strong baselines.
- The design document describes an explicit target-side incremental-risk test;
  the current paper claim relies on remote feasibility and target reservation,
  not a demonstrated causal target-harm guarantee. Either implement and ablate
  that safeguard or narrow the design text to the implemented semantics.
- A single-host simulator cannot establish cache, NUMA, runtime contention,
  packet-processing, or end-to-end RPC effects.
- The current simulator uses a scalar handoff delay. Physical measurements are
  needed to justify a distribution or sensitivity range.

## Go/no-go for an INFOCOM submission

Proceed with paper drafting and CloudLab implementation in parallel. Treat the
simulation section as frozen except for pre-registered physical calibration or
a clearly separated safeguard revision. Do not claim a completed INFOCOM-ready
system until all P0 blockers have physical evidence.

If the W3 rho 0.85 benefit survives both strong baselines on the physical
runtime with bounded overhead, the project has a credible INFOCOM systems
story. If it reverses, reposition the work as a simulation/mechanism and
boundary study or revise the scheduler before submission.
