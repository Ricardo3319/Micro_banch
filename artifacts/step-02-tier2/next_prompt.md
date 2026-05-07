你现在执行 Step-03（仅 Tier-3，W3 重尾泛化 + 边界负例）。

必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-02-tier2/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## 项目背景

我正在用 C++17 构建一个离散事件仿真器（DES），对比四种微秒级调度算法。工作空间在 `Test`，构建工具链为 cmake 4.2.3 + g++ 15.2.0 (MSYS2 ucrt64) + ninja 1.13.2，Windows 11。

## Step-01 & Step-02 关键结论

### Step-01（W2: MMPP + Bimodal）— PASS
- rho=0.50: M0 median P99=118us vs B2=346us, 改善 +65.9%
- rho=0.85: M0 median P99=964us vs B2=1610us, 改善 +40.1%
- 关键架构: M0 per-host 并行 CHECK_MIGRATION (64 独立事件)

### Step-02（W1: Poisson + Bimodal）— PASS
- M0 在 W1 下无显著 P99 优势（最大 +1.8% at rho=0.70）
- M0 在 P999 rho=0.70 显示 +15.5% 优势（极端尾部效应）
- B0 (Ideal-cFCFS) 在 rho≥0.87 因 per-pull 通信开销退化
- M0 副作用可控: mr≤0.048, imr≤0.215 (all rho)
- 结论: M0 优势依赖突发场景 (W2)，W1 下各方法趋近

## 本步目标

补齐 W3（Poisson + Lognormal）重尾泛化与边界负例场景。

## 执行内容

1. **实现 W3 工作负载**: Poisson + Lognormal 服务时间（E[S]=24us, σ=1.0, μ=ln(24)−0.5σ²≈2.678）
2. **运行 W3 代表点**: B1/B2/M0, rho={0.50, 0.70, 0.85, 0.92}, seeds={11,23,37,47,59}
3. **边界场景**: 低负载极短任务占比高场景（可选 W1 rho=0.10 + 100% 短任务变体）
4. **输出指标**:
   - P99/P99.9 对比（W3 vs W1/W2）
   - migration_rate + imr vs rho
   - 至少 1 个可信负例 + 退化机制说明
5. **重尾对比图**: Lognormal 长尾如何影响各方法表现

## 冻结参数（不可修改）

- 集群: 64 hosts × 16 cores = 1024 cores, 均质 C=1.0
- T_host=2.1us, T_net=3.15us, SYNC_LOAD=10us
- M0: α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us, budget=5% (effective 4.5%)
- B2: K_DST=2, cooldown=2us, budget=5%
- W3: Lognormal σ=1.0, μ=ln(24)−0.5≈2.678, E[S]=24us
- 统计: warmup=200k, measurement=1M, seeds={11,23,37,47,59}

## 关键文件

- 主仿真引擎: `src/core/simulator.cpp` + `include/sim/core/simulator.h`
- M0 算法: `include/sim/algorithms/host_proactive_migration.h`
- B2 算法: `include/sim/algorithms/host_reactive_migration.h`
- B1 算法: `include/sim/algorithms/host_power_of_k.h`
- 工作负载: `include/sim/workloads/generators.h`
- 常量: `include/sim/common/constants.h`
- 入口: `src/app/main.cpp`
- 构建: `cmake --build "d:\desktop\Test\build"`

## 验收标准

- 完成 W3 代表点运行（4 rho × 3 methods × 5 seeds = 60 runs）
- 至少 1 个可信负例且机制解释完整
- 重尾对比结论与 W1/W2 方向自洽

## 输出并落盘到 `artifacts/step-03-tier3/`

- `summary.md`
- `metrics_table.csv`
- `boundary_case.md`
- `run_log.md`
- `next_prompt.md`
