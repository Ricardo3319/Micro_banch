# 04 Experiment Design

更新时间：2026-07-07

本文档基于当前仿真代码和 `docs/03-physical-mapping.md`，设计将 RescueSched 核心算法迁移到真实物理机 `c6525-25g` 的用户态复现实验。当前项目已有完整仿真入口、参数、baseline、CSV schema 和绘图分析脚本，但没有物理机 runtime；因此本文显式区分【可复用】和【需要新增】。

## 1. 实验目标

核心目标是在真实用户态系统中验证：RescueSched 的 rescuability-aware migration 是否能在单机多 core 场景下降低平均延迟、P99/P999 尾延迟和 SLO 违约率，同时不依赖明显更高的迁移次数或不可接受的 CPU/control-plane 开销。

对齐当前仿真主线：

| 目标 | 当前代码依据 | 物理机验证方式 | 状态 |
| --- | --- | --- | --- |
| 验证 W3 heavy-tail 下 RescueSched 的 SLO 改善 | `src/app/main.cpp::run_rescue_smoke()`、`run_rescue_main()` | 单台 c6525-25g，16 worker，W3 Poisson + lognormal，比较 L1/M0/M1 | 【需要新增】physical runtime |
| 保持仿真参数和输出 schema 可比 | `include/sim/common/constants.h`、`src/app/main.cpp::write_rescue_header()` | physical summary CSV 对齐 `scenario,workload,method,rho,seed,...` | 【可复用】字段定义 |
| 区分真实可测指标和模型估计指标 | `include/sim/metrics/stats.h`、`docs/03-physical-mapping.md` | raw log + offline aggregation；反事实指标单独标注 estimator-based | 【需要新增】聚合脚本 |
| 量化用户态调度开销 | 仿真中 `summary_update_cost_est_us()` 等为固定系数估算 | 真实计时 `scan_ns,candidate_eval_ns,target_select_ns,commit_ns` | 【需要新增】overhead log |

第一阶段只验证单机 intra-host 结论，不验证 `NUM_HOSTS=64`、跨机 RPC、跨 host migration 和 DQB/AQB 完整论文矩阵。

## 2. 第一阶段最小可运行 Demo

第一阶段建议实现一个用户态原型：一个进程内包含 generator、dispatcher、16 个 worker queue、一个 scheduler/control thread 和 CSV logger。所有 request 都是内存中的 descriptor，不需要修改 Linux 内核。

| 维度 | 最小 Demo 设置 | 当前代码关联 | 状态 |
| --- | --- | --- | --- |
| 机器 | 1 台 `c6525-25g` | `docs/03` 第一阶段目标 | 【需要新增】真实环境 manifest |
| worker | 16 个 pinned worker | `CORES_PER_HOST = 16` in `include/sim/common/constants.h` | 【可复用】常量语义；【需要新增】线程绑定 |
| workload | `W3_POISSON_LOGNORMAL` | `LognormalService` in `include/sim/workloads/generators.h` | 【可复用】分布参数；【需要新增】真实 generator |
| load | `rho=0.85` | `run_rescue_smoke()` 和 `run_rescue_main()` | 【可复用】实验点；【需要新增】物理 QPS 校准 |
| methods | `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched` | `MethodType` in `include/sim/common/types.h` | 【可复用】策略枚举；【需要新增】runtime 策略实现 |
| estimator | `class_mean` 或 `EWMA`；oracle 只做离线上界 | `estimate_service_time()` 支持多模式 | 【可复用】估计器逻辑；【需要新增】非 oracle runtime 状态 |
| 指标 | P50/P90/P99/P999、SLO、throughput、CPU、queue length、migration count、scheduler overhead | `write_rescue_header()` 部分字段 | 【可复用】summary schema；【需要新增】raw logs |
| 完成判据 | 三个方法均能完成 warmup + measurement，产生可聚合日志 | `WARMUP_REQUESTS`、`MEASUREMENT_REQUESTS` | 【可复用】窗口定义；【需要新增】物理停止条件 |

