# 02 Simulation Analysis

更新时间：2026-07-06

本文档是当前仿真逻辑的可审计版本，重点面向 RescueSched 主线，并保留 legacy baseline/图表链路的上下文。本文只分析已有代码，不声明物理机实验已经完成。

主要代码依据：

- 主入口与实验调度：`src/app/main.cpp`
- 仿真核心：`src/core/simulator.cpp`
- 仿真接口：`include/sim/core/simulator.h`
- 枚举类型：`include/sim/common/types.h`
- 常量与参数：`include/sim/common/constants.h`
- workload 生成：`include/sim/workloads/generators.h`
- 任务/队列模型：`include/sim/model/task.h`、`include/sim/model/core.h`、`include/sim/model/node.h`
- baseline 算法：`include/sim/algorithms/host_power_of_k.h`、`include/sim/algorithms/host_reactive_migration.h`、`include/sim/algorithms/host_proactive_migration.h`
- DQB/AQB legacy 算法：`include/sim/algorithms/dqb_proactive_migration.h`
- 指标计算：`include/sim/metrics/stats.h`、`include/sim/metrics/histogram.h`
- 图表脚本：`scripts/rescue_analysis.py`、`scripts/infocom_readiness_analysis.py`、`scripts/generate_charts.py`

## 1. 仿真要解决的科研问题

当前主线问题是：在微秒级 RPC/任务队列中，当部分 core 队列因为随机分配、突发到达或长尾服务时间而形成局部排队风险时，如何只迁移那些“本地会错过 SLO，但迁移后仍有机会按时完成，且不会明显伤害目标队列”的任务，从而降低 SLO 违约率和尾延迟。

RescueSched 关注的不是通用负载均衡，而是 SLO rescue：

- 识别 queued-but-not-running 的本地风险任务。
- 估计该任务留在源 core 的完成时间。
- 估计迁移到目标 core 后的完成时间。
- 检查目标 core 当前风险，避免把问题转移到目标队列。
- 在每轮检查预算内执行少量 intra-host migration。

该科研问题和已有 baseline 的关系：

| 方法 | 研究定位 |
| --- | --- |
| `L0_RandomCore` | 无队列感知的单机多核随机分配下界。 |
| `L1_WorkStealing` | 空闲 core 从最重队列偷任务，偏资源利用，不直接看 SLO 可抢救性。 |
| `M0_IntraHostProactive` | 主机内预测性迁移，按 latency gain 迁移，但没有 RescueSched 的完整 target-safety/rescuability 语义。 |
| `M1_RescueSched` | 当前主线，面向 SLO rescue 的队列修复策略。 |
| `M1_RescueSched_NoTargetSafety` | 消融：去掉目标安全检查。 |
| `M1_RescueSched_NoRescuable` | 消融：去掉严格可抢救判定，更多体现 pressure relief。 |
| `M1_RescueSched_Hybrid` | W2 burst/skew 场景下的 hybrid relief 变体。 |
| `B1/B2/M0/AQB/DQB` | 跨 host 或 legacy proactive/batch migration 对照与历史线。 |

## 2. 仿真系统模型

### 2.1 离散事件模型

仿真是离散事件系统，事件类型定义在 `include/sim/common/types.h`：

- `TASK_GENERATE`
- `TASK_ARRIVE`
- `TASK_FINISH`
- `SYNC_LOAD`
- `CHECK_MIGRATION`
- `TASK_EXECUTE` 目前不作为直接入队事件使用

事件循环位于 `Simulator::run()` 和 `Simulator::process_event()`，实现文件为 `src/core/simulator.cpp`。事件按时间戳和优先级进入 priority queue；仿真在 measurement window 完成 `MEASUREMENT_REQUESTS` 后停止。

### 2.2 集群和 core 模型

基础拓扑常量在 `include/sim/common/constants.h`：

| 常量 | 值 | 含义 |
| --- | ---: | --- |
| `NUM_HOSTS` | 64 | 跨 host legacy 模型的 host 数。 |
| `CORES_PER_HOST` | 16 | 每个 host 的 core 数。 |
| `TOTAL_CORES` | 1024 | 64 x 16。 |
| `HETERO_FAST_NODES` | 48 | 异构 profile 中快节点数。 |
| `HETERO_SLOW_NODES` | 16 | 异构 profile 中慢节点数。 |
| `HETERO_SLOW_CAPACITY` | 0.2 | 慢节点 capacity。 |

