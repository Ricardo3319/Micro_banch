你现在执行 Step-02（仅 Tier-2，W1 全扫描）。

必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-01-tier1/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## 项目背景

我正在用 C++17 构建一个离散事件仿真器（DES），对比四种微秒级调度算法。工作空间在 `Test`，构建工具链为 cmake 4.2.3 + g++ 15.2.0 (MSYS2 ucrt64) + ninja 1.13.2，Windows 11。

## Step-01 关键结论

Step-01（W2: MMPP + Bimodal）已通过验收（迭代 #14）：
- **rho=0.50**: M0 median P99=118us vs B2=346us, 改善 65.9%, imr≤0.001, mr≤0.041
- **rho=0.85**: M0 median P99=964us vs B2=1610us, 改善 40.1%, imr≤0.168, mr≤0.049
- 关键架构: M0 使用 per-host 并行 CHECK_MIGRATION（64 个独立事件），B2 保持集中式 CHECK
- rho=0.70/0.92 退化（per-host 预算竞争/极端饱和），有清晰机制解释

## 本步目标

在 W1（Poisson + Bimodal, 无 MMPP 突发）下形成主结果与公平性证据。

## 执行内容

1. **实现 B0 (Ideal-cFCFS) 基线**：全局单队列理想调度，作为性能下界参考。
2. **实现 W1 工作负载**: Poisson 到达 + Bimodal 服务时间（80%×5us + 20%×100us, E[S]=24us），无 MMPP 突发。
3. **运行 W1 全扫描**: B0/B1/B2/M0, rho=0.10..0.95 step=0.05, seeds={11,23,37,47,59}。
4. **输出指标**:
   - P99/P99.9 vs rho（四种方法对比）
   - migration_rate + invalid_migration_ratio vs rho
   - knee 点比较表（P99 突增拐点）
5. **公平性审计**: 验证所有方法使用相同资源预算、相同 stale_view 约束。
6. **双口径对照**: 统一默认点与同预算最优点结论方向一致。

## 冻结参数（不可修改）

- 集群: 64 hosts × 16 cores = 1024 cores, 均质 C=1.0
- T_host=2.1us, T_net=3.15us, SYNC_LOAD=10us
- M0: α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us, budget=5% (effective 4.5%)
- B2: K_DST=2, cooldown=2us, budget=5%
- 统计: warmup=200k, measurement=1M, seeds={11,23,37,47,59}

## 关键文件

- 主仿真引擎: `src/core/simulator.cpp` + `include/sim/core/simulator.h`
- M0 算法: `include/sim/algorithms/host_proactive_migration.h`（per-host 并行 CHECK）
- B2 算法: `include/sim/algorithms/host_reactive_migration.h`（集中式 CHECK）
- B1 算法: `include/sim/algorithms/host_power_of_k.h`
- 常量: `include/sim/common/constants.h`
- 入口: `src/app/main.cpp`
- 构建: `cmake --build "d:\desktop\Test\build"`

## 验收标准

- 完成 W1 全扫描（18 rho × 4 methods × 5 seeds = 360 runs）
- 输出 knee 点比较与默认点/同预算最优点双口径对照
- 统一默认点与同预算最优点结论方向一致

## 输出并落盘到 `artifacts/step-02-tier2/`

- `summary.md`
- `metrics_scan.csv`
- `fairness_audit.md`
- `run_log.md`
- `next_prompt.md`