Demo 不以 `beneficial_migration_ratio`、`useless_migration_ratio`、`target_induced_miss_actual` 为成功标准，因为这些依赖 counterfactual 或 target-side 归因。第一阶段可以记录所需原始字段，为第二阶段离线估计做准备。

## 3. 推荐物理机数量

| 阶段 | 推荐机器数量 | 用途 | 原因 |
| --- | --- | --- | --- |
| 第一阶段最小 Demo | 1 台 c6525-25g | generator、scheduler、workers、logger 同机隔离 core | 当前 RescueSched 主线是 intra-host；可直接对齐 `active_host_count_=1` |
| 第二阶段完整实验 | 2 台 c6525-25g | 1 台 generator/collector，1 台 server/runtime | 避免 generator 与 server 抢 CPU；引入真实 25G 网络入口延迟 |
| 第三阶段论文实验 | 3-4 台或更多【待确认】 | 多 generator、多 server、独立 collector | 做多 seed、多 workload、网络/NUMA sensitivity 和稳定统计 |

当前项目中的 `NUM_HOSTS=64` 是仿真集群规模，不应在第一阶段强行映射到 64 台物理机。

## 4. 单机 / 多机网络拓扑

单机拓扑，第一阶段推荐：

```text
c6525-25g node
  generator thread(s)  -> dispatcher -> 16 worker queues -> worker threads
                                  \-> scheduler/control thread
                                  \-> async logger
```

要求：

- generator、scheduler、worker、logger 分别绑定固定 core。
- request 通过内存 queue 传递；migration 是跨 worker queue 的 descriptor handoff。
- 网络不参与主路径，便于先隔离调度算法本身。

两机拓扑，第二阶段推荐：

```text
c6525-25g generator node --25G--> c6525-25g server node
                                      16 pinned workers
                                      scheduler/control thread
                                      local logger
```

要求：

- generator node 负责产生 Poisson/MMPP 请求和收集 client-side latency。
- server node 负责 worker queue、service handler、RescueSched。
- 需要额外记录 NIC、MTU、IRQ affinity、RTT、packet loss 和 server-side latency。

多机拓扑，进阶：

- 多 generator 对单 server，验证 ingress 压力和连接偏斜。
- 多 server 不等同于当前 RescueSched 第一阶段，因为当前核心物理目标是 intra-host queue migration。
- 跨 host migration、RPC handoff 和 DQB/AQB 相关实验应作为后续单独设计。

## 5. 操作系统和内核要求

基础用户态方案不要求修改 Linux 内核。

| 项目 | 要求 | 状态 |
| --- | --- | --- |
| OS | Linux，发行版和版本【待确认】 | 【需要新增】记录到 `host_inventory.txt` |
| Kernel | 支持 `sched_setaffinity`、`clock_gettime`、`perf_event_open` 或 `perf stat` | 【需要新增】环境确认 |
| 权限 | 可使用 `taskset`、`numactl`、`perf stat`、`mpstat/pidstat`；是否需要 root【待确认】 | 【需要新增】实验环境准备 |
| CPU governor | 建议固定 performance 或记录实际 governor | 【需要新增】环境记录 |
| 时钟 | 使用 `CLOCK_MONOTONIC_RAW` 或校准 TSC；跨机时钟需单独同步 | 【需要新增】runtime 支持 |

可选但不属于内核修改：

- 使用 boot 参数或 systemd 配置隔离实验 core：`isolcpus`、`nohz_full`、`rcu_nocbs`【待确认】。
- 固定 IRQ affinity，避免 NIC 或系统中断打到 worker core。
- 使用 `chrt` 或实时优先级运行 scheduler thread【待确认】，需评估是否影响公平性。

进阶方案，单独列出，不作为第一阶段要求：