需要特别注意：`Simulator::configure()` 中，`L0/L1/M0_IntraHost/M1_RescueSched` 这些 intra-host 方法只启用 `active_host_count_=1`，即单 host、16 core 模型；`B0/B1/B2/M0_Proactive/AQB/DQB` 等 legacy 跨 host 方法使用 64 host 模型。

### 2.3 任务和队列模型

任务结构在 `include/sim/model/task.h`：

- `generate_time_us`
- `arrive_time_us`
- `base_service_time_us`
- `expected_service_time_us`
- `slo_target_us`
- `assigned_host`
- `assigned_core`
- 迁移/RescueSched 标记与估计字段

core 队列在 `include/sim/model/core.h`：

- 每个 core 有一个 intrusive FIFO wait queue。
- core 同时最多运行一个任务。
- `local_workload_us(now_us)` 返回等待队列估计工作量加当前 running residual。
- `compute_exec_time()` 在 `src/core/simulator.cpp` 中使用 `base_service_us / capacity + T_host_us`。

### 2.4 网络/主机时间模型

常量在 `include/sim/common/constants.h`：

- `T_host_us = 2.1`
- `T_net_oneway_us = 3.15`
- `T_rpc_us = 6.3`

跨 host migration/arrival 使用 `T_net_oneway_us` 建模；intra-host RescueSched 使用 `rescue_migration_cost_us` 建模。真实系统中这些必须通过 microbench 或运行时日志校准，不能直接假定成立。

## 3. 仿真假设

| 假设 | 代码依据 | 物理机风险 |
| --- | --- | --- |
| 时间单位统一为微秒 | 常量与 CSV 字段均使用 `_us` | 真实系统 clock source、TSC、系统调用开销会引入误差。 |
| 服务时间可由合成分布抽样 | `include/sim/workloads/generators.h` | 真实 RPC handler 的 service time 可能受 cache、IO、锁竞争影响。 |
| `rho` 可通过到达率和平均服务时间精确控制 | `Simulator::configure()` 中 `lambda_global = rho * effective_capacity / 24` | 物理机上服务能力随频率、NUMA、背景负载变化。 |
| W1/W2/W3 平均服务时间都按 24 us 处理 | `BIMODAL` 与 `W3_MEAN_SERVICE_US` | 物理服务时间校准误差会改变有效负载。 |
| SLO 由服务类别决定 | `Task::slo_for_service()` | 真实业务 SLO 可能按 RPC 类型、tenant 或 deadline 指定。 |
| 队列状态在仿真内可精确遍历 | `run_rescue_sched_check()` 扫描 wait_queue | 物理机中无锁/并发队列快照可能不一致。 |
| 部分算法可以使用 expected service time | `estimate_service_time()` | 真实系统无法天然知道未来服务时间，oracle 只能作为上界。 |
| warmup 后才开始记录指标 | `MetricsCollector::init(WARMUP_REQUESTS)` | 物理机实验也必须定义 warmup/measurement 边界。 |
| target harm 可以用仿真 counterfactual watch 归因 | `target_harm_watch_*` 字段 | 真实系统中反事实不可直接观测，需要日志和离线估计。 |

## 4. 输入数据和配置参数

### 4.1 CLI/config

入口解析位于 `src/app/main.cpp`：

- `--mode`
- `--config`
- `--workload`
- `--rho`
- `--seed`
- `--out-dir`
- `--output`

`config/rescuesched.yaml` 当前最小配置：

```yaml
mode: rescue-main
workload: W3
rhos: [0.85]
seeds: [11]
output_dir: artifacts/rescuesched-cli
```

`config/default.yaml` 当前是 step-00 skeleton 元信息，不是完整实验配置。

### 4.2 核心参数

