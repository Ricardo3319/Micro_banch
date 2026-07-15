# Local Simulation Diagnostics Before CloudLab

## Purpose

Step-22 adds post-freeze simulator diagnostics that can be completed before a
physical RPC runtime is available. It does not revise the Step-21 corrected
paper gate and must not overwrite any Step-20 or Step-21 artifact.

The diagnostic work addresses four pre-CloudLab questions:

1. Does the queued-only descriptor migration implementation preserve queue,
   reservation, completion, and trace-sharing invariants?
2. How do the four frozen anchor points respond to one-factor-at-a-time changes
   in placement, handoff delay, bounded search, migration budget, epsilon, and
   EWMA adaptation?
3. What estimator errors, candidate/target operation counts, migration cohorts,
   and queue-work conditions accompany the observed server-side outcomes?
4. Which simulator assumptions require physical measurement rather than more
   local tuning?

## Implemented Diagnostics

- Deterministic request-random trace regression preserving
  `rescuesched-trace-v2` identity.
- Opt-in `rescuesched-trace-v3` flow-affine placement with stable flow-to-core
  hashing and optional Zipf flow popularity.
- Queue entries inspected, accepted candidates, targets evaluated, and source
  or remote-feasibility revalidation rejects.
- Migrated short/long and burst/non-burst cohorts, source/destination work at
  commit, and migrated/non-migrated completion outcomes.
- Method-keyed estimator signed error, MAE, RMSE, cold starts, and class MAE.
- Per-operation control-path counts over the measurement interval and
  configured accounting-only cost totals.
- Runtime guards for non-positive periods/bounds, invalid EWMA alpha, negative
  handoff/epsilon values, negative accounting costs, and non-finite trace or
  policy parameters.
- Tests that running descriptors cannot migrate, destination insertion remains
  append-tail, reservations are released, and measurement requests are neither
  lost nor completed twice.

## Diagnostic Matrix

Run:

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
bash scripts/run_local_simulation_diagnostics.sh smoke build-local
```

Available tiers are `smoke`, `pilot`, and `full`. Pilot and full use only the
frozen development seeds `11,23,37,47,59`; they do not reopen holdout tuning.
Every profile covers W3 rho 0.70, 0.85, and 0.90 plus W2 rho 0.85 and compares
the four primary methods on the same trace within each workload/rho/seed point.

The optional fourth runner argument selects a comma-separated profile subset
for targeted confirmation while retaining the same validation and provenance
path. A subset must include `baseline`; omitting the argument runs all profiles.
For example:

```bash
bash scripts/run_local_simulation_diagnostics.sh full build-local \
  artifacts/step-22-local-simulation-diagnostics/full-ewma-confirmation \
  baseline,ewma-0.01