- eBPF/XDP 采集网络路径或做 kernel-bypass ingress。
- DPDK 或 io_uring 网络路径，降低 network stack jitter。
- 内核调度器或队列机制修改。当前 RescueSched 原型不建议走这条路，除非用户态结果证明调度开销完全受内核机制限制。

## 6. CPU 绑定策略

第一阶段建议绑定布局：

| 角色 | core 数 | 绑定建议 | 备注 |
| --- | --- | --- | --- |
| worker threads | 16 | 对齐 `CORES_PER_HOST=16`，优先物理 core，避免 SMT sibling | c6525-25g 实际拓扑【待确认】 |
| scheduler/control thread | 1 | 与 worker 同 NUMA node，但独占 core | 记录 tick jitter 和 CPU 使用率 |
| generator thread | 1-2 | 同机 Demo 中独占 core；两机实验在 generator node 独占 | 不应与 worker 抢 core |
| async logger | 1 | 可独占或与 collector 合并 | 若日志同步写入影响延迟，必须改为 ring buffer |
| OS/IRQ | 预留 1-2 | 不用于 worker 或 scheduler | 记录 IRQ affinity |

落地要求：

- 【需要新增】physical runtime 启动时打印 `worker_id -> cpu_id`、`scheduler_cpu`、`generator_cpu`。
- 【需要新增】实验脚本保存 `lscpu -e`、`numactl --hardware`、`/proc/cpuinfo`、`/proc/interrupts`。
- 【可复用】worker 数默认来自 `include/sim/common/constants.h::CORES_PER_HOST` 的 16 core 语义。

## 7. NUMA 策略

第一阶段使用单 NUMA node 优先策略：

- 如果一个 NUMA node 有至少 18 个可用物理 core，则 worker、scheduler、logger 尽量放在同一 NUMA node，generator 可放同 node 或单独预留 core。
- 如果单 NUMA node 不足 18 个物理 core，则优先保证 16 worker 分布规则固定，并在 manifest 中记录跨 NUMA 布局。
- request descriptor、worker queue 和 per-worker summary 应在 worker 所在 NUMA node 上初始化；避免运行中频繁跨 NUMA 分配。

第二阶段增加 NUMA sensitivity：

| 实验 | 目的 | 状态 |
| --- | --- | --- |
| same-NUMA | 验证算法本身，降低硬件干扰 | 【需要新增】 |
| cross-NUMA workers | 观察 migration cost 和 queue snapshot 跨 NUMA 影响 | 【需要新增】 |
| scheduler cross-NUMA | 观察 control thread 读取队列 summary 的远端代价 | 【需要新增】 |

当前仿真只把 host 抽象为 16 个同构 core，不包含 NUMA 层级；物理实验必须显式报告 NUMA 配置。

## 8. Workload 生成方案

第一阶段 workload：W3 Poisson + lognormal。

| 组件 | 设计 | 当前代码关联 | 状态 |
| --- | --- | --- | --- |
| arrival | 指数分布 inter-arrival，按目标 `rho` 转换 QPS | `PoissonArrival` in `include/sim/workloads/generators.h` | 【可复用】分布逻辑；【需要新增】真实定时发送 |
| service | lognormal target service，`mu=2.6782363`、`sigma=1.0`、mean 24 us | `W3_LOGNORMAL_*` in `include/sim/common/constants.h` | 【可复用】参数；【需要新增】busy-work calibration |
| service class | `target_service_us <= 20` 为 mice，否则 elephant | `SLO_SHORT_SERVICE_THRESHOLD_US` | 【可复用】分类阈值 |
| SLO | mice 40 us，elephant 200 us | `SLO_SHORT_US`、`SLO_LONG_US` | 【可复用】阈值 |
| warmup | 200000 completed requests | `WARMUP_REQUESTS` | 【可复用】计数 |
| measurement | 1000000 completed requests | `MEASUREMENT_REQUESTS` | 【可复用】计数 |