| 参数 | 默认值 | 位置 | 说明 |
| --- | ---: | --- | --- |
| `WARMUP_REQUESTS` | 200000 | `include/sim/common/constants.h` | warmup 完成数。 |
| `MEASUREMENT_REQUESTS` | 1000000 | 同上 | 记录窗口完成数。 |
| `SLO_SHORT_US` | 40 | 同上 | 短任务 SLO。 |
| `SLO_LONG_US` | 200 | 同上 | 长任务 SLO。 |
| `SLO_SHORT_SERVICE_THRESHOLD_US` | 20 | 同上 | 短/长分类阈值。 |
| `SEEDS` | 11, 23, 37, 47, 59 | 同上 | 默认 5 seeds。 |
| `M0_T_CHECK_US` | 1.0 | 同上 | 默认检查周期。 |
| `RESCUE_SCAN_DEPTH` | 64 | 同上 | 每个源 core 扫描深度。 |
| `RESCUE_K_CANDIDATES` | 16 | 同上 | 每个源 core 接收候选上限。 |
| `RESCUE_H_TARGETS` | 4 | 同上 | 尝试目标 core 数。 |
| `RESCUE_BUDGET_PER_CHECK` | 1 | 同上 | 每轮检查迁移预算。 |
| `RESCUE_EPSILON_US` | 2.0 | 同上 | rescue 可行性安全裕量。 |
| `RESCUE_THETA` | 0 | 同上 | target risk 允许增量。 |
| `RESCUE_MIGRATION_COST_US` | 0.5 | 同上 | intra-host rescue migration 成本。 |

### 4.3 物理机难以严格控制的变量

| 变量 | 难点 |
| --- | --- |
| `rho` | 依赖真实 service rate，QPS 与有效利用率不一定线性。 |
| `base_service_time_us` | busy-loop、sleep、真实 handler 都有 jitter。 |
| `t_check_us` | 微秒级 timer 受 OS scheduler 影响。 |
| `rescue_migration_cost_us` | 取决于队列实现、cache、NUMA、锁竞争。 |
| `expected_service_time_us` | oracle 服务时间不可直接获得。 |
| target queue snapshot | 并发系统中快照可能过期或不一致。 |
| W2 burst state | 真实突发流量不一定服从二状态 MMPP。 |

## 5. Workload 生成方式

workload 类型在 `include/sim/common/types.h`，生成器在 `include/sim/workloads/generators.h`。

| Workload | 到达过程 | 服务时间 | 代码位置 | 说明 |
| --- | --- | --- | --- | --- |
| W1 `W1_POISSON_BIMODAL` | Poisson | 80% 5us + 20% 100us | `PoissonArrival`、`BimodalService` | 平均服务时间 24us。 |
| W2 `W2_MMPP_BIMODAL` | 二状态 MMPP | 80% 5us + 20% 100us | `MMPPArrival`、`BimodalService` | normal/burst，burst lambda=1.5x normal。 |
| W3 `W3_POISSON_LOGNORMAL` | Poisson | lognormal, mean=24us, sigma=1.0 | `PoissonArrival`、`LognormalService` | RescueSched 主实验重点。 |

W2 的局部 skew：

- 跨 host 模型中，burst 期间可能将请求偏向 `hot_nodes_`。
- intra-host 模型中，burst 期间可能将请求偏向 `hot_cores_`。
- 控制参数包括 `w2_hot_core_count` 和 `w2_hot_dispatch_prob`。

任务生成流程位于 `Simulator::handle_task_generate()`：

1. 根据 workload 生成下一次 inter-arrival。
2. 抽样服务时间。
3. 通过 `estimate_service_time()` 得到 expected service time。
4. 设置 SLO。
5. intra-host 方法直接入单 host 的 core 队列；跨 host 方法先经 scheduler 选择 host，再产生 `TASK_ARRIVE`。

## 6. 核心算法流程

### 6.1 RescueSched 主流程

主要函数：

- `run_rescue_sched_check()` in `src/core/simulator.cpp`
- `move_rescue_task_intra_host()` in `src/core/simulator.cpp`
- `on_rescue_finish()` in `include/sim/metrics/stats.h`

执行顺序：

1. `CHECK_MIGRATION` 周期触发。
2. 读取当前 host 的 16 个 core 队列。
3. 为每个 core 估算当前 workload 和 target risk。
4. 按 `risk_before`、`workload_us` 排序得到候选 target cores。
5. 扫描源 core wait queue 前缀。
6. 对每个任务估计：
   - 源 core 本地完成时间。
   - 是否 `locally_doomed`。
   - 迁移后远端完成时间。
   - 是否 `remote_feasible`。
   - 目标 core 是否 `strict_target_safe`。
7. 对可行迁移打分：
   - 默认 score = remote slack + local lateness bonus - migration cost。
   - `NoRescuable` 变体使用 pressure gain 型 score。
