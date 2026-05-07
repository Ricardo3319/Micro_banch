# Step-04b Sensitivity — Run Log

## Execution Environment
- OS: Windows 11
- Compiler: g++ 15.2.0 (MSYS2 ucrt64)
- Build: cmake 4.2.3 + ninja 1.13.2
- Command: `.\build\simulator.exe sensitivity`

## Timeline
- Build: `cmake --build d:\desktop\Test\build` — OK (0 errors)
- Sensitivity scan start: 2026-03-12
- Total runs: 125 (120 M0 + 5 B2 baseline)
- All runs completed successfully (total_finished = 1,000,000 each)

## Parameter Matrix

| 参数 | 扫描值 | Runs |
|------|--------|------|
| B2 baseline | — | 5 |
| α | {0.5, 0.6, 0.7, 0.8, 0.9, 1.0} | 30 |
| T_margin | {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 5.0} | 35 |
| K_DST | {1, 2, 4, 8, 16} | 25 |
| T_CHECK | {0.5, 1.0, 2.0, 5.0, 10.0} | 25 |
| **Total** | | **120 + 5 = 125** |

## Fixed Parameters (Frozen)
- Cluster: 64 hosts × 16 cores, C=1.0 (homogeneous)
- Workload: W2 (MMPP+Bimodal), ρ=0.85
- warmup=200,000, measurement=1,000,000
- Seeds: {11, 23, 37, 47, 59}
- T_host=2.1 us, T_net=3.15 us, SYNC=10.0 us
- M0 defaults (when not being swept): α=0.8, T_margin=1.5, K_DST=4, T_CHECK=1.0

## Code Changes for This Step
1. Added `M0Config` struct to `types.h` with runtime-overridable M0 parameters
2. `ProactiveMigrationScheduler::check_core()` reads runtime α, margin, k_dst from config
3. `Simulator::configure()` accepts optional `M0Config` parameter
4. CHECK_MIGRATION interval uses `m0_config_.t_check_us` instead of compile-time constant
5. `main.cpp` — added `run_sensitivity()` function for parameter sweep

## Verification
- All 125 rows present in `sensitivity_scan.csv`
- total_finished = 1,000,000 for every run
- Default parameter runs (α=0.8, T_margin=1.5, K_DST=4, T_CHECK=1.0) match Step-04 frozen values
- No modification to Step-01~04 artifacts