第二阶段 workload：

- W1 Poisson + bimodal：复用 `BIMODAL_SHORT_US=5`、`BIMODAL_LONG_US=100`、`BIMODAL_SHORT_PROB=0.8`。
- W2 MMPP + bimodal：复用 `W2_LAMBDA_BURST_FACTOR=1.5`、`W2_NORMAL_STAY_US=5000`、`W2_BURST_STAY_US=500`、`w2_hot_core_count`、`w2_hot_dispatch_prob`。
- trace replay【需要新增】：把真实 trace 转成 `request_id,interarrival_us,target_service_us,class`，并复用相同 runtime。

校准流程【需要新增】：

1. 空 handler 测 framework overhead。
2. 单 worker 校准 busy loop，使实际 `actual_service_us` 接近 target distribution。
3. 16 worker 无迁移 baseline 下测最大稳定吞吐。
4. 用实测平均 service 和吞吐换算 physical `rho`，同时保留 target `rho`。

## 9. Baseline 设计

| Baseline | 当前代码位置 | 物理机实现 | 状态 |
| --- | --- | --- | --- |
| `L0_RandomCore` | `src/core/simulator.cpp::enqueue_task_on_random_core()` | dispatcher 随机选择 worker queue，不 stealing，不 proactive，不 rescue | 【可复用】语义；【需要新增】runtime |
| `L1_WorkStealing` | `src/core/simulator.cpp::steal_one_task()` | worker 空闲时从其他 queue 尝试 steal queued request | 【可复用】语义；【需要新增】并发队列实现 |
| `M0_IntraHostProactive` | `src/core/simulator.cpp::run_intra_proactive_check()` | scheduler 周期扫描 source queue，按预测收益移动 queued request | 【可复用】决策思路；【需要新增】真实快照和 handoff |
| `M1_RescueSched` | `src/core/simulator.cpp::run_rescue_sched_check()` | 只迁移 locally doomed、remote feasible、target safe 的 queued request | 【可复用】核心算法逻辑；【需要新增】runtime 采集和提交 |
| `M1_RescueSched_NoTargetSafety` | `src/app/main.cpp::method_has_target_safety()` | 关闭 target safety 的消融实验 | 第二阶段【可复用】开关语义；【需要新增】runtime |
| `M1_RescueSched_NoRescuable` | `src/app/main.cpp::method_has_rescuable_filter()` | 关闭 rescuable filter 的消融实验 | 第二阶段【可复用】开关语义；【需要新增】runtime |

第一阶段主对比使用 `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched`。`L0_RandomCore` 可作为 sanity baseline，但不一定写入第一阶段主结论。

## 10. 我的方法如何落地

RescueSched 的物理机落地应保持“决策逻辑可复用、执行环境替换”的边界。

| 仿真逻辑 | 当前代码 | 用户态落地方式 | 状态 |
| --- | --- | --- | --- |
| 队列快照 | `Core::queue`、`TaskQueue`、`Node::local_total_workload_us()` | 每个 worker 维护 queue length、estimated work、head age 的 lock-free 或低锁 summary | 【需要新增】 |
| locally doomed | `run_rescue_sched_check()` 估计 local latency 与 SLO | 用 `now - arrival + queued_work_ahead + predicted_service` 判断本地是否 miss | 【可复用】公式；【需要新增】runtime 时间源 |
| remote feasible | 同上 | 用 target queue summary、migration cost、predicted service 判断迁移后是否满足 SLO | 【可复用】公式；【需要新增】目标 summary |
| target safe | 同上 | 估计迁入后是否使目标队列已有请求风险增加超过 `theta` | 【可复用】概念；【需要新增】watch/归因日志 |
| candidate scoring | `score = remote slack + local lateness bonus - migration cost` 相关逻辑 | 在 scheduler thread 中对候选打分，按 score 提交 | 【可复用】排序逻辑；【需要新增】线程安全候选池 |
| migration commit | `move_rescue_task_intra_host()` | CAS 将 request 从 `queued` 标为 `moving`，从源 queue remove，append 到目标 queue | 【需要新增】 |
| service estimator | `estimate_service_time()` | 第一阶段用 class mean 或 EWMA；oracle 只用于离线 replay | 【可复用】non-oracle 模式；【需要新增】在线更新 |