8. 按 score 排序。
9. 在 `rescue_budget_per_check` 和每源 core 至多一次迁移的约束下提交 migration。
10. 任务完成时统计 beneficial/useless/unsaved/harmful 等指标。

### 6.2 target safety

target safety 的核心条件：

- target 原本扫描前缀中 `risk_before == 0`
- 迁移任务自身带来的 `delta_risk <= rescue_theta`
- 若不满足，标准 `M1_RescueSched` 会记录 `target_unsafe_reject_count` 并拒绝。
- `M1_RescueSched_NoTargetSafety` 会允许接受 unsafe target，用于消融。

需要注意：当前默认目标插入策略是 append-to-tail。由于新迁移任务排在已有任务之后，真实 target-induced miss 在该模型下可能为 0；项目中另有 `RESCUE_TARGET_INSERT_HEAD_STRESS` 用于 stress target-side harm。

### 6.3 rescuability

标准 RescueSched 要求：

- 任务本地预计错过 SLO。
- 迁移后加上 `rescue_epsilon_us` 仍不晚于 deadline。

`M1_RescueSched_NoRescuable` 放松该条件，更接近 pressure relief，对 W2 burst/skew 边界有意义，但它不等同于标准 SLO rescue。

### 6.4 hybrid relief

`M1_RESCUE_HYBRID` 在标准 RescueSched 找不到可行 scored move 时，可调用 `run_hybrid_relief_check()`：

- 仅对 W2 MMPP 场景启用。
- 找 source workload 明显高于平均值的 core。
- 找低于平均 workload 的 destination core。
- 要求迁移后仍满足 deadline 且有最小 gain。

## 7. Baseline 算法流程

| 方法 | 代码位置 | 流程摘要 |
| --- | --- | --- |
| `B0_IdealCFCFS` | `try_b0_pull()` in `src/core/simulator.cpp` | 全局 FIFO 队列，空闲 host/core 拉取；理想化 baseline。 |
| `L0_RandomCore` | `enqueue_task_on_random_core()` | 单 host 内随机 core 分配；无迁移。 |
| `L1_WorkStealing` | `steal_one_task()` | core 完成任务且本地队列空时，从同 host workload 最大的其它 core 偷一个队首任务。 |
| `B1_PowerOf2` | `host_power_of_k.h` | dispatch 时随机采样 k=2 个 host，选择 stale queue 较短者。 |
| `B2_Reactive` | `host_reactive_migration.h` | Power-of-2 dispatch + 周期检查过载 host，根据 q_hi/q_lo 迁移尾部任务。 |
| `M0_Proactive` | `host_proactive_migration.h` | 周期扫描 core wait queue，根据本地风险、远端收益和 margin 迁移。 |
| `M0_IntraHostProactive` | `run_intra_proactive_check()` | 单 host 16 core 内扫描风险任务，迁移到 workload 最低 core。 |
| `M1_AQB_PM` | `host_proactive_migration.h` + `handle_check_migration()` | 收集单任务候选并进行 bounded batch selection。 |
| `M2_DQB_PM` | `dqb_proactive_migration.h` + `handle_check_migration()` | 构造 distribution-aware queue batch，按 batch summary 迁移。 |

物理机复现时，建议第一阶段只实现 `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched`。跨 host legacy 方法和 DQB/AQB 可暂作为仿真历史线。

## 8. 调度决策变量

| 决策变量 | 含义 | 决策阶段 | 物理机控制难度 |
| --- | --- | --- | --- |
| `method` | 调度/迁移算法选择 | 实验配置 | 易。 |
| `workload` | W1/W2/W3 | 实验配置 | 中，真实分布需校准。 |
| `rho` | 负载强度 | workload generator | 难，依赖真实服务率。 |
| `seed` | 随机性 | workload/generator | 中，多线程非确定性仍存在。 |
| `t_check_us` | 检查周期 | 调度控制循环 | 难，微秒 timer 抖动。 |
| `rescue_scan_depth` | 源队列扫描深度 | RescueSched check | 中，并发队列扫描成本。 |
| `rescue_k_candidates` | 每源 core 候选上限 | RescueSched candidate stage | 中。 |
| `rescue_h_targets` | target core 尝试数 | target selection | 中。 |
| `rescue_budget_per_check` | 每轮迁移预算 | commit stage | 易到中。 |
| `rescue_epsilon_us` | 安全裕量 | remote feasibility | 中，依赖时钟/服务估计。 |
| `rescue_theta` | target risk 容忍度 | target safety | 中，依赖风险模型。 |
| `rescue_migration_cost_us` | 迁移成本 | remote completion estimate | 难，必须 microbench。 |
| `service_estimate_mode` | 服务时间估计模式 | expected service time | 难，oracle 不可物理复用。 |
| `target_insert_policy` | 目标队列插入位置 | migration commit | 易，但影响 target harm 语义。 |
| `w2_hot_core_count`/`w2_hot_dispatch_prob` | W2 skew 控制 | workload routing | 中，真实 burst 难严格服从。 |

