# Step-04 Reproduction Commands

## Prerequisites

```
OS:       Windows 11
Compiler: g++ 15.2.0 (MSYS2 ucrt64, PATH includes C:\msys64\ucrt64\bin)
CMake:    4.2.3
Ninja:    1.13.2
Python:   3.x (for bootstrap CI only)
Workdir:  d:\desktop\Test
```

## 1. Build

```powershell
Set-Location "d:\desktop\Test"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Resulting binary: `build\simulator.exe`

## 2. Step-01 (W2, Tier-1)

Step-01 `main.cpp` writes to `artifacts/step-01-tier1/metrics_table.csv`.  
Before running, checkout the Step-01 version of `src/app/main.cpp` (the entry point was changed per step).

```powershell
# Build and run (Step-01 main.cpp must output W2 data)
.\build\simulator.exe "artifacts/step-01-tier1/metrics_table.csv"
```

**Configuration**: W2 (MMPP+Bimodal), methods={B1,B2,M0}, rho={0.50,0.70,0.85,0.92}, seeds={11,23,37,47,59}  
**Expected runs**: 4 rho × 3 methods × 5 seeds = 60

## 3. Step-02 (W1, Tier-2 Full Scan)

Step-02 `main.cpp` writes to `artifacts/step-02-tier2/metrics_scan.csv`.

```powershell
.\build\simulator.exe "artifacts/step-02-tier2/metrics_scan.csv"
```

**Configuration**: W1 (Poisson+Bimodal), methods={B0,B1,B2,M0}, rho={0.10..0.95 step=0.05}, seeds={11,23,37,47,59}  
**Expected runs**: 18 rho × 4 methods × 5 seeds = 360

## 4. Step-03 (W3, Tier-3 + Boundary)

Step-03 `main.cpp` writes to `artifacts/step-03-tier3/metrics_table.csv`.

```powershell
.\build\simulator.exe "artifacts/step-03-tier3/metrics_table.csv"
```

**Configuration**:
- Part 1: W3 (Poisson+Lognormal σ=1.0), methods={B1,B2,M0}, rho={0.50,0.70,0.85,0.92}, seeds={11,23,37,47,59} → 60 runs
- Part 2: W1 boundary rho=0.10, methods={B1,B2,M0}, 5 seeds → 15 runs
- Part 3: W3 boundary rho=0.10, methods={B1,B2,M0}, 5 seeds → 15 runs
- Part 4: W3 overload rho=0.95, methods={B1,B2,M0}, 5 seeds → 15 runs
- **Expected total**: 105 runs

## 5. Step-04 (Bootstrap CI + Manifest)

Bootstrap CI is computed via Python, not the C++ simulator.

```powershell
python -c "
import csv, json, statistics, random
random.seed(42)
B = 10000

def bootstrap_ci_median(data, n_boot=B):
    medians = []
    n = len(data)
    for _ in range(n_boot):
        sample = [data[random.randint(0, n-1)] for _ in range(n)]
        medians.append(statistics.median(sample))
    medians.sort()
    lo = medians[int(n_boot * 0.025)]
    hi = medians[int(n_boot * 0.975)]
    return statistics.median(data), lo, hi

def read_csv(path):
    rows = []
    with open(path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader: rows.append(row)
    return rows

s1 = read_csv('artifacts/step-01-tier1/metrics_table.csv')
s2 = read_csv('artifacts/step-02-tier2/metrics_scan.csv')
s3 = read_csv('artifacts/step-03-tier3/metrics_table.csv')
# ... (full manifest generation code in run_log.md)
"
```

The full Python script for `final_results_manifest.json` generation is recorded in `run_log.md`.

## 6. Frozen Parameter Cross-Reference

All three steps are driven by the same constants defined in `include/sim/common/constants.h`:

| Parameter | Code Constant | Value |
|-----------|---------------|-------|
| warmup | `WARMUP_REQUESTS` | 200000 |
| measurement | `MEASUREMENT_REQUESTS` | 1000000 |
| seeds | `SEEDS[]` | {11, 23, 37, 47, 59} |
| T_host | `T_host_us` | 2.1 us |
| T_net | `T_net_oneway_us` | 3.15 us |
| SYNC period | `SYNC_LOAD_PERIOD_US` | 10.0 us |
| M0 α | `M0_ALPHA` | 0.8 |
| M0 margin | `M0_T_MARGIN_US` | 1.5 us |
| M0 K_DST | `M0_K_DST` | 4 |
| M0 T_CHECK | `M0_T_CHECK_US` | 1.0 us |
| M0 budget | `M0_BUDGET` | 0.05 (effective 0.045) |
| B2 K_DST | `B2_K_DST` | 2 |
| B2 cooldown | `B2_T_COOLDOWN_US` | 2.0 us |
| B2 budget | `B2_BUDGET` | 0.05 |

## 7. Note on main.cpp Versioning

Each step overwrites `src/app/main.cpp` with step-specific experiment logic (which workloads/rho points to scan). The current workspace contains the Step-03 version. To reproduce earlier steps, restore the corresponding `main.cpp` from git history or manually modify the run configuration section.

The simulator core (`src/core/simulator.cpp`) and all algorithm headers are frozen since Step-01 iteration #14 (with Step-02/03 additive changes for B0 and W3). No algorithmic parameters have been changed between steps.
