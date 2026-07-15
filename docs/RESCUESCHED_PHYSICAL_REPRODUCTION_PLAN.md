# RescueSched Physical Reproduction Plan

This document defines the first physical-machine reproduction path for the
current RescueSched line. The local synthetic replay path now exists, but it is
not a networked RPC server and is not paper-valid physical evidence.

## Scope

Primary simulator target:

- `M1_RescueSched`
- Main simulator entry: `src/app/main.cpp`
- Main physical-ready CLI mode: `rescue-main`
- Minimal config: `config/rescuesched.yaml`
- Authoritative corrected simulation: `artifacts/step-21-corrected-full/`
- Local runtime contract: `docs/PHYSICAL_RUNTIME_CONTRACT.md`
- Final-run protocol: `docs/CLOUDLAB_PREREGISTRATION.md`

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

Output when an explicit file is supplied:

- `<output>/migration-cost.csv`

The physical preflight stores both the legacy condition-variable benchmark and
the pinned descriptor primitive under `physical-results/<run>/`. The pinned
runner selects topology-valid CPU pairs and records same-core, same-socket,
contention, and cross-NUMA distributions where the host exposes them.

The current benchmark reports:

- local descriptor queue push/pop cost
- cross-thread condition-variable handoff cost
- simulator calibration points for low/default/measured/high migration cost

Use `scripts/run_pinned_handoff_microbench.sh` for P50/P95/P99/P999, cycles,
context switches, affinity, and optional `perf stat` cache misses. These are
host-local primitive measurements. The final RPC runtime must still measure its
complete production handoff path.

## Trace Replay Plan

Current status: `physical::FrozenTrace` strictly loads simulator v2/v3 CSVs,
checks schema and trace identity, and feeds the pinned-worker synthetic runtime.
This closes the local loader blocker. Integration with a real RPC server/client
and NIC/RSS receive path is still required before physical validation.

Accepted trace schemas are the simulator's exact request-random v2 and
flow-affine v3 CSVs. The runtime records the embedded canonical trace SHA-256
and the input-file SHA-256 separately.

The future RPC outcome schema must include at least:

```text
request_id,start_us,finish_us,assigned_host,assigned_core,migration_count,rescue_count,deadline_miss
```

Replay phases:

1. Generate and freeze simulator v2/v3 input traces and their checksums.
2. Connect the existing loader to the real RPC request runtime without exposing
   hidden service demand to scheduling decisions.
3. Run simulator replay and physical replay using the same trace, seed, rho
   label, SLO, and service-time units.
4. Compare simulator, local synthetic, and physical RPC metrics with
   `scripts/analyze_simulator_physical_alignment.py`.

## Metrics Alignment

| Simulator CSV column | Physical measurement | Notes |
| --- | --- | --- |
| `P99_us` | server completion P99 | Client RTT is a separate physical field. |
| `P999_us` | server completion P99.9 | Require enough samples for stable tail estimate. |
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

Result-alignment review triggers:

- Directional conclusions for RescueSched versus `L1_WorkStealingPolling` and
  `M0_AltoThreshold` reverse on the same workload/rho point.
- Simulator and physical server-side tails differ materially after calibrated
  handoff, requiring a documented runtime, cache, NUMA, or network explanation.
- RescueSched improves tail latency only by increasing useless or harmful
  migrations beyond the simulator-boundary cases.

## Implementation status before physical claim

| Item | Status |
| --- | --- |
| Strict frozen trace loader | Implemented for v2/v3 CSV |
| Pinned FIFO workers and queued-only migration | Implemented in local synthetic runtime |
| Four policies in one runtime | Implemented for local alignment validation |
| Completion-updated method EWMA | Implemented and unit tested |
| Request/decision/migration logs and manifests | Implemented locally |
| Pinned handoff distribution runner | Implemented; cross-NUMA depends on host topology |
| Evidence-separated alignment script | Implemented |
| Frozen CloudLab protocol | Documented; execution pending |
| Real RPC server/client and NIC/RSS path | `[IMPLEMENTATION DETAIL REQUIRED]` |
| Client RTT and NIC/packet logs | `[IMPLEMENTATION DETAIL REQUIRED]` |
| Final CloudLab paired results | `[PHYSICAL RESULT REQUIRED]` |