## 9. 评价指标

RescueSched CSV 表头由 `write_rescue_header()` in `src/app/main.cpp` 生成；指标计算在 `include/sim/metrics/stats.h`。

### 9.1 可以在物理机真实测量的指标

| 指标 | 仿真字段 | 物理机测量方式 |
| --- | --- | --- |
| 完成数 | `total_finished` | request completion log。 |
| 生成数 | `total_generated` | generator submitted count。 |
| 延迟分位数 | `P99_us`, `P999_us` | request start/finish timestamp。 |
| SLO 违约率 | `slo_violation_rate` | deadline miss / completed。 |
| 迁移次数 | `intra_move_count`, `rescue_success_count` | migration log。 |
| rescue attempt | `rescue_attempt_count` | scheduler check log。 |
| candidate count | `rescue_candidate_count` | scheduler decision log。 |
| reject count | `target_unsafe_reject_count`, `remote_infeasible_reject_count` | decision log。 |
| steal count | `steal_attempt_count`, `steal_success_count` | work stealing log。 |
| CPU/worker utilization | 仿真未直接完整输出 | perf/runtime counters。 |

### 9.2 模型估算或反事实指标

| 指标 | 字段 | 为什么不是直接真实测量 |
| --- | --- | --- |
| `expected_service_time_us` | task 内部字段 | 真实系统不知道未来实际 service time，除非 oracle/trace replay。 |
| `estimated_local_latency_us` | task 内部字段 | 这是迁移前仿真预测，不是物理事实。 |
| `rescue_predicted_remote_latency_us` | task 内部字段 | 基于队列快照和迁移成本估计。 |
| `remote_feasible_count` | CSV 字段 | 基于预测完成时间和 deadline。 |
| `target_safe_count` | CSV 字段 | 基于扫描前缀的 target risk model。 |
| `beneficial_migration_ratio` | CSV 字段 | 使用“预测本地延迟 > SLO 且迁移后实际 <= SLO”的仿真反事实。 |
| `useless_migration_ratio` | CSV 字段 | 由 needless/unsaved/harmful 组合而来，需要未迁移反事实。 |
| `harmful_actual_ratio` | CSV 字段 | 依赖 target harm watch 机制归因，不是天然观测。 |
| `target_induced_miss_actual` | CSV 字段 | 依赖迁移前记录的 target counterfactual latency。 |
| `summary_update_cost_est_us` | diag 字段 | 固定系数估算：`summary_update_count * 0.02`。 |
| `batch_estimation_cost_est_us` | diag 字段 | 固定系数估算：`batch_candidate_count * 0.03`。 |
| `target_selection_cost_est_us` | diag 字段 | 固定系数估算：`(target_plan_reject + batch_selected) * 0.005`。 |

### 9.3 Histogram 注意事项

`include/sim/metrics/histogram.h` 使用 0-10000us 的固定 bucket histogram 估计分位数，返回 bucket 中点。物理机结果若用原始 latency 排序计算分位数，可能与仿真 histogram 有小差异。

## 10. 实验输出文件

主要输出由 `src/app/main.cpp` 的 mode 决定：