推荐用户态组件【需要新增】：

```text
src/physical/
  main_physical.cpp          # physical runner entry
  runtime/
    request.h                # request descriptor and timestamps
    worker.h/.cpp            # pinned workers and queues
    scheduler.h/.cpp         # L1/M0/M1 control logic
    queue.h/.cpp             # concurrent queue with bounded scan/remove
    workload.h/.cpp          # Poisson/MMPP + service target generation
    metrics.h/.cpp           # raw log buffers and summary aggregation hooks
    affinity.h/.cpp          # CPU/NUMA binding helpers
```

## 11. 需要采集的指标

| 类别 | 指标 | 采集方式 | 对齐关系 |
| --- | --- | --- | --- |
| latency | mean、P50、P90、P99、P999、max | request raw latency offline 聚合 | 对齐 `P99_us`、`P999_us` |
| SLO | total/mice/elephant SLO violation rate | `latency_us > slo_us` | 对齐 `slo_violation_rate` |
| throughput | offered QPS、accepted QPS、completed QPS | generator/server counters | 补充仿真 `rho` |
| CPU | per-core utilization、scheduler CPU、worker CPU | `perf stat`、`mpstat`、runtime busy time | 仿真没有真实 CPU 指标，物理新增 |
| queue | per-worker queue length、estimated work、head age | periodic queue summary log | 对齐算法输入 |
| migration | attempt、commit、fail、intra_move_count、rescue_success_count、migration_rate | migration log | 对齐 `intra_move_*`、`rescue_*` |
| decision | locally doomed、remote feasible、target safe、reject reason | decision log | 对齐 RescueSched 内部计数 |
| overhead | scan time、candidate eval time、target selection time、commit time、tick jitter | scheduler instrumentation | 替换仿真固定估算 overhead |
| hardware | cache misses、context switches、cycles、instructions | `perf stat` | 物理新增 |
| environment | CPU topology、NUMA、kernel、governor、IRQ | inventory 文件 | 复现实验必要信息 |

## 12. 日志格式

推荐每次运行生成一个 run directory：

```text
results/physical/<date>/<run_id>/
  run_manifest.json
  run_config.yaml
  host_inventory.txt
  request_log.csv
  scheduler_log.csv
  decision_log.csv
  migration_log.csv
  queue_sample_log.csv
  overhead_log.csv
  perf_stat.txt
  summary.csv
```

`request_log.csv`【需要新增】：

```text
run_id,phase,request_id,seed,method,workload,rho,class,target_service_us,
arrival_ts_ns,enqueue_ts_ns,start_ts_ns,finish_ts_ns,latency_us,
actual_service_us,slo_us,miss,assigned_worker,final_worker,migrated
```

`scheduler_log.csv`【需要新增】：

```text
run_id,tick_id,ts_ns,method,event,worker_id,queue_len,queue_work_us,
scan_depth,candidate_count,target_count,committed_count,tick_interval_us
```

`decision_log.csv`【需要新增】：

```text
run_id,tick_id,request_id,src_worker,dst_worker,arrival_age_us,
predicted_service_us,predicted_local_latency_us,predicted_remote_latency_us,
slo_us,locally_doomed,remote_feasible,target_safe,reject_reason,score
```

`migration_log.csv`【需要新增】：

```text
run_id,migration_id,request_id,method,src_worker,dst_worker,
attempt_ts_ns,commit_ts_ns,finish_ts_ns,target_service_us,predicted_service_us,
migration_cost_est_us,commit_latency_us,actual_latency_us,slo_us,miss
```

