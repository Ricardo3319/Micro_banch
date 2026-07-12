# Next Phase after Corrected Evaluation

**Decision date:** 2026-07-12
**Evidence version:** `rescuesched-corrected-eval-v1`

## Decision

Proceed to paper drafting and a focused CloudLab validation. The corrected
simulation supports a narrow claim: at moderate and high offered load,
request-specific deadline-feasibility selection reduces deadline violations
relative to polling work stealing and an ALTO-style threshold policy while
moving less work. It does not support universal latency improvement.

The paper must report the negative boundaries. At W3 rho 0.70, polling work
stealing has fewer deadline violations. In W2, RescueSched reduces deadline
violations but substantially worsens unconditional P99/P999 latency. These
results motivate workload- and tail-aware safeguards; they are not evidence to
discard or silently tune away.

## Paper drafting package

1. Use the paper contract as the claim authority and the full corrected
   manifest as the result authority.
2. Lead with deadline violation / SLO goodput. Report migrated work as the
   efficiency cost. Treat P99/P999 as explicit secondary outcomes.
3. Use W3 rho 0.85 and 0.90 for the main paired result, W3 rho 0.70 as the
   low-load counterexample, and W2 rho 0.85 as the burst/tail boundary.
4. Describe polling work stealing and ALTO-style threshold migration as paid,
   budget-matched strong baselines. Do not use legacy AQB/DQB evidence.
5. Keep the novelty statement limited to request-specific outcome-change
   selection. Do not claim that oracle knowledge, target safety, or real-machine
   feasibility has already been demonstrated.

## CloudLab validation on c6225 25G

The first machine experiment should answer mechanism-validity questions, not
repeat the entire simulator matrix.

1. Measure the complete decision path, descriptor handoff, queue insertion,
   cache/NUMA disruption, and polling cost. Feed the measured handoff-cost
   distribution back into the simulator; do not use only the current scalar
   cost.
2. Reproduce four anchor points with frozen request traces where practical:
   W3 rho 0.85 and 0.90, W3 rho 0.70, and W2 rho 0.85. Run at least ten paired
   seeds or trace repetitions for each primary method.
3. Report server-side deadline violation, goodput, P50/P99/P999, migrated
   requests and work, CPU utilization, polling/decision CPU cycles, cache
   misses, and cross-core/NUMA movement. End-to-end RTT should be reported
   separately from the server-side deadline budget.
4. Calibrate offered load from measured service plus host overhead and verify
   it from arrivals and completed work. Record NIC/RSS configuration, CPU
   affinity, NUMA placement, frequency policy, kernel, compiler, firmware, and
   commit hash.

## Go/no-go checks before expanding the matrix

- The measured implementation must preserve the local-miss/remote-meet
  decision semantics without observing the current request's service time.
- Paid handoff and control-plane CPU cost must be small enough that W3 rho 0.85
  retains a directional deadline-violation benefit over both strong baselines.
- If W2 tail amplification reproduces, either add a pre-registered tail-aware
  safeguard and rerun all affected comparisons, or retain it as a disclosed
  limitation. Do not tune only on the final holdout traces.
- If the W3 benefit does not reproduce, narrow the paper to the simulation
  mechanism study or revise the system design before claiming deployability.

## Immediate writing order

Draft the evaluation methodology and results first from the versioned artifacts,
then the design, introduction, and abstract. This minimizes claim drift while
the CloudLab implementation is being prepared. Every numerical statement in
the draft should link internally to a row or paired comparison in the corrected
full artifact.