| Mode | 输出路径 | 内容 |
| --- | --- | --- |
| `rescue-main` | `artifacts/step-15-rescuesched/rescue_main.csv` | W3/W1/W2 可配置主 sweep。 |
| `rescue-w3-only` | `artifacts/step-15-rescuesched/rescue_w3_only.csv` | W3 rho=0.85 focus。 |
| `rescue-ablation` | `artifacts/step-15-rescuesched/rescue_ablation.csv` | target safety / rescuable 消融。 |
| `rescue-check-sweep` | `artifacts/step-15-rescuesched/rescue_check_sweep.csv` | check period、epsilon、budget sweep。 |
| `rescue-overload-sanity` | `artifacts/step-15-rescuesched/rescue_overload_sanity.csv` | W1 sanity 与 overload boundary。 |
| `rescue-w2-burst` | `artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv` | W2 burst/skew。 |
| `rescue-robustness-10seed` | `artifacts/step-17-rescuesched-closure/rescue_robustness_10seed.csv` | W2/W3 10 seed robustness。 |
| `rescue-cost-microbench` | `artifacts/step-17-rescuesched-closure/migration_cost_microbench.csv` | 仿真内 descriptor queue/handoff microbench。 |
| `rescue-calibration` | `artifacts/step-17-rescuesched-closure/rescue_calibration.csv` | migration cost 与 service estimate sensitivity。 |
| `rescue-estimator-main` | `artifacts/step-18-infocom-readiness/rescue_estimator_main.csv` | W3 estimator readiness。 |
| `rescue-estimator-w2` | `artifacts/step-18-infocom-readiness/rescue_estimator_w2.csv` | W2 estimator readiness。 |
| `rescue-cost-calibration` | `artifacts/step-18-infocom-readiness/rescue_cost_calibration.csv` | measured handoff cost sensitivity。 |
| `rescue-w2-boundary` | `artifacts/step-18-infocom-readiness/rescue_w2_boundary.csv` | W2 hot-core/prob/rho boundary。 |
| `rescue-hybrid-main` | `artifacts/step-18-infocom-readiness/rescue_hybrid_main.csv` | hybrid W2 main。 |
| `rescue-target-safety-stress` | `artifacts/step-18-infocom-readiness/rescue_target_safety_stress.csv` | append-tail vs head-insert-stress target safety。 |

`--out-dir` 可重定向 artifact root，`--output` 可对单输出 mode 指定 CSV 文件。

## 11. 图表生成流程

### 11.1 RescueSched readiness 图

脚本：`scripts/rescue_analysis.py`

命令：

```bash
python scripts/rescue_analysis.py
```

输入：

- `artifacts/step-15-rescuesched/*.csv`
- 可选 `artifacts/step-17-rescuesched-closure/*.csv`

输出：

- `artifacts/step-16-rescuesched-readiness/median_summary.csv`
- `artifacts/step-16-rescuesched-readiness/readiness_report.md`
- `artifacts/step-16-rescuesched-readiness/figures/*.png|*.pdf`
- `artifacts/step-17-rescuesched-closure/closure_median_summary.csv`
- `artifacts/step-17-rescuesched-closure/ci_summary.csv`
- `artifacts/step-17-rescuesched-closure/figures/*.png|*.pdf`

### 11.2 INFOCOM readiness 表

脚本：`scripts/infocom_readiness_analysis.py`

命令：

```bash
python scripts/infocom_readiness_analysis.py
```

输出：

- `artifacts/step-18-infocom-readiness/infocom_median_summary.csv`
- `artifacts/step-18-infocom-readiness/infocom_ci_summary.csv`
- `artifacts/step-18-infocom-readiness/summary.md`

当前该脚本主要生成 summary/table，不生成 figure。

### 11.3 Legacy fig1-fig9

脚本：`scripts/generate_charts.py`

命令：

```bash
python scripts/generate_charts.py
```

输出：

- `docs/figures/fig1_w1_p99_full.{png,pdf}`
- `docs/figures/fig2_w1_p999_full.{png,pdf}`
- `docs/figures/fig3_w2_bar.{png,pdf}`
- `docs/figures/fig4_w3_ci.{png,pdf}`
- `docs/figures/fig5_cross_workload.{png,pdf}`
- `docs/figures/fig6_migration_metrics.{png,pdf}`
- `docs/figures/fig7_sensitivity.{png,pdf}`
- `docs/figures/fig8_heterogeneous.{png,pdf}`
- `docs/figures/fig9_negative_case.{png,pdf}`

## 12. 每个实验图对应的数据来源

### 12.1 RescueSched 图

