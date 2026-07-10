# RescueSched Physical Reproduction Plan

This document defines the first physical-machine reproduction path for the
current RescueSched line. It is a plan, not evidence that the physical replay
path already exists.

## Scope

Primary simulator target:

- `M1_RescueSched`
- Main simulator entry: `src/app/main.cpp`
- Main physical-ready CLI mode: `rescue-main`
- Minimal config: `config/rescuesched.yaml`
- Current source commit recorded when this plan was written: `4f8bed8`
- Worktree state at plan creation: dirty, because this change set is not yet
  committed.

## CloudLab Build Plan

Recommended baseline node:

- Ubuntu 22.04 or 24.04 bare-metal node.
- 8 or more hardware cores.
- CPU frequency governor fixed to `performance` when allowed by the profile.
- NTP or chrony enabled for cross-node trace alignment if multi-node traces are
  collected.

Build and gate:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

If Ninja is unavailable, use the platform default generator:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Record in every physical run manifest:

- `git rev-parse HEAD`
- `git status --short`
- `cmake --version`
- compiler path and version
- `build/CMakeCache.txt`
- host model, CPU count, kernel version, governor, and NUMA topology

## Simulator Reproduction Commands

Smoke gate:

```bash
./build/simulator --mode rescue-smoke
```

Small configurable RescueSched run:

```bash
./build/simulator --config config/rescuesched.yaml
```

Explicit single-point run:

```bash
./build/simulator \
  --mode rescue-main \
  --workload W3 \
  --rho 0.85 \
  --seed 11 \
  --out-dir artifacts/cloudlab-$(hostname)
```

Full default RescueSched main sweep:

```bash
./build/simulator --mode rescue-main --out-dir artifacts/cloudlab-$(hostname)
```

## Microbench Plan

Use the built-in migration-cost microbenchmark before trace replay:

```bash
./build/simulator \
  --mode rescue-cost-microbench \
  --out-dir artifacts/cloudlab-$(hostname)
```

Output:

- `artifacts/<run>/step-17-rescuesched-closure/migration_cost_microbench.csv`

The current benchmark reports:

- local descriptor queue push/pop cost
- cross-thread condition-variable handoff cost
- simulator calibration points for low/default/measured/high migration cost

Physical migration-cost calibration should use this as the initial upper-bound
measurement, then replace it with a pinned-thread or production-runtime
microbench if the physical implementation has a different handoff path.

## Trace Replay Plan

Current status: the repository does not yet contain a physical trace replay
loader. This is a required implementation gap before physical validation can be
claimed.

Expected trace schema:

```text
arrival_us,request_id,tenant_or_flow,service_us,slo_us,workload_class,source_host,source_core
```

Optional physical outcome schema:

```text
request_id,start_us,finish_us,assigned_host,assigned_core,migration_count,rescue_count,deadline_miss
```

Replay phases:

1. Collect physical RPC or synthetic-service traces with stable timestamps.
2. Convert traces into the expected CSV schema.
3. Add a loader that maps trace rows to simulator task arrivals without
   changing RescueSched policy logic.
4. Run simulator replay and physical replay using the same trace, seed, rho
   label, SLO, and service-time units.
5. Compare simulator and physical metrics with the alignment table below.

## Metrics Alignment

| Simulator CSV column | Physical measurement | Notes |
| --- | --- | --- |
| `P99_us` | request latency P99 | Same completed-request set and warmup policy. |
| `P999_us` | request latency P99.9 | Require enough samples for stable tail estimate. |
| `slo_violation_rate` | deadline misses / completed requests | SLO definition must be identical. |
| `total_finished` | completed request count | Exclude dropped requests unless simulator models drops. |
| `total_generated` | submitted request count | Trace input count. |
| `migration_rate` | migrations / generated requests | Physical implementation must log successful migrations. |
| `rescue_attempt_count` | rescue checks/candidates inspected | Requires policy instrumentation. |
| `rescue_success_count` | committed rescue migrations | Must distinguish rescue from ordinary work stealing. |
| `beneficial_migration_ratio` | rescued requests that avoid miss | Requires counterfactual or bounded estimator; otherwise mark unavailable. |
| `useless_migration_ratio` | migrations that do not improve deadline outcome | Same limitation as beneficial ratio. |
| `rescue_per_migration` | rescue successes / all migrations | Directly measurable if both counters exist. |

## Failure Criteria

Build and gate failures:

- CMake configure or build fails.
- `ctest --test-dir build --output-on-failure` fails.
- `rescue-smoke` does not print `RescueSched smoke status: PASS`.

Microbench failures:

- Cross-thread handoff measurements vary by more than 25 percent across three
  repeated runs without an explained host-load cause.
- Measured handoff cost is higher than the simulator stress point used in the
  sensitivity sweep, unless a new sweep is generated with the measured cost.

Trace replay failures:

- Simulator and physical replay do not use the same input trace.
- Tail latency rank set differs because warmup, dropped requests, or timeout
  handling differ.
- Physical logs cannot reconstruct rescue success, migration count, and SLO
  violation rate.

Result-alignment failure:

- Directional conclusions for RescueSched versus `L1_WorkStealing` and
  `M0_IntraHostProactive` reverse on the same workload/rho point.
- Simulator median P99/P999 and physical median P99/P999 differ by more than
  20 percent after microbench-calibrated migration cost, unless explained by a
  documented workload or implementation difference.
- RescueSched improves tail latency only by increasing useless or harmful
  migrations beyond the simulator-boundary cases.

## Required Additions Before Physical Claim

- Physical trace loader.
- Runtime instrumentation for rescue attempts, rescue successes, target-safety
  rejects, migrations, and deadline misses.
- Run manifest generator that records commit, build, host, command, and output
  CSV paths.
- Physical baseline implementations or wrappers for `L1_WorkStealing` and
  `M0_IntraHostProactive`.
- A comparison script that consumes simulator CSVs and physical CSVs into one
  metrics table.