`summary.csv`【可复用 schema，需新增 physical aggregator】应尽量兼容 `src/app/main.cpp::write_rescue_header()`，至少包含：

```text
scenario,workload,method,rho,seed,check_period_us,epsilon_us,budget_per_check,
k_candidates,h_targets,migration_cost_us,service_estimate_mode,
P99_us,P999_us,slo_violation_rate,total_finished,total_generated,
migration_rate,intra_move_rate,intra_move_count,rescue_attempt_count,
rescue_candidate_count,locally_doomed_count,remote_feasible_count,
target_safe_count,rescue_success_count,target_unsafe_reject_count,
remote_infeasible_reject_count
```

## 13. 实验矩阵

第一阶段最小 Demo 矩阵：

| 维度 | 取值 | 状态 |
| --- | --- | --- |
| machine | 1 台 c6525-25g | 【需要新增】 |
| topology | single-node, in-memory queues | 【需要新增】 |
| workers | 16 pinned physical cores | 【可复用】`CORES_PER_HOST=16` |
| workload | W3 | 【可复用】分布参数 |
| rho | 0.85 | 【可复用】`run_rescue_smoke()` 默认点 |
| seed | 11 | 【可复用】`SEEDS` |
| methods | L1, M0, M1 | 【可复用】`MethodType` |
| estimator | class_mean 或 EWMA | 【可复用】估计器模式；【需要新增】runtime |
| measurement | warmup 200k + measure 1M completed | 【可复用】窗口常量 |

第二阶段完整实验矩阵：

| 维度 | 取值 | 目的 |
| --- | --- | --- |
| workload | W1, W2, W3 | 覆盖 bimodal、bursty、heavy-tail |
| rho | W3: 0.50, 0.70, 0.85, 0.92；W2 同 `W2_RHO_POINTS`；W1【待确认】 | 画 SLO/latency vs load |
| methods | L0, L1, M0, M1, NoTargetSafety, NoRescuable | 主对比和消融 |
| check period | 1, 2, 5, 10 us【待确认】 | 评估用户态 tick 可行性 |
| migration cost estimator | measured median, measured P99, sweep values | 评估成本敏感性 |
| NUMA | same-NUMA, cross-NUMA | 评估硬件拓扑影响 |

第三阶段论文矩阵：

- 5 seed 或 10 seed robustness，对齐 `run_rescue_robustness` 相关仿真思路。
- estimator sweep：oracle replay upper bound、class mean、EWMA、quantile guard。
- W2 burst/skew 边界：验证 RescueSched 与 NoRescuable/Hybrid 的适用边界。
- target safety stress：append-tail 与 stress 插入策略对 target harm 的影响。
- 两机网络入口：generator-server 分离后看网络 jitter 是否掩盖算法收益。
- trace replay：真实 arrival/service trace 下验证方向性结论。

## 14. 重复实验次数

| 阶段 | 建议重复 | 说明 |
| --- | --- | --- |
| 第一阶段 Demo | 每个方法至少 3 次，先固定 seed=11 | 验证原型稳定性和日志完整性 |
| 第二阶段 | 5 seeds：11, 23, 37, 47, 59；核心点每 seed 1 次，关键点重复 3 次 | 对齐 `include/sim/common/constants.h::SEEDS` |
| 第三阶段 | 关键 claim 使用 10 seeds 或 5 seeds x 3 repeats【待确认】 | 用置信区间支撑论文叙述 |

每次运行必须保存 manifest，包含 commit hash、dirty status、机器信息、CPU 绑定、NUMA、kernel、governor、method、workload、rho、seed、时间戳。

## 15. 结果统计方法

