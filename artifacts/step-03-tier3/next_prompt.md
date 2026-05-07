你现在执行 Step-04（仅封板，不新增算法）。

必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-03-tier3/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## 项目背景

我正在用 C++17 构建一个离散事件仿真器（DES），对比四种微秒级调度算法。工作空间在 `Test`，构建工具链为 cmake 4.2.3 + g++ 15.2.0 (MSYS2 ucrt64) + ninja 1.13.2，Windows 11。

## Step-01/02/03 关键结论

### Step-01（W2: MMPP + Bimodal）— PASS
- rho=0.50: M0 median P99=118us vs B2=346us, 改善 +65.9%
- rho=0.85: M0 median P99=964us vs B2=1610us, 改善 +40.1%
- 关键架构: M0 per-host 并行 CHECK_MIGRATION (64 独立事件)

### Step-02（W1: Poisson + Bimodal）— PASS
- M0 在 W1 下无显著 P99 优势（最大 +1.8% at rho=0.70）
- M0 在 P999 rho=0.70 显示 +15.5% 优势（极端尾部效应）
- B0 (Ideal-cFCFS) 在 rho≥0.87 因 per-pull 通信开销退化
- 结论: M0 优势依赖突发场景 (W2)，W1 下各方法趋近

### Step-03（W3: Poisson + Lognormal）— PASS
- M0 在 W3 rho=0.85: P99 改善 +10.0%, P999 改善 +12.7% (vs B2, 5/5 seeds)
- M0 在 W3 rho=0.92: P99 改善 +6.2%, P999 改善 +9.7% (vs B2, 4/5 seeds)
- M0 优势三级递进: W1(0%) < W3(+10%) < W2(+40%)
- 负例: W3 rho=0.95 M0 P999 退化 −23.5% (5/5 seeds), 机制=全局饱和+迁移开销浪费
- M0 副作用可控: mr≤0.045, imr≤0.181 (all 4 W3 rep points)

## 本步目标

固定统计结果与复现产物。

## 执行内容

1. **Bootstrap CI 计算**: 对关键对比点（rho=0.85/0.92 W3, rho=0.85 W2）计算 P99 中位数的 95% bootstrap CI（固定重采样次数）
2. **复现命令清单**: 产出可一键重建所有步骤关键图表的命令清单
3. **结果清单**: `final_results_manifest.json` 包含全部配置点的最终结果摘要
4. **统一口径复查**: 核对 warmup=200k, measurement=1M, seeds={11,23,37,47,59} 在所有步骤一致

## 冻结参数（不可修改）

- 集群: 64 hosts × 16 cores = 1024 cores, 均质 C=1.0
- T_host=2.1us, T_net=3.15us, SYNC_LOAD=10us
- M0: α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us, budget=5% (effective 4.5%)
- B2: K_DST=2, cooldown=2us, budget=5%
- 统计: warmup=200k, measurement=1M, seeds={11,23,37,47,59}

## 关键文件

- 主仿真引擎: `src/core/simulator.cpp` + `include/sim/core/simulator.h`
- 入口: `src/app/main.cpp`
- Step-01 结果: `artifacts/step-01-tier1/metrics_table.csv`
- Step-02 结果: `artifacts/step-02-tier2/metrics_scan.csv`
- Step-03 结果: `artifacts/step-03-tier3/metrics_table.csv`
- 构建: `cmake --build "d:\desktop\Test\build"`

## 验收标准

- 全部 bootstrap CI 完成且关键对比点 CI 不重叠
- `final_results_manifest.json` 覆盖 Step-01/02/03 全部配置点
- 复现命令可从原始 seed/config 一键重跑

## 输出并落盘到 `artifacts/step-04-freeze/`

- `summary.md`
- `final_results_manifest.json`
- `repro_commands.md`
- `run_log.md`
- `next_prompt.md`
