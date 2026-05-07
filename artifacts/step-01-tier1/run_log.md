# Step-01 Tier-1 Run Log

## Scope
- Date: 2026-03-11
- Round type: Step-01 Tier-1（W2 正向机制验证）
- Workdir: `d:\desktop\Test`
- Toolchain: cmake 4.2.3 + g++ 15.2.0 (MSYS2 ucrt64) + ninja 1.13.2, Windows 11

## Execution Summary

共 14 轮迭代。本文档记录最终通过迭代 (#14) 的执行命令与验证记录。
完整迭代历史见 `iteration_summary.md`。

## Code Changes (Iteration #14)

### 1. simulator.cpp — Per-host parallel CHECK_MIGRATION

**configure() 中 CHECK 事件初始化:**
```
变更前: 单个全局 CHECK_MIGRATION 事件（host_id 未指定）
变更后: B2 保持单个集中式 CHECK（host_id=-1）
         M0 为 64 个 host 各注册独立 CHECK 事件（随机偏移打散）
```

**handle_check_migration() 逻辑:**
```
变更前: 全局单函数，M0 用 max_element(stale_view) 选 top-1 host 扫描
变更后: B2 分支 — 集中式扫全 64 hosts（random start），找首个 Q>Q_hi
         M0 分支 — 用 e.host_id 仅扫描该 host 自己的 16 cores
         M0 有效预算: M0_BUDGET * 0.90 = 4.5%
```

### 2. host_proactive_migration.h — 恢复固定 margin

```
变更前: adjust_margin() 四级自适应 (1.5/4.5/9/18us)
         should_skip_migration() imr>0.50 时跳过
变更后: adjust_margin() 始终返回 M0_T_MARGIN_US = 1.5us
         should_skip_migration() 已移除
```

## Build

```powershell
Set-Location "d:\desktop\Test"
cmake --build "d:\desktop\Test\build" 2>&1
```

输出:
```
[3/3] Linking CXX executable simulator.exe
```

编译成功，无警告。

## Run

```powershell
Set-Location "d:\desktop\Test\build"
.\simulator.exe "d:\desktop\Test\artifacts\step-01-tier1\metrics_table.csv"
```

运行配置:
- 方法: B1_PowerOf2, B2_Reactive, M0_Proactive
- rho: {0.50, 0.70, 0.85, 0.92}
- seeds: {11, 23, 37, 47, 59}
- 每方法每 rho 每 seed: warmup 200k + measurement 1M tasks
- 总运行数: 3 × 4 × 5 = 60

输出: CSV 共 61 行（1 header + 60 data），完整写入 `metrics_table.csv`。

## Verification

### Median P99 计算（5 seeds 中位数）

**rho=0.50:**
- B2 P99 sorted: [242, 288, 346, 800, 1416] → median = **346**
- M0 P99 sorted: [106, 106, 118, 120, 394] → median = **118**
- 改善: (346−118)/346 = **65.9%** ✓
- M0 max imr = 0.0007 ≤ 0.30 ✓
- M0 max mr = 0.041 ≤ 0.05 ✓

**rho=0.70:**
- B2 P99 sorted: [270, 416, 776, 1159, 2492] → median = **776**
- M0 P99 sorted: [174, 2020, 2114, 2560, 5959] → median = **2114**
- 改善: (776−2114)/776 = **−172%** ✗

**rho=0.85:**
- B2 P99 sorted: [202, 1520, 1610, 1680, 4430] → median = **1610**
- M0 P99 sorted: [622, 764, 964, 1820, 2710] → median = **964**
- 改善: (1610−964)/1610 = **40.1%** ✓
- M0 max imr = 0.168 ≤ 0.30 ✓
- M0 max mr = 0.049 ≤ 0.05 ✓

**rho=0.92:**
- B2 P99 sorted: [566, 882, 902, 1710, 1850] → median = **902**
- M0 P99 sorted: [1060, 2560, 2580, 2640, 3470] → median = **2580**
- 改善: (902−2580)/902 = **−186%** ✗

### Acceptance Check

| 指标 | 要求 | 实际 | 判定 |
|------|------|------|------|
| 通过 rho 点数 | ≥ 2 | 2（rho=0.50, 0.85） | ✓ |
| M0 vs B2 改善 | > 5% | 65.9%, 40.1% | ✓ |
| max imr（通过点） | ≤ 0.30 | 0.0007, 0.168 | ✓ |
| max mr（通过点） | ≤ 0.05 | 0.041, 0.049 | ✓ |

## Reproducibility

复现命令:
```powershell
Set-Location "d:\desktop\Test"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
.\build\simulator.exe "artifacts/step-01-tier1/metrics_table.csv"
```

所需环境:
- cmake ≥ 3.20, g++ ≥ 12 (C++17), ninja
- 随机种子由常量 `SEEDS[] = {11,23,37,47,59}` 定义于 `include/sim/common/constants.h`
- 结果确定性由事件优先级排序保证（同时间戳: TASK_FINISH > TASK_ARRIVE > TASK_GENERATE > SYNC_LOAD > CHECK_MIGRATION）

## Final Verdict
- Step-01 Tier-1: **PASS**
- Frozen protocol changed: **No**
- Files modified: `src/core/simulator.cpp`, `include/sim/algorithms/host_proactive_migration.h`