| 统计项 | 方法 | 注意事项 |
| --- | --- | --- |
| latency percentile | 基于 raw `request_log.csv` offline 排序计算 P50/P90/P99/P999 | 不只依赖 runtime histogram |
| SLO violation | `miss_count / total_finished` | warmup 和 measurement 分开 |
| throughput | completed requests / measurement wall time | 同时报 offered QPS 与 completed QPS |
| CPU utilization | `perf stat`、`mpstat`、runtime busy time 三方交叉 | scheduler/control CPU 单独报告 |
| queue length | 按时间窗口计算平均、P95、max queue length | 用于解释调度行为 |
| migration rate | committed migrations / total_generated | 区分 attempt、commit、fail |
| overhead | 每 tick 或每迁移的 scan/eval/commit ns 分布 | 替换仿真固定开销估计 |
| 多次重复 | 报 median、min/max 或 bootstrap 95% CI | 小样本先报告 median 与所有点 |
| 仿真 vs 物理 | 同 workload/rho/seed/method 下对比方向和相对改善 | 不要求绝对数值一致 |

建议新增 `scripts/physical_analyze.py`【需要新增】，读取 physical raw logs 输出与 `write_rescue_header()` 兼容的 `summary.csv`。若字段对齐，`scripts/rescue_analysis.py`【可复用】可部分用于画图；否则新增 adapter。

## 16. 风险点和规避方式

| 风险点 | 影响 | 规避方式 |
| --- | --- | --- |
| 1 us check period 在用户态不稳定 | RescueSched 触发频率与仿真不一致 | 记录 tick jitter；增加 2/5/10 us sensitivity；必要时 busy polling |
| 40 us SLO 过紧 | 计时、锁和调度开销可能主导结果 | 先做 empty-handler 和 single-worker 校准；报告实际 service distribution |
| busy loop service 漂移 | `rho` 和 service class 偏离仿真 | 固定 governor；记录 actual service；用实测均值校准 QPS |
| 并发队列 remove 难 | migration commit 失败或 ABA | request state CAS；只迁移 queued 状态；记录 commit failure |
| logger 干扰 tail latency | P99/P999 被日志 I/O 污染 | per-thread buffer + 异步 flush；measurement 后落盘 |
| NUMA/SMT 干扰 | migration cost 和 queue summary 成本变大 | 固定 core list；same-NUMA 优先；记录 `lscpu -e` |
| 反事实指标不可直接观测 | BMR/UMR 解释不严谨 | 第一阶段不作为主结论；第二阶段标为 estimator-based |
| generator 与 server 抢 CPU | throughput 和 latency 失真 | 单机隔离 core；第二阶段拆到两台机器 |
| c6525-25g 环境未知 | 复现实验不可解释 | `host_inventory.txt` 必须记录硬件、内核、governor、NUMA |
| 物理结果不如仿真 | 算法或实现开销可能抵消收益 | 同时报告 overhead、queue length、migration cost，定位原因 |

## 17. 和原仿真的对齐方式

| 对齐项 | 仿真来源 | 物理实现 | 对齐策略 |
| --- | --- | --- | --- |
| 方法名 | `MethodType`、`method_name()` | physical runtime method enum | 使用同名字符串 |
| workload | `WorkloadType`、`generators.h` | physical workload generator | 使用相同参数，记录实际分布 |
| core 数 | `CORES_PER_HOST=16` | 16 pinned workers | manifest 记录 CPU list |
| warmup/measurement | `WARMUP_REQUESTS`、`MEASUREMENT_REQUESTS` | completed request count gate | 默认沿用，必要时报告 time-based 补充 |
| SLO | `SLO_SHORT_US`、`SLO_LONG_US` | per-request `slo_us` | 使用相同阈值 |
| rho | `lambda_global` 公式 | measured service capacity -> QPS | 报 target rho 和 measured rho |
| RescueSched 参数 | `M0Config` | physical config | 同名字段：`check_period_us,epsilon_us,budget,k,h,cost` |
| summary CSV | `write_rescue_header()` | physical `summary.csv` | 尽量字段兼容 |
| 图表 | `scripts/rescue_analysis.py` | physical summary 或 adapter | 先复用 `fig_rescue_slo_vs_rho` 的数据形状 |
| 不可直接对齐 | oracle、BMR/UMR、target-induced miss、估算 overhead | offline estimator 或实测替代 | 在图注和论文中明确口径 |

