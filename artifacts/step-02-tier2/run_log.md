# Step-02 Run Log

## Environment
- OS: Windows 11
- Compiler: g++ 15.2.0 (MSYS2 ucrt64)
- CMake: 4.2.3
- Build system: Ninja 1.13.2
- Build flags: `-O2`

## Code Changes

### New: `B0_IDEAL_CFCFS` method (types.h, simulator.h, simulator.cpp)
- Added `B0_IDEAL_CFCFS` to `MethodType` enum
- Added `WorkloadType` enum (`W1_POISSON_BIMODAL`, `W2_MMPP_BIMODAL`)
- Global FIFO `TaskQueue global_queue_` in Simulator for B0
- `try_b0_pull()`: pop from global queue → schedule TASK_ARRIVE to idle core with T_net delay
- `find_b0_idle_host()`: random-start scan for host with idle core
- B0 skips SYNC_LOAD and CHECK_MIGRATION events

### Updated: `configure()` (simulator.cpp)
- Accepts `WorkloadType` parameter (default W2 for backward compatibility)
- W1: uses `PoissonArrival` instead of `MMPPArrival`, no hot node dispatch
- W2: unchanged (MMPP + hot node dispatch)
- B0: no IScheduler, no SYNC/CHECK events

### Updated: `handle_task_generate()` (simulator.cpp)
- B0 path: push to global queue + try_b0_pull()
- W1 path: use poisson_->next_interarrival() instead of mmpp_

### Updated: `handle_task_finish()` (simulator.cpp)
- B0 path: if local queue empty, try_b0_pull(host_id)

### Updated: `main.cpp`
- Step-02 scan: rho=0.10..0.95 step=0.05, B0/B1/B2/M0, seeds={11,23,37,47,59}
- Output: `artifacts/step-02-tier2/metrics_scan.csv`

### Minor: `core.h`
- Added `TaskQueue::clear()` for Simulator reset

## Build

```
cmake --build "d:\desktop\Test\build"
# [4/4] Linking CXX executable simulator.exe — SUCCESS
```

## Execution

```
.\build\simulator.exe "artifacts/step-02-tier2/metrics_scan.csv"
# 360 runs completed
# CSV: 361 lines (header + 360 data rows)
```

## Verification

### Line count check
```powershell
(Get-Content "artifacts/step-02-tier2/metrics_scan.csv" | Measure-Object -Line).Lines
# Result: 361 ✓
```

### Run coverage
- 18 rho points × 4 methods × 5 seeds = 360 runs ✓
- All methods present: B0_IdealCFCFS, B1_PowerOf2, B2_Reactive, M0_Proactive ✓
- All rho points: 0.10, 0.15, ..., 0.95 ✓
- All seeds: 11, 23, 37, 47, 59 ✓

### Low-load sanity check
- rho=0.10: all methods P99=106us (identical) ✓
- Expected floor: 100us/1.0 + 2.1 + 3.15 ≈ 105.25us → bucket center 105.5us rounds to 106 ✓

### Migration metrics boundary check
- M0 max mr = 0.048 (≤ 0.05) ✓
- M0 max imr = 0.215 at rho=0.90 (≤ 0.30) ✓
- B2 imr exceeds 0.30 only at rho=0.95 (0.403) — flagged as side-effect uncontrolled

### Reproducibility
All runs use fixed seeds {11,23,37,47,59}. Re-running the same binary produces identical CSV.