```

The matrix is one-factor-at-a-time. It includes the corrected default plus:

- flow-affine uniform and Zipf placement models;
- handoff delays of 0, 2, and 5 us;
- check periods of 2 and 5 us;
- scan depths of 32 and 128;
- candidate bounds of 8 and 32;
- target bounds of 2 and 8;
- per-check budgets of 2 and 4;
- epsilon values of 0 and 5 us;
- EWMA alpha values of 0.01 and 0.20;
- a normalized 1-us-per-operation accounting profile.

The normalized control profile is intentionally synthetic. Its configured
totals expose operation volume only; they do not advance simulated time and
are not physical overhead measurements.

## Outputs

The default output is:

```text
artifacts/step-22-local-simulation-diagnostics/<tier>/
```

It contains raw extended-schema CSVs, logs, `profiles.csv`, `summary.csv`,
`paired_comparisons.csv`, `sensitivity_vs_default.csv`, a manifest, and SHA-256
checksums covering raw outputs and logs. `RUN_METADATA.txt` freezes the source revision, launch-time dirty
state, simulator binary hash, and analysis-script hashes. `RUN_STATUS.txt`
remains `INCOMPLETE` after an interrupted or failed run and changes to `PASS`
only after validation and summary generation. Both files are checksummed. No
figures are generated.

The analysis validates complete measurement cohorts, all-method trace sharing,
placement-specific trace identity, bounded target evaluation, migration cohort
coverage, estimator direction/class coverage, configured cost arithmetic, and
the actual CSV parameters for every OFAT profile before writing summaries. Its bootstrap
intervals are descriptive and are not a rerun of the corrected gate.

## Full-Size EWMA Confirmation

The development-seed pilot selected `ewma-0.01` for a targeted full-size
confirmation, alongside the corrected default. The resulting artifact is
`artifacts/step-22-local-simulation-diagnostics/full-ewma-confirmation-20260715/`.
It covers seeds `11,23,37,47,59`, 200,000 warmup requests, and 1,000,000
measurement requests at W2 rho 0.85 and W3 rho 0.70, 0.85, and 0.90.

The extended-schema default rows were compared with the matching Step-21 rows.
All 80 rows matched across all 90 shared columns, with zero differences. The
Step-22 instrumentation therefore preserves the frozen default outcomes at the
four diagnostic anchors.

For RescueSched, changing the shared method-keyed EWMA alpha from 0.05 to 0.01
produced the following paired development-seed sensitivity. Negative miss-rate
change means fewer deadline violations; the intervals are descriptive paired
bootstrap intervals from `sensitivity_vs_default.csv`.

| Anchor | Mean miss-rate change | 95% descriptive interval | Median P99 ratio | Median P999 ratio | Migrated-work-rate ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| W2 rho 0.85 | 0 | [0, 0] | 1.0000 | 1.0000 | 1.0000 |
| W3 rho 0.70 | -0.0001210 | [-0.0001784, -0.0000718] | 0.9950 | 1.0000 | 0.9927 |
| W3 rho 0.85 | -0.0011362 | [-0.0014898, -0.0007826] | 0.9953 | 0.9948 | 0.9914 |
| W3 rho 0.90 | -0.0018180 | [-0.0023400, -0.0014108] | 0.9958 | 1.0046 | 0.9891 |

This is not a RescueSched-only improvement. The alpha controls the estimator
shared by the deployable-estimator methods, and ALTO's mean miss rate also
improved at every W3 anchor. RescueSched's mean miss-rate advantage over ALTO
decreased slightly at all three W3 points. The W3 rho 0.70 negative result
against polling remains, the W2 tail regression is unchanged, and W3 rho 0.90
has a small median P999 regression under alpha 0.01. Consequently, alpha 0.05
remains the frozen default, Step-21 remains authoritative, and alpha 0.01 is
only an estimator-calibration candidate requiring preregistration and
independent evidence.

## Evidence Boundary

- `artifacts/step-21-corrected-full/` remains the authoritative corrected
  simulation evidence for the current paper claim.
- Step-22 may support implementation debugging, sensitivity planning, and
  physical experiment design. It must not silently replace Step-21 numbers.
- Flow-affine placement is a simulator model. It is not evidence about a NIC,
  RSS indirection table, transport state, or measured flow distribution.
- Configured handoff delay is simulated elapsed time, not measured descriptor
  migration overhead.
- Control-path costs are accounting-only. CPU cycles, cache effects, lock
  contention, NUMA effects, and packet processing remain
  `[PHYSICAL RESULT REQUIRED]`.
- BMR, UMR, and target-harm counters remain counterfactual simulator
  diagnostics and must not be described as physically observed facts.

## Work Remaining Before CloudLab

1. Freeze the physical trace format and replay checks for identical arrivals,
   methods, deadlines, and placement inputs across all methods.
2. Implement the four primary methods in one pinned-worker runtime with a
   shared handoff path and queued-only ownership transfer.
3. Define the legal descriptor ownership protocol, memory ordering, queue
   insertion point, in-flight state, and completion routing.
4. Add decision-path cycle counters, handoff distributions, polling cycles,
   cache misses, NUMA placement, CPU utilization, and separate server-side and
   end-to-end latency logs.
5. Measure physical costs first, then preregister any simulator calibration
   range. Do not select a range from final physical outcome traces.
6. Preserve the four anchors and negative boundaries in the CloudLab plan.