| 图 | 输出 | 数据来源 | 生成脚本 |
| --- | --- | --- | --- |
| RescueSched SLO vs rho | `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_slo_vs_rho.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_main.csv` | `scripts/rescue_analysis.py` |
| RescueSched ablation quality | `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_ablation_quality.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_ablation.csv` | `scripts/rescue_analysis.py` |
| RescueSched budget sweep | `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_budget_sweep.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_check_sweep.csv` | `scripts/rescue_analysis.py` |
| W2 burst SLO vs rho | `artifacts/step-17-rescuesched-closure/figures/fig_w2_burst_slo_vs_rho.{png,pdf}` | `artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv` | `scripts/rescue_analysis.py` |
| Rescue calibration | `artifacts/step-17-rescuesched-closure/figures/fig_rescue_calibration.{png,pdf}` | `artifacts/step-17-rescuesched-closure/rescue_calibration.csv` | `scripts/rescue_analysis.py` |

### 12.2 Legacy fig1-fig9

| 图 | 输出 | 数据来源 | 生成脚本 |
| --- | --- | --- | --- |
| Fig1 W1 P99 | `docs/figures/fig1_w1_p99_full.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `scripts/generate_charts.py` |
| Fig2 W1 P99.9 | `docs/figures/fig2_w1_p999_full.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `scripts/generate_charts.py` |
| Fig3 W2 bar | `docs/figures/fig3_w2_bar.{png,pdf}` | `artifacts/step-01-tier1/metrics_table.csv` | `scripts/generate_charts.py` |
| Fig4 W3 CI | `docs/figures/fig4_w3_ci.{png,pdf}` | `artifacts/step-03-tier3/metrics_table.csv` | `scripts/generate_charts.py` |
| Fig5 cross workload | `docs/figures/fig5_cross_workload.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv`, `artifacts/step-01-tier1/metrics_table.csv`, `artifacts/step-03-tier3/metrics_table.csv` | `scripts/generate_charts.py` |
| Fig6 migration metrics | `docs/figures/fig6_migration_metrics.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `scripts/generate_charts.py` |
| Fig7 sensitivity | `docs/figures/fig7_sensitivity.{png,pdf}` | `artifacts/step-04b-sensitivity/sensitivity_scan.csv` | `scripts/generate_charts.py` |
| Fig8 heterogeneous | `docs/figures/fig8_heterogeneous.{png,pdf}` | `artifacts/step-04c-heterogeneous/metrics_table.csv`, `artifacts/step-01-tier1/metrics_table.csv` | `scripts/generate_charts.py` |
| Fig9 negative case | `docs/figures/fig9_negative_case.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv`, `artifacts/step-03-tier3/metrics_table.csv`, `artifacts/step-01-tier1/metrics_table.csv` | `scripts/generate_charts.py` |

## 13. 仿真中可以直接复用到物理机的部分

| 可复用部分 | 复用方式 |
| --- | --- |
| 实验命名和 mode 体系 | 物理机 runtime 可沿用 `rescue-main`、`rescue-smoke` 等命名。 |
| workload 参数定义 | W1/W2/W3 的目标分布和 rho 点可作为 generator config。 |
| seed/rho/workload/output CLI 习惯 | 物理程序保持相同 CLI，有利于脚本复用。 |
| CSV 字段命名 | request/migration summary 尽量输出同名字段。 |
| baseline 名称 | 物理实现继续使用 `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched`。 |
| RescueSched 决策结构 | “locally doomed -> remote feasible -> target safe -> budgeted move”的结构可保留。 |
| target-safety 消融设计 | no-target-safety 作为物理消融仍有价值。 |
| analysis grouping 逻辑 | median/CI/group-by 脚本可在字段一致时复用或轻改。 |
| artifact provenance 约定 | 每张图绑定输入 CSV、脚本、命令、commit。 |

可保留为算法决策逻辑的代码思想：

- 源队列前缀扫描。
- 目标 core workload/risk ranking。
- 迁移预算。
- `rescue_epsilon_us` slack guard。
- target safety gate。
- 消融开关。

但不建议直接把 `src/core/simulator.cpp` 当作物理 runtime 使用；它是事件模拟器，应与真实线程/队列实现分离。

## 14. 仿真中不能直接复用到物理机的部分

| 不能直接复用 | 替代方案 |
| --- | --- |
| priority-queue 事件循环 | 真实线程、网络、timer、worker queue。 |
| oracle service time | runtime estimator、历史 profile、trace replay。 |
| `base_service_time_us` 直接可见 | handler 执行计时或合成 busy-loop 参数。 |
| 精确遍历 wait_queue 且无并发冲突 | 并发安全 snapshot 或近似队列 summary。 |
| `target_harm_watch` 反事实归因 | 物理日志 + 离线 replay/counterfactual estimator。 |
| 固定 `T_host_us`/`T_net_oneway_us` | 机器 microbench、RPC pingpong、handoff benchmark。 |
| `rho` 到 lambda 的精确换算 | 通过 calibration 找可持续 QPS。 |
| histogram 分位数实现 | 物理端建议保留原始 latency，再离线计算。 |
| synthetic W2 hot-core 随机偏置 | 真实 burst trace 或 generator 状态机。 |
| DQB/AQB 估算成本字段 | 真实 CPU cycles / perf / wall-clock instrumentation。 |

需要替换为真实系统采集模块的部分：

- 任务生成事件 -> load generator。
- arrival/finish event -> request timestamp log。
- service time 抽样 -> synthetic handler 或 trace replay。
- queue state 读取 -> runtime queue snapshot。
- migration commit -> 实际跨 worker handoff。
- `MetricsCollector` 部分反事实字段 -> physical log + offline estimator。
- `summary_update_cost_est_us` 等固定估算 -> real overhead measurement。

## 15. 仿真中在真实系统里可能不成立的假设

| 仿真假设 | 为什么可能不成立 | 应对 |
| --- | --- | --- |
| 微秒级检查周期稳定 | OS timer 和线程调度 jitter 大 | 记录实际 check timestamp，报告 jitter。 |
| 任务可任意从队列中间移除 | 真实队列可能只支持 head/tail 或需要锁 | 设计支持 remove 的队列，或限制候选位置。 |
| 迁移成本固定 | cache、锁、NUMA、跨线程唤醒成本变化 | microbench + sensitivity。 |
| expected service time 可准确估计 | 真实 RPC service time 不可预知 | EWMA/class/quantile estimator。 |
| target queue 快照实时准确 | 并发下快照过期 | 使用版本号/近似 summary。 |
| append-tail 不伤害已有 target 任务 | 真实 runtime 中 lock、cache、抢占仍可能影响 | 记录 target-side latency 和 contention。 |
| workload 服从 Poisson/MMPP/lognormal | 真实 trace 可能有自相关和多峰 | trace replay。 |
| 64x16 或 1x16 拓扑代表真实机器 | CloudLab 节点 core/NUMA 不同 | 明确映射并记录 host inventory。 |
| warmup 后系统稳定 | 物理机 thermal/frequency/background load 可能变化 | 长时间监控和重复实验。 |
| P999 稳定 | 极端尾部需要大量样本 | 增加样本量和 CI。 |

## 16. 物理机迁移时的指标分类建议

第一阶段必测：

- `P99_us`
- `slo_violation_rate`
- `total_generated`
- `total_finished`
- `intra_move_count`
- `rescue_attempt_count`
- `rescue_success_count`
- `target_unsafe_reject_count`

第二阶段再测或估计：

- `P999_us`
- `beneficial_migration_ratio`
- `useless_migration_ratio`
- `harmful_actual_ratio`
- `target_induced_miss_actual`
- `remote_feasible_count`
- `target_safe_count`

需要特别标注为模型估算：

- `summary_update_cost_est_us`
- `batch_estimation_cost_est_us`
- `target_selection_cost_est_us`
- oracle service estimate 下的所有结果
- 任何依赖 counterfactual local latency 的 migration quality 指标

## 17. 当前结论

当前仿真已经足够支持 RescueSched 的算法级分析、消融设计、CSV/图表复现和第一阶段物理机映射。最关键的迁移边界是：

- 可复用算法结构、参数命名、实验矩阵和 CSV schema。
- 不能复用离散事件执行环境、oracle 服务时间、精确队列快照和反事实指标采集。
- 物理机复现的第一目标应是对齐 P99/SLO/migration/rescue success 等真实可测指标，而不是一开始就追求 beneficial/useless/harmful 的完整仿真语义。

下一步建议先实现物理日志 schema 和单机 worker queue runtime，再用最小 W3 `rho=0.85` 实验对齐 `L1/M0/M1` 三个方法。