物理机结果不要求绝对 latency 与仿真一致。第一阶段只要求方向性和机制可解释：相同 workload 和相同负载点下，RescueSched 是否改善 SLO/tail latency，以及改善是否伴随可接受的迁移率和控制面开销。

## 18. 后续可以写进论文的实验叙述

可写入论文的方法叙述：

- We implement RescueSched as a user-space scheduler on a 16-worker single-node runtime, preserving the simulator's rescuability filters: locally doomed, remote feasible, and target safe.
- The physical prototype uses the same W3 Poisson-lognormal workload parameters, SLO thresholds, warmup length, measurement length, and method names as the simulator.
- We report raw measured latency, SLO violation, throughput, CPU utilization, queue lengths, migration counts, and scheduler overhead; counterfactual migration-quality metrics are reported only as estimator-based diagnostics.

可写入论文的实验主线：

- 第一阶段：single-node heavy-tail experiment，验证 RescueSched 在真实用户态队列中是否降低 SLO violation 和 P99。
- 第二阶段：sensitivity study，检查 check period、migration cost、service estimator 和 NUMA 对收益的影响。
- 第三阶段：robustness and boundary，使用多 seed、W2 burst、NoTargetSafety、NoRescuable、trace replay 说明算法适用边界。

限制需要如实写入：

- 当前物理原型不修改 Linux 内核，因此结果代表用户态可实现性，不代表内核调度器级最优实现。
- 物理机中的 `beneficial_migration_ratio` 和 `useless_migration_ratio` 不是纯实测指标，必须标注为 counterfactual estimator-based。
- 单机 intra-host 结果不能直接外推到 64 host 或 25G 跨机迁移；跨机实验需要单独校准网络和 RPC 成本。

## 推荐目录结构

建议在后续实现时采用如下结构；本轮只生成文档，不创建这些目录。

```text
configs/
  rescuesched.yaml                 # 【可复用】现有仿真配置
  physical/
    demo_w3_single_node.yaml       # 【需要新增】第一阶段物理 Demo 配置
    sweep_w3_rho.yaml              # 【需要新增】rho sweep
    numa_sensitivity.yaml          # 【需要新增】NUMA 实验

scripts/
  rescue_analysis.py               # 【可复用】仿真图表与部分 summary 分析
  physical_run.sh                  # 【需要新增】启动 physical runtime
  physical_collect_inventory.sh    # 【需要新增】采集 lscpu/numactl/kernel/governor
  physical_analyze.py              # 【需要新增】raw log -> summary.csv
  physical_plot.py                 # 【需要新增】physical-vs-sim 图表

src/
  app/
    main.cpp                       # 【可复用】仿真入口和 CSV schema 参考
  core/
    simulator.cpp                  # 【可复用】算法决策参考，不直接作为 runtime
  physical/                        # 【需要新增】
    main_physical.cpp
    runtime/
      request.h
      worker.h
      scheduler.h
      queue.h
      workload.h
      metrics.h
      affinity.h

results/
  physical/
    <date>/<run_id>/
      run_manifest.json            # 【需要新增】
      run_config.yaml
      host_inventory.txt
      request_log.csv
      scheduler_log.csv
      decision_log.csv
      migration_log.csv
      queue_sample_log.csv
      overhead_log.csv
      perf_stat.txt
      summary.csv

logs/
  physical/
    smoke/
    sweeps/

docs/
  03-physical-mapping.md           # 【可复用】变量/指标映射
  04-experiment-design.md          # 本文档
  05-implementation-plan.md        # 后续实现路线
  06-test-record.md                # 记录真实实验结果
  07-result-analysis.md            # 后续结果分析
```
