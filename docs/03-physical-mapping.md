# 03 Physical Mapping

更新时间：2026-07-06

目标：把当前 RescueSched 仿真中的抽象变量、算法输入、系统假设和评价指标，映射为真实物理机 `c6525-25g` 实验中的可控参数和可测指标。本文基于 `docs/02-simulation-analysis.md`、`include/sim/common/constants.h`、`include/sim/common/types.h`、`include/sim/workloads/generators.h`、`src/core/simulator.cpp`、`include/sim/metrics/stats.h`、`src/app/main.cpp` 和 `scripts/rescue_analysis.py`。`c6525-25g` 的具体 CPU 型号、NUMA 拓扑、SMT 状态、内核版本和 NIC 配置目前未在项目中记录，统一标记为【待确认】。

优先级说明：P0 表示第一阶段最小 Demo 必须复现；P1 表示第一阶段可记录或第二阶段必须复现；P2 表示完整实验或论文实验再复现；P3 表示当前物理机阶段暂不建议优先实现。

## 一、变量映射表

| 仿真变量 | 代码位置 | 含义 | 物理机对应物 | 控制方式 | 测量方式 | 推荐工具 | 误差来源 | 复现优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `MethodType` | `include/sim/common/types.h`，`src/app/main.cpp::method_name()` | 实验方法枚举，包括 `L0_RANDOM_CORE`、`L1_WORK_STEALING`、`M0_INTRA_HOST_PROACTIVE`、`M1_RESCUE_SCHED` 等 | 运行时调度策略开关 | 通过启动参数或配置选择 worker 调度策略 | run manifest 记录 `method` | 自研 runtime 参数、JSON 或 CSV manifest | 同一代码路径下不同策略可能共享队列实现，需避免额外差异 | P0 |
| `WorkloadType::W3_POISSON_LOGNORMAL` | `include/sim/common/types.h`，`include/sim/workloads/generators.h`，`src/core/simulator.cpp::configure()` | RescueSched 主线 workload，Poisson 到达加 lognormal 服务时间 | c6525-25g 上的合成请求流与长尾 handler | generator 按指数间隔发请求，handler 按 lognormal 分布 busy work | 每请求 `arrival_ts`、`start_ts`、`finish_ts`、`target_service_us`、`actual_service_us` | 自研 load generator、`clock_gettime` 或 TSC 校准、Python 聚合 | busy work 与真实服务时间不完全一致，尾部分布样本不足，CPU 频率漂移 | P0 |
| `WorkloadType::W2_MMPP_BIMODAL` | `include/sim/workloads/generators.h::MMPPArrival`，`src/core/simulator.cpp::pick_w2_intra_core()` | bursty 到达，服务时间为 bimodal | 二状态 burst 请求流，可加入热点 core 偏斜 | generator 维护 normal/burst 状态和热点派发概率 | 时间窗口到达率、状态切换日志、core 分布日志 | 自研 generator、Python 分布检验 | 状态切换时间、OS 抖动、热点派发与真实负载偏斜不一致 | P2 |
| `WorkloadType::W1_POISSON_BIMODAL` | `include/sim/common/types.h`，`include/sim/workloads/generators.h::BimodalService` | Poisson 到达，80% 短任务加 20% 长任务 | 合成短长 handler 混合流 | generator 按概率选择短长 handler | service class 计数、短长任务 latency | 自研 generator、日志聚合 | 短任务 5 us 在物理机上容易被计时和调度开销淹没 | P1 |
| `rho` | `src/core/simulator.cpp::configure()`，`lambda_global = rho * effective_capacity_ / mean_service_us` | 系统负载强度 | 实际 offered load，即 QPS 除以实测服务能力 | 先用单 worker 校准服务能力，再按目标 `rho` 设置 inter-arrival | 发送端请求数、接收端完成数、每 worker busy time | generator 日志、`perf stat`、CPU 利用率、runtime summary | 服务能力随频率、cache、NUMA 和温度变化；`rho` 只能近似控制 | P0 |
| `seed` | `include/sim/common/constants.h::SEEDS`，`src/core/simulator.cpp::configure()` | 随机重复实验编号 | generator seed、service-time seed、worker 分配 seed | manifest 固定 seed，并为 arrival 与 service 使用可复现 RNG | run manifest、trace header | 程序日志、JSON manifest | 多线程执行顺序不可完全确定，物理机不能保证 bitwise reproducible | P0 |
| `NUM_HOSTS = 64` | `include/sim/common/constants.h` | 仿真集群 host 数 | 物理节点数或进程组数 | CloudLab node 分配或多进程部署 | host manifest、node inventory | CloudLab profile、`hostname`、`lscpu` | 第一阶段 c6525-25g 单机无法映射 64 host；跨机网络拓扑差异大 | P3 |
| `CORES_PER_HOST = 16` | `include/sim/common/constants.h`，`src/core/simulator.cpp::configure()` | 每 host 16 个 core | c6525-25g 上 16 个 pinned worker core | `taskset`、`pthread_setaffinity_np`、`numactl` 绑定 16 个物理 core | worker 启动日志、`/proc/<pid>/task/*/status`、`lscpu` | `lscpu`、`numactl --hardware`、`hwloc-ls` | SMT sibling、NUMA 跨 socket、频率和中断干扰；c6525-25g 实际拓扑【待确认】 | P0 |
| `active_host_count_` | `src/core/simulator.cpp::configure()` | intra-host 方法只启用 1 个 host，跨 host legacy 方法启用 64 host | 第一阶段单机单进程多 worker | 只复现 intra-host 方法，禁用跨 host 迁移 | manifest 记录 `host_count=1`、`worker_count=16` | runtime manifest | 仿真中的单 host 是理想 host，物理机单 host 有 NUMA、IRQ、cache 层级 | P0 |
| `ClusterProfile::HOMOGENEOUS` | `include/sim/common/types.h`，`src/core/simulator.cpp::compute_effective_capacity()` | 同构 capacity 模型 | 同型号同配置 worker core | 固定同一 NUMA node 或同一 socket 的物理 core | per-worker throughput 校准 | `lscpu`、`numactl`、microbench | 不同 core 频率与热状态不完全同构 | P1 |
| `ClusterProfile::HETERO_25PCT` | `include/sim/common/types.h`，`include/sim/common/constants.h` | 48 个快节点加 16 个 0.2 capacity 慢节点 | 限速 worker 或不同节点 | cgroup CPU quota、插入额外 busy delay、异构节点 | per-worker service rate | cgroup v2、`perf stat`、runtime 校准 | 人为限速与真实慢节点行为不同 | P3 |
| `WARMUP_REQUESTS = 200000` | `include/sim/common/constants.h`，`src/core/simulator.cpp::configure()`，`MetricsCollector::init()` | warmup 完成数，warmup 后才记录指标 | 物理实验 warmup 边界 | 完成 200000 个请求后进入 measurement，或用等价时间窗口【待确认】 | request log 中 `phase=warmup` 或 `phase=measure` | runtime 日志、Python 聚合 | 物理机热状态和队列残留会跨越边界，warmup 数量可能需重新校准 | P0 |
| `MEASUREMENT_REQUESTS = 1000000` | `include/sim/common/constants.h`，`src/core/simulator.cpp::configure()` | measurement 完成数 | 物理实验采样窗口 | measurement 完成 1000000 个请求后停止 | `total_finished`、request log 行数 | runtime summary、Python 聚合 | P999 需要更大样本；丢请求和超时口径需固定 | P1 |
| `SLO_SHORT_US = 40.0` | `include/sim/common/constants.h`，`include/sim/model/task.h` | 短任务或 W3 mice 任务 SLO | request deadline 阈值 | 运行参数固定 40 us，按 request class 计算 deadline miss | per-request `latency_us` 与 `slo_us` | runtime log、Python 聚合 | 物理机 40 us 极紧，计时、队列锁和调度开销会占比较大 | P0 |
| `SLO_LONG_US = 200.0` | `include/sim/common/constants.h`，`include/sim/model/task.h` | 长任务或 elephant 任务 SLO | 长任务 deadline 阈值 | 运行参数固定 200 us | per-request `latency_us` 与 `slo_us` | runtime log、Python 聚合 | 长任务实际 service-time 若偏离 100 us 或 lognormal 尾部，miss 口径变化 | P1 |
| `SLO_SHORT_SERVICE_THRESHOLD_US = 20.0` | `include/sim/common/constants.h`，`include/sim/model/task.h` | W3 中区分 mice 与 elephant 的服务时间阈值 | 按目标 service 或实测 service 对请求分类 | generator 在发起时记录 `service_class`，不要事后用 latency 分类 | request log `class=mice/elephant` | runtime log | 实测 service-time 可能被抢占拉长，分类应基于目标 service 还是实际 service【待确认】 | P1 |
| `BIMODAL_SHORT_US = 5.0` | `include/sim/common/constants.h`，`include/sim/workloads/generators.h::BimodalService` | W1/W2 短任务服务时间 | 短 handler 目标 CPU work | TSC 校准 busy loop，避免 `sleep` | handler 内 `service_start_ts`、`service_finish_ts` | TSC、`perf stat`、microbench | 5 us 量级易受函数调用、锁、cache miss 和中断影响 | P1 |
| `BIMODAL_LONG_US = 100.0` | 同上 | W1/W2 长任务服务时间 | 长 handler 目标 CPU work | TSC 校准 busy loop | handler service histogram | TSC、runtime log | CPU 频率、NUMA 和抢占导致实际时间分布变宽 | P1 |
| `BIMODAL_SHORT_PROB = 0.80` | 同上 | W1/W2 短任务概率 | 短长请求比例 | generator 按 seed 抽样 | class count | runtime log、Python 检验 | 小样本偏差，worker 派发偏斜 | P1 |
| `W3_LOGNORMAL_MU = 2.6782363` | `include/sim/common/constants.h`，`include/sim/workloads/generators.h::LognormalService` | W3 lognormal 服务分布位置参数 | long-tail handler 目标 service 抽样参数 | generator 用相同参数抽样目标 service_us | target service histogram 和实际 service histogram | runtime log、Python 分布检验 | busy loop 截断、CPU 抢占会改变实际尾部 | P0 |
| `W3_LOGNORMAL_SIGMA = 1.0` | 同上 | W3 lognormal 服务分布形状参数 | long-tail 程度 | 同上 | 同上 | 同上 | 极端尾部样本不足，P999 不稳定 | P0 |
| `W3_MEAN_SERVICE_US = 24.0` | `include/sim/common/constants.h`，`src/core/simulator.cpp::mean_service_time()` | W3 平均服务时间，用于计算 `lambda` | 单请求平均目标 service 与实测 service | target service 固定分布，实测均值用于修正 QPS | service histogram | runtime log、Python 聚合 | 仿真用目标均值，物理机应用实测均值校准 `rho` | P0 |
| `W2_LAMBDA_BURST_FACTOR = 1.5` | `include/sim/common/constants.h`，`include/sim/workloads/generators.h::MMPPArrival` | W2 burst 状态到达率倍数 | burst QPS 与 normal QPS 比例 | generator 状态机切换 QPS | 时间窗口请求数 | generator log | 定时精度和 backpressure 会扭曲到达率 | P2 |
| `W2_NORMAL_STAY_US = 5000.0` | 同上 | W2 normal 状态平均停留时间 | normal 阶段持续时间 | generator 指数抽样状态停留时间 | state transition log | generator log | us 级状态切换在用户态可能有 jitter | P2 |
| `W2_BURST_STAY_US = 500.0` | 同上 | W2 burst 状态平均停留时间 | burst 阶段持续时间 | 同上 | 同上 | 同上 | 500 us burst 对 timer 和发送线程要求高 | P2 |
| `w2_hot_core_count` | `include/sim/common/constants.h::M0Config`，`src/core/simulator.cpp::pick_w2_intra_core()` | W2 intra-host 热点 core 数，默认 4 | 热点 worker 数 | generator 或 dispatcher 将请求偏向指定 worker | per-worker arrival count | runtime log | 热点与真实线程队列竞争不完全一致 | P2 |
| `w2_hot_dispatch_prob` | 同上 | W2 派发到热点 core 的概率，默认 0.5 | 热点派发概率 | dispatcher 按概率选择热点或全局随机 | per-worker arrival count | runtime log | 多线程 backpressure 会改变实际派发比例 | P2 |
| `M0Config::t_check_us` | `include/sim/common/constants.h::M0Config`，`src/core/simulator.cpp::schedule_checks()` | 迁移检查周期，默认 1 us | scheduler tick 或 polling interval | 专用调度线程 busy polling，或每个 worker 周期性检查 | tick timestamp、tick jitter、missed ticks | runtime log、`perf sched` | 用户态 1 us 周期难以严格保证，调度线程会消耗 CPU | P0 |
| `INTRA_PROACTIVE_SCAN_DEPTH = 32` | `include/sim/common/constants.h`，`src/core/simulator.cpp::run_intra_proactive_check()` | M0 proactive 每轮扫描源队列深度 | M0 baseline 队列扫描上限 | runtime 参数 | scheduler decision log | runtime log | 并发队列扫描成本和锁争用 | P1 |
| `RESCUE_SCAN_DEPTH = 64` | `include/sim/common/constants.h`，`src/core/simulator.cpp::run_rescue_sched_check()` | RescueSched 源队列扫描深度 | 每轮最多查看 queued tasks 数 | runtime 参数，扫描 bounded queue prefix | `decision_log.scan_depth` | runtime log | 真实队列快照可能陈旧，扫描会引入锁或 cache 成本 | P0 |
| `RESCUE_K_CANDIDATES = 16` | 同上 | 每个源 core 候选上限 | 候选任务池大小 | runtime 参数 | `candidate_count` | runtime log | 排序和候选构建成本未被仿真准确建模 | P0 |
| `RESCUE_H_TARGETS = 4` | 同上 | 每轮候选目标 core 数 | 目标 worker 数 | runtime 参数，取 workload 最低或随机子集【待确认】 | `target_count`、target workload snapshot | runtime log | 目标队列状态可能陈旧，跨 NUMA 读取更贵 | P0 |
| `RESCUE_BUDGET_PER_CHECK = 1` | 同上 | 每轮检查最多迁移任务数 | 每 tick 最大跨队列 handoff 数 | runtime 参数，CAS 或锁控制提交预算 | migration log | runtime log | 并发迁移可能超预算或失败，需要记录 attempted 与 committed | P0 |
| `RESCUE_EPSILON_US = 2.0` | 同上 | remote feasible 的安全 slack | 迁移后仍满足 SLO 的 slack 阈值 | runtime 参数 | decision log 中 `predicted_remote_latency_us` 与 `slo_us` | runtime log | service estimator 误差和 tick jitter 会吞掉 2 us slack | P0 |
| `RESCUE_THETA = 0` | 同上 | 目标风险允许增量 | target-side risk guard 阈值 | runtime 参数 | target-safety decision log | runtime log | 目标风险是模型估计，真实系统归因困难 | P1 |
| `RESCUE_MIGRATION_COST_US = 0.5` | `include/sim/common/constants.h`，`src/core/simulator.cpp::run_rescue_sched_check()` | 仿真中扣除的迁移成本 | descriptor handoff、跨队列 enqueue、cache miss、可能的 NUMA 成本 | 用 microbench 校准，再作为 runtime estimator 参数 | handoff latency distribution | 自研 microbench、`perf stat -e cache-misses` | 仿真固定 0.5 us，真实成本随队列长度、锁竞争、NUMA 改变 | P0 |
| `service_estimate_mode` | `include/sim/common/constants.h::M0Config`，`src/core/simulator.cpp::estimate_service_time()` | 服务时间估计模式，包括 oracle、mean、class mean、EWMA、quantile guard | runtime 对未完成请求服务时间的预测器 | 第一阶段避免 oracle，优先 `class_mean` 或 `ewma`；oracle 只作为 trace replay 上界 | estimator log：`predicted_service_us`、`actual_service_us` | runtime log、offline replay | 真实系统无法知道未来真实 service time，oracle 不可直接复现 | P0 for non-oracle，P2 for oracle comparison |
| `service_estimate_noise_cv` | `include/sim/common/constants.h::M0Config`，`src/core/simulator.cpp::estimate_service_time()` | noisy oracle 的噪声系数 | 人为估计误差注入 | runtime 参数或 offline replay 参数 | prediction error histogram | Python analysis | 物理系统误差分布可能不是高斯或 lognormal【待确认】 | P2 |
| `service_estimate_ewma_alpha` | 同上 | EWMA 服务时间估计更新系数，默认 0.05 | per-class 或 per-handler EWMA 更新率 | runtime 参数 | estimator state dump | runtime log | 非平稳 workload 下 EWMA 滞后 | P1 |
| `rescue_target_insert_policy` | `include/sim/common/constants.h`，`src/core/simulator.cpp::move_rescue_task_intra_host()` | 迁移任务插入目标队列尾部或 stress 头部 | 真实队列插入策略 | 第一阶段用 append-tail；stress 策略只用于 target harm 实验 | enqueue position log | runtime log | 队列实现不同会改变 target-side harm | P1 |
| `target_safety_enabled` | `src/app/main.cpp::method_has_target_safety()`，`src/core/simulator.cpp::run_rescue_sched_check()` | `M1_RESCUE_NO_TARGET_SAFETY` ablation 开关 | 是否启用目标安全过滤 | method 参数选择 | decision log 中 unsafe reject 计数 | runtime log | 真实 target risk 难以准确估计 | P1 |
| `rescuable_filter_enabled` | `src/app/main.cpp::method_has_rescuable_filter()`，`src/core/simulator.cpp::run_rescue_sched_check()` | `M1_RESCUE_NO_RESCUABLE` ablation 开关 | 是否要求 locally doomed 与 remote feasible | method 参数选择 | decision log 中 locally doomed 与 remote feasible 计数 | runtime log | 反事实本地完成时间是估算值，不是实测值 | P1 |
| `T_host_us = 2.1` | `include/sim/common/constants.h`，`src/core/simulator.cpp::compute_exec_time()` | 仿真固定 host 内处理开销 | runtime 固定框架开销、queue pop、handler dispatch | 第一阶段不单独控制，包含在实测 latency 或 service 中 | empty handler baseline | microbench、`perf stat` | 仿真加法开销与真实 pipeline 不同 | P2 |
| `T_net_oneway_us = 3.15`，`T_rpc_us = 6.3` | `include/sim/common/constants.h` | 跨 host 网络和 RPC 开销 | c6525-25g 25G 网络 RTT 或 one-way latency【待确认】 | 第二阶段用 ping-pong microbench 校准 | RTT、tail RTT、packet loss | `iperf3`、`sockperf`、自研 RPC ping-pong | 第一阶段单机不涉及，跨机 latency 受交换机和 kernel network stack 影响 | P3 |

## 二、指标映射表

| 仿真指标 | 代码位置 | 含义 | 物理机测量方法 | 日志格式 | 是否可直接对齐 | 注意事项 |
| --- | --- | --- | --- | --- | --- | --- |
| `P99_us` | `include/sim/metrics/stats.h::p99()`，`src/app/main.cpp::write_rescue_header()` | measurement 窗口内 request latency 第 99 百分位 | request 完成时记录端到端 latency，offline percentile | `request_log.csv`: `run_id,phase,request_id,class,arrival_ts,start_ts,finish_ts,latency_us,slo_us,method,worker_id` | 可直接对齐 | 必须和仿真一样排除 warmup；时钟建议单进程内单调时钟，跨机需同步【待确认】 |
| `P999_us` | `include/sim/metrics/stats.h::p999()` | request latency 第 99.9 百分位 | 同 `P99_us`，但需要足够样本 | 同上 | 可部分对齐 | 1000000 completed 只有约 1000 个 top 0.1% 样本，物理噪声下置信区间需报告 |
| `slo_violation_rate` | `include/sim/metrics/stats.h::slo_violation_rate()` | `slo_violations / total_finished` | 每请求比较 `latency_us > slo_us` 后聚合 | `summary.csv`: `slo_violation_rate,total_finished`；`request_log.csv`: `miss=0/1` | 可直接对齐 | SLO 起点必须是 arrival/enqueue 时间，不是 handler start 时间 |
| `short_slo_violation_rate`，`long_slo_violation_rate` | `include/sim/metrics/stats.h`，legacy CSV header | W1/W2 短长任务 miss 率 | 按 generator class 聚合 miss | `request_log.csv`: `class=short/long` | 可直接对齐 | W3 主实验不应混用 short/long 与 mice/elephant 口径 |
| `mice_slo_violation_rate`，`elephant_slo_violation_rate` | `include/sim/metrics/stats.h`，legacy CSV header | W3 mice/elephant miss 率 | 按 `target_service_us <= 20` 或配置阈值分类聚合 | `request_log.csv`: `class=mice/elephant,target_service_us` | 可部分对齐 | 分类应基于目标 service 还是实测 service 需在 manifest 固定 |
| `total_generated` | `src/app/main.cpp::write_rescue_row()`，`Simulator::total_generated()` | 生成请求总数 | generator 发出的 request 数 | `summary.csv`: `total_generated`；`request_log.csv` 行数 | 可直接对齐 | 如果物理实验存在 drop 或 backpressure，需要单独记录 submitted 与 accepted |
| `total_finished` | `include/sim/metrics/stats.h`，`write_rescue_row()` | measurement 窗口完成请求数 | worker 完成并记录的 request 数 | `summary.csv`: `total_finished` | 可直接对齐 | 超时但最终完成的请求仍应计入完成；未完成请求口径【待确认】 |
| `migration_rate` | `include/sim/metrics/stats.h::migration_rate()` | `total_migrations / total_generated` | 统计 committed migration 或 handoff 次数除以 generated | `migration_log.csv`: `request_id,src_worker,dst_worker,commit_ts`；`summary.csv`: `migration_rate` | 可直接对齐 | 需区分 attempted、failed、committed，仿真通常按成功移动计 |
| `intra_move_rate` | `include/sim/metrics/stats.h::intra_move_rate()` | intra-host move 数除以 generated | 同上，仅统计同进程同节点 worker 间 handoff | `summary.csv`: `intra_move_rate,intra_move_count` | 可直接对齐 | 第一阶段只应使用 intra move，不引入网络迁移 |
| `intra_move_count` | `write_rescue_row()` | intra-host move 次数 | committed handoff 计数 | `migration_log.csv` 与 `summary.csv` | 可直接对齐 | 并发重复迁移同一 request 必须由 request state 防止 |
| `intra_moved_work_us` | `write_rescue_row()`，`MetricsCollector` | 被移动任务的估计工作量总和 | 迁移任务的 `predicted_service_us` 或 `target_service_us` 求和 | `migration_log.csv`: `predicted_service_us,target_service_us` | 可部分对齐 | 仿真用模型 service，物理机若用实际 service 会变成事后指标 |
| `invalid_migration_ratio` | `include/sim/metrics/stats.h::invalid_migration_ratio()` | legacy 跨 host 无效迁移比例 | 需要定义物理 invalid 语义后统计 | `migration_log.csv`: `predicted_local_latency_us,actual_latency_us` | 不可直接对齐 | 当前 RescueSched 主表重点应看 `useless_migration_ratio`，legacy invalid 口径需【待确认】 |
| `invalid_intra_move_ratio` | `include/sim/metrics/stats.h::invalid_intra_move_ratio()` | M0 proactive 的无效 intra move 比例 | 迁移后实际 latency 是否超过预测本地 latency | `migration_log.csv`: `predicted_local_latency_us,actual_latency_us` | 可部分对齐 | 反事实本地 latency 不可真实观测，只能用当时快照估计 |
| `steal_attempt_count`，`steal_success_count`，`stolen_task_count` | `src/core/simulator.cpp::steal_one_task()`，`write_rescue_header()` | work stealing baseline 的尝试、成功与被偷任务数 | idle worker 尝试从其他队列 pop 的次数、成功次数 | `scheduler_log.csv`: `event=steal_attempt/steal_success` | 可直接对齐 | 真实 stealing 会有锁竞争和 cache 成本，需记录失败原因 |
| `proactive_intra_attempt_count`，`proactive_intra_success_count` | `src/core/simulator.cpp::run_intra_proactive_check()` | M0 proactive 检查和移动成功数 | scheduler tick 中 M0 扫描与 commit 计数 | `scheduler_log.csv`: `event=m0_check/m0_move` | 可直接对齐 | tick 实际周期必须记录，否则 attempt count 不可比 |
| `rescue_attempt_count` | `src/core/simulator.cpp::run_rescue_sched_check()`，`MetricsCollector::on_rescue_attempt()` | RescueSched 检查尝试数 | 每次 rescue scheduler tick 或每个 source 检查计数 | `scheduler_log.csv`: `event=rescue_check,ts,host,worker` | 可直接对齐 | 物理 tick jitter 会改变尝试次数；需同时记录 elapsed time |
| `rescue_candidate_count` | `run_rescue_sched_check()` | 被纳入 rescue 候选池的任务数 | 每轮扫描后 candidate 数 | `decision_log.csv`: `candidate_count` | 可直接对齐 | 队列快照并发变化可能导致候选失效 |
| `locally_doomed_count` | `run_rescue_sched_check()` | 预测本地继续排队会 miss 的候选数 | 按当前队列快照和 estimator 预测 local miss | `decision_log.csv`: `request_id,predicted_local_latency_us,slo_us,locally_doomed` | 可部分对齐 | 这是模型判断，不是物理直接测量；真实 local counterfactual 不可观测 |
| `remote_feasible_count` | `run_rescue_sched_check()` | 预测迁移到目标仍可满足 SLO 的候选数 | 按目标队列快照、迁移成本和 estimator 预测 remote latency | `decision_log.csv`: `predicted_remote_latency_us,remote_feasible` | 可部分对齐 | 目标队列可能在 commit 前变化，真实 remote latency 包含锁和 cache 成本 |
| `target_safe_count` | `run_rescue_sched_check()` | 通过目标安全过滤的候选数 | 预测对目标队列已有任务不会造成超额风险 | `decision_log.csv`: `target_safe,target_risk_before,target_risk_after` | 可部分对齐 | target-side harm 是模型估算，物理归因需要额外 watch log |
| `rescue_success_count` | `write_rescue_row()`，`MetricsCollector::on_rescue_success()` | RescueSched 成功迁移任务数 | committed rescue handoff 数 | `migration_log.csv`: `event=rescue_commit`；`summary.csv` | 可直接对齐 | 第一阶段应作为核心执行指标之一 |
| `rescue_moved_work_us` | `write_rescue_row()` | RescueSched 移动任务工作量总和 | committed rescue 的目标或预测 service 求和 | `migration_log.csv`: `target_service_us,predicted_service_us` | 可部分对齐 | 与 `intra_moved_work_us` 一样受 service 定义影响 |
| `target_unsafe_reject_count` | `run_rescue_sched_check()` | 因 target safety 失败拒绝的候选数 | scheduler 决策日志中的拒绝原因 | `decision_log.csv`: `reject_reason=target_unsafe` | 可部分对齐 | 拒绝原因来自模型，非硬件实测 |
| `remote_infeasible_reject_count` | `run_rescue_sched_check()` | 因 remote infeasible 拒绝的候选数 | scheduler 决策日志中的拒绝原因 | `decision_log.csv`: `reject_reason=remote_infeasible` | 可部分对齐 | 依赖 estimator 和 migration cost 校准 |
| `needless_migration_count` | `MetricsCollector::on_rescue_finish()` | 本地预测并不需要 rescue 或实际无收益的迁移计数 | 需要记录预测本地结果与实际结果后 offline 判定 | `migration_log.csv`: `predicted_local_latency_us,actual_latency_us,slo_us` | 不可直接对齐 | 属于反事实近似，真实未迁移路径不可观测 |
| `unsaved_migration_count` | `MetricsCollector::on_rescue_finish()` | rescue 后仍 miss 的迁移计数 | 迁移请求完成后若 `actual_latency_us > slo_us` 则计数 | `migration_log.csv`: `actual_latency_us,slo_us,miss` | 可直接对齐 | 不代表迁移无效，因为未迁移可能更差；解释需谨慎 |
| `beneficial_migration_count` | `MetricsCollector::on_rescue_finish()` | 预测本地会 miss 且迁移后满足 SLO 的计数 | 用预测 local counterfactual 与实际 remote 完成结果组合 | `migration_log.csv`: `predicted_local_miss,actual_miss` | 可部分对齐 | 一半是模型估计，一半是实测；不能写成纯实测 |
| `beneficial_migration_ratio` | `include/sim/metrics/stats.h::beneficial_migration_ratio()` | `beneficial_migration_count / rescue_success_count` | 同上聚合 | `summary.csv`: `beneficial_migration_ratio` | 可部分对齐 | 论文中需明确为 estimator-based quality metric |
| `harmful_migration_count` | `write_rescue_header()` | 仿真中曾包含预测 target unsafe 等 harm 信号 | 物理机不建议直接复用旧口径 | 【待确认】 | 不建议直接对齐 | `docs/02` 已指出 harmful 语义需区分 predicted 与 actual |
| `predicted_target_unsafe_accept_count` | `write_rescue_header()` | 接受了模型预测 target unsafe 的迁移数 | 若禁用 safety，可记录预测 unsafe 但仍 commit 的次数 | `decision_log.csv`: `target_safe=0,committed=1` | 可部分对齐 | 这是模型预测，不是实际 harm |
| `target_harm_watch_count` | `write_rescue_header()`，`src/core/simulator.cpp::move_rescue_task_intra_host()` | 对目标队列任务建立 harm watch 的数量 | 记录迁移时目标队列受影响任务集合 | `target_watch_log.csv`: `migration_id,target_request_id,watch_start_ts` | 可部分对齐 | 真实队列中被影响对象定义复杂，append-tail 下可能无直接前序 harm |
| `harmful_actual_count`，`harmful_actual_ratio` | `include/sim/metrics/stats.h::harmful_actual_ratio()` | 实际诱发 target-side miss 的计数与比例 | 对 watch 集合内请求比较迁移前预测与迁移后实际 miss | `target_watch_log.csv`: `target_request_id,induced_miss=0/1` | 可部分对齐 | target-induced miss 归因高度依赖模型和 watch 范围 |
| `target_induced_miss_actual` | `write_rescue_header()` | 迁移导致目标任务实际 miss 的计数 | 目标队列 watch 加 offline 归因 | `target_watch_log.csv` 与 `request_log.csv` join | 可部分对齐 | 第一阶段不建议作为核心结论，先记录原始数据 |
| `useless_migration_ratio` | `include/sim/metrics/stats.h::useless_migration_ratio()` | `(needless + unsaved + harmful_actual) / rescue_success_count` | 由迁移后 miss 与 counterfactual 估计合成 | `summary.csv`: `useless_migration_ratio` | 可部分对齐 | 包含不可观测反事实和归因项，只能作为近似质量指标 |
| `rescue_per_migration` | `include/sim/metrics/stats.h::rescue_per_migration()` | 当前代码直接返回 `beneficial_migration_ratio()` | 不建议作为独立物理指标 | 同 `beneficial_migration_ratio` | 不建议直接对齐 | 名称易误导；物理实验可保留字段但注明等价于 BMR |
| `relief_attempt_count` 等 hybrid 指标 | `write_rescue_header()`，`src/core/simulator.cpp::run_hybrid_relief_check()` | `M1_RESCUE_HYBRID` 的 relief fallback 指标 | Hybrid 策略的额外迁移尝试与结果 | `scheduler_log.csv`: `event=relief_*` | P2 后可对齐 | W3 第一阶段不需要；W2 burst 边界再启用 |
| `summary_update_cost_est_us` | `include/sim/metrics/stats.h::summary_update_cost_est_us()` | 按计数乘固定 0.02 us 的模型估算开销 | 真实 summary 更新耗时 | scheduler 代码段计时或 perf event | `overhead_log.csv`: `summary_update_ns` | 不可直接对齐 | 必须替换为实测时间，不能沿用固定系数 |
| `batch_estimation_cost_est_us` | `include/sim/metrics/stats.h::batch_estimation_cost_est_us()` | 按候选数乘固定 0.03 us 的模型估算开销 | 真实候选评估耗时 | 包围候选构建和打分代码段计时 | `overhead_log.csv`: `candidate_eval_ns,candidate_count` | 不可直接对齐 | 受编译优化、cache、锁竞争影响 |
| `target_selection_cost_est_us` | `include/sim/metrics/stats.h::target_selection_cost_est_us()` | 按固定 0.005 us 估算 target selection 成本 | 真实目标选择耗时 | 包围 target summary 和选择代码段计时 | `overhead_log.csv`: `target_select_ns,target_count` | 不可直接对齐 | 当前估算过于理想，物理机必须实测 |

## 三、算法模块映射表

| 仿真模块 | 代码位置 | 当前功能 | 物理机复现时是否保留 | 需要替换为什么 | 修改难度 |
| --- | --- | --- | --- | --- | --- |
| CLI 与实验模式 | `src/app/main.cpp`，`config/rescuesched.yaml` | 选择 `rescue-smoke`、`rescue-main` 等模式，写 CSV 输出 | 保留实验命名、参数 schema 和 CSV 字段 | 新增或另写 physical runner，读取同类配置并启动真实 worker | 中 |
| 离散事件仿真引擎 | `src/core/simulator.cpp::run()`，`process_event()` | 用 priority queue 推进虚拟时间和事件 | 不保留执行机制 | 替换为真实时间 runtime、worker 线程、真实队列和调度线程 | 高 |
| `Task` 数据结构 | `include/sim/model/task.h` | 保存 request id、arrival、service、SLO、迁移标记 | 保留字段语义 | 转为 runtime request descriptor，增加 atomic state、timestamps、trace ids | 中 |
| `Core` 与 `TaskQueue` | `include/sim/model/core.h`，`include/sim/model/task_queue.h` | 仿真 core 队列、队列工作量估计、remove/append | 保留抽象，不保留实现 | 替换为真实 MPSC 或 locked deque，支持 bounded scan、remove queued task、append-tail | 高 |
| `Node` 模型 | `include/sim/model/node.h` | host 内 core 集合和 shortest queue 查询 | 第一阶段保留为单节点 worker group 概念 | runtime worker registry 和 per-worker queue summary | 中 |
| workload generator | `include/sim/workloads/generators.h` | Poisson、MMPP、bimodal、lognormal 抽样 | 保留分布逻辑 | 替换为真实 load generator 和 handler calibration；抽样参数保持一致 | 中 |
| 服务时间估计器 | `src/core/simulator.cpp::estimate_service_time()` | oracle、mean、noisy oracle、class mean、EWMA、quantile guard | 保留 non-oracle 策略逻辑 | oracle 只能用于 trace replay 上界；物理机默认使用 class mean 或 EWMA | 中 |
| L0 RandomCore | `src/core/simulator.cpp::enqueue_task_on_random_core()` | 新任务随机进入 core 队列 | 保留 baseline 语义 | dispatcher 随机选择 worker queue | 低 |
| L1 WorkStealing | `src/core/simulator.cpp::steal_one_task()` | idle core 从其他 core 队列 steal | 保留 baseline 语义 | worker 空闲时尝试真实队列 steal，并记录失败原因 | 中 |
| M0 IntraHost Proactive | `src/core/simulator.cpp::run_intra_proactive_check()` | 周期性扫描队列，按预测收益移动任务 | 保留 baseline 决策逻辑 | 用真实队列快照、真实 handoff、实测 tick 周期替换虚拟时间 | 中高 |
| M1 RescueSched 主逻辑 | `src/core/simulator.cpp::run_rescue_sched_check()` | locally doomed、remote feasible、target safe 三阶段 rescue 决策 | 保留为核心算法决策逻辑 | 将 `now_us_`、队列 workload、service estimate、move commit 替换为 runtime 采集和并发安全提交 | 高 |
| rescue commit | `src/core/simulator.cpp::move_rescue_task_intra_host()` | 从源队列移除 queued task 并插入目标队列，记录 harm watch | 保留语义，不保留数据结构操作 | 真实跨队列 handoff、request state CAS、append-tail enqueue、migration log | 高 |
| target safety ablation | `src/app/main.cpp::method_has_target_safety()`，`run_rescue_sched_check()` | 启用或禁用 target safety 过滤 | 保留 | runtime method flag 和 decision log | 中 |
| rescuable filter ablation | `src/app/main.cpp::method_has_rescuable_filter()`，`run_rescue_sched_check()` | 启用或禁用 locally doomed 与 remote feasible 过滤 | 保留 | runtime method flag 和 decision log | 中 |
| hybrid relief | `src/core/simulator.cpp::run_hybrid_relief_check()` | RescueSched 找不到标准 rescue 时按 pressure relief 迁移 | 第一阶段不保留 | 第二阶段 W2 burst 实验再实现 | 中高 |
| AQB/DQB proactive migration | `include/sim/algorithms/dqb_proactive_migration.h`，`include/sim/algorithms/host_proactive_migration.h` | 跨 host 或批量迁移算法 | 第一阶段不保留 | 论文完整实验可作为后续跨节点策略 | 高 |
| metrics collector | `include/sim/metrics/stats.h` | 聚合 latency、SLO、migration quality、成本估算 | 保留字段名和可直接对齐公式 | 替换为 request log、scheduler log、migration log 的 offline aggregator | 中 |
| histogram percentile | `include/sim/metrics/histogram.h`【待确认】 | 仿真内 histogram 计算 P99/P999 | 不建议只保留 histogram | 物理机保存 raw latency 或高精度 histogram，offline 统一算 percentile | 低 |
| RescueSched CSV schema | `src/app/main.cpp::write_rescue_header()`，`write_rescue_row()` | 输出 `rescue_main.csv` 等 summary | 保留 summary 字段作为兼容目标 | 增加 raw logs，同时 summary 字段尽量对齐现有 schema | 中 |
| 图表脚本 | `scripts/rescue_analysis.py` | 读取 `artifacts/step-15-rescuesched`、`step-17-rescuesched-closure` CSV 生成 figures 和 readiness 文档 | 可复用一部分 | 物理机 summary CSV 对齐字段后复用；新增 physical-vs-sim 对比脚本 | 低中 |
| 通用绘图脚本 | `scripts/generate_charts.py` | 读取 legacy step artifacts 生成 Fig1-Fig9 | 暂不作为第一阶段核心 | 若要对齐旧论文图，需要生成同名 CSV 或新建 mapping adapter | 中 |

## 四、假设风险表

| 仿真假设 | 在代码中的体现 | 真实物理机中是否成立 | 风险 | 近似复现方案 |
| --- | --- | --- | --- | --- |
| intra-host 方法等价于 1 个 host、16 个 core | `src/core/simulator.cpp::configure()` 对 `L0`、`L1`、`M0_INTRA_HOST_PROACTIVE`、`M1_RESCUE_*` 设置 `active_host_count_ = 1`，`CORES_PER_HOST = 16` | 部分成立 | c6525-25g 真实 core、SMT、NUMA 拓扑【待确认】；16 worker 可能跨 socket | 第一阶段固定 1 个物理节点、16 个物理 core；优先同一 NUMA node 或记录跨 NUMA 配置 |
| 所有 core 同速 | `ClusterProfile::HOMOGENEOUS` 和 `compute_effective_capacity()` | 近似成立 | 频率 scaling、温度、SMT sibling、IRQ 会造成 per-core service rate 差异 | 关闭或固定 governor【待确认】，避开 SMT sibling，运行 per-worker calibration 并记录 |
| `rho` 可由平均服务时间精确计算 | `lambda_global = rho * effective_capacity_ / mean_service_us` | 不完全成立 | 实测服务时间随系统状态漂移，offered load 与 accepted load 可能不同 | 用单 worker 和 16 worker baseline 校准 service capacity；summary 同时报告 target rho 与 measured rho |
| W3 平均服务时间固定为 24 us | `W3_MEAN_SERVICE_US = 24.0`，`LognormalService` | 目标 service 成立，实际 service 不一定成立 | busy-loop 校准误差会改变负载和 SLO miss | 日志同时记录 `target_service_us` 与 `actual_service_us`，按实际均值修正 QPS |
| 1 us scheduler check 可稳定执行 | `M0_T_CHECK_US = 1.0`，`M0Config::t_check_us` | 很难严格成立 | 用户态 timer jitter、线程抢占、调度线程自身开销 | 使用 dedicated pinned scheduler thread busy polling；记录每次实际 check interval；必要时扫 1、2、5、10 us |
| oracle service estimate 可用 | `SERVICE_ESTIMATE_ORACLE` 默认值，`estimate_service_time()` 直接返回 base service | 真实系统不成立 | 物理机运行时不知道未来实际 service time | 第一阶段主结果使用 `class_mean` 或 `EWMA`；oracle 只用于 simulator 或 trace replay 上界 |
| 队列 scan 和 remove 没有并发成本 | `run_rescue_sched_check()` 直接遍历仿真队列并移除任务 | 不成立 | 锁竞争、ABA、任务开始执行后不可迁移、cache line bouncing | request state CAS 限制只迁移 queued 状态；bounded scan；记录 scan time 和 commit failure |
| 迁移成本固定为 0.5 us | `RESCUE_MIGRATION_COST_US = 0.5` | 不成立 | handoff 成本依赖 queue implementation、NUMA、cache、竞争 | 先做 descriptor-only handoff microbench，取 median 和 P99；实验中可扫 measured cost |
| append-tail 不伤害目标队列 | `RESCUE_TARGET_INSERT_APPEND_TAIL`，`move_rescue_task_intra_host()` | 部分成立 | 真实 enqueue 会增加锁竞争和 cache 干扰，即使排在队尾也可能影响目标 worker | 第一阶段只报告整体 SLO 与 move count；第二阶段增加 target watch 和 overhead log |
| target safety 能由风险模型判断 | `target_safe_count`、`target_unsafe_reject_count`、`RESCUE_THETA` | 只能近似成立 | 目标任务未来 service 和队列演化不可知 | 保留模型判断日志；实际 harm 用 watch log 做近似归因，不作为第一阶段核心结论 |
| locally doomed 与 remote feasible 是可靠决策条件 | `run_rescue_sched_check()` 中 local latency 与 remote latency 预测 | 部分成立 | service estimate 误差、snapshot 陈旧、迁移提交延迟会改变可行性 | 记录 prediction error；第一阶段比较 non-oracle estimator 下的总体 SLO |
| warmup 后系统进入稳定状态 | `MetricsCollector::init(WARMUP_REQUESTS)` | 不一定成立 | CPU 温度、JIT 无关但 cache 和 allocator 状态、队列 backlog 会继续变化 | 同时使用 request-count warmup 和时间序列检查；记录 warmup 末尾 backlog |
| 仿真 histogram 足以代表 raw latency | `MetricsCollector::p99()`、`p999()` | 物理机不应只依赖内存 histogram | histogram bucket、时钟精度和在线聚合可能掩盖异常 | 保存 raw request latency 或至少高分辨率 HDR histogram；offline 统一计算 |
| 单机实验不涉及网络成本 | intra-host RescueSched 只在 core 间移动 | 第一阶段成立 | 后续跨 host 算法无法从单机结论直接外推 | 第一阶段明确限定为 single-node intra-host；跨节点实验另行校准 25G RTT 和 RPC tail |
| W2 burst 热点派发能代表真实 burst skew | `w2_hot_core_count`、`w2_hot_dispatch_prob` | 只是一种合成近似 | 真实业务 burst 可能由连接、NIC queue、锁热点引起 | 第二阶段用 synthetic W2 验证边界，再考虑真实 trace replay |
| 模型估算开销可作为 overhead | `summary_update_cost_est_us()`、`batch_estimation_cost_est_us()`、`target_selection_cost_est_us()` 固定乘系数 | 不成立 | 固定系数无法代表 c6525-25g 上真实控制面开销 | 物理机必须用代码段计时和 perf counters 替换，不沿用这些估算值 |

## 五、第一阶段建议复现目标

建议第一阶段选择一个最小核心结论，而不是完整论文图：在单台 `c6525-25g`、16 个 pinned worker core、W3 Poisson plus lognormal workload、`rho=0.85` 下，对比 `L1_WorkStealing`、`M0_IntraHostProactive` 和 `M1_RescueSched`，观察 RescueSched 是否能降低 `slo_violation_rate` 和 P99，同时记录其 `intra_move_rate` 与 `rescue_success_count`。这个目标对应当前代码的 `src/app/main.cpp::run_rescue_smoke()` 和 `run_rescue_main()` 主线，也对应 `scripts/rescue_analysis.py` 中 `fig_rescue_slo_vs_rho` 的一个核心数据点。

选择该目标的原因：

- 它只依赖 intra-host 逻辑，和 `src/core/simulator.cpp::configure()` 中 RescueSched 单 host 16 core 的模型一致，不需要先复现 64 host 或 25G 网络。
- 核心指标 `P99_us`、`slo_violation_rate`、`total_generated`、`total_finished`、`intra_move_count`、`rescue_success_count` 可以真实测量或直接记录。
- 它避开了第一阶段最难证明的反事实指标，例如 `beneficial_migration_ratio`、`useless_migration_ratio` 和 `target_induced_miss_actual`，但仍可保留原始日志供后续分析。
- 它能直接验证“不是单纯增加迁移次数，而是通过 rescuability-aware decision 改善 SLO”的主线，但第一阶段只能报告现象和可测指标，不能声称完全复现仿真中的反事实质量指标。

第一阶段最小实验矩阵：

| 维度 | 建议设置 | 代码依据 | 备注 |
| --- | --- | --- | --- |
| 机器 | 1 台 `c6525-25g` | intra-host 方法 `active_host_count_ = 1` | CPU、NUMA、SMT、governor【待确认】 |
| worker | 16 个 pinned worker core | `CORES_PER_HOST = 16` | 优先同 NUMA node；若不可行需记录 core list |
| workload | `W3_POISSON_LOGNORMAL` | `run_rescue_smoke()`、`run_rescue_main()` | 目标 service lognormal，mean 24 us，sigma 1.0 |
| load | `rho=0.85` | `run_rescue_smoke()` 默认点 | 先用实测 service capacity 校准 QPS |
| seed | demo 用 11，扩展用 11、23、37、47、59 | `SEEDS` 和 `run_rescue_*` | 多线程非确定性下 seed 只控制输入 trace |
| methods | `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched` | `MethodType` 与 `run_rescue_main()` | `L0_RandomCore` 可作为补充 baseline |
| scheduler params | `t_check_us=1`、`scan_depth=64`、`k=16`、`h=4`、`budget=1`、`epsilon=2` | `M0Config` defaults | 实测 tick jitter 后可加 2 us 或 5 us sensitivity |
| estimator | 第一阶段建议 `class_mean` 或 `EWMA`，oracle 只作为 trace replay 上界 | `estimate_service_time()` | 当前仿真默认 oracle，物理机主实验不能依赖 oracle |
| primary metrics | `P99_us`、`slo_violation_rate`、`total_finished`、`intra_move_rate`、`rescue_success_count` | `write_rescue_header()` | P999 可记录但不作为 demo 成败唯一指标 |
| raw logs | `request_log.csv`、`scheduler_log.csv`、`migration_log.csv`、`overhead_log.csv`、`run_manifest.json` | 对齐 `write_rescue_header()` 字段 | summary CSV 可兼容 `scripts/rescue_analysis.py` |

第一阶段不建议作为成败标准的内容：

- 不把 `beneficial_migration_ratio`、`useless_migration_ratio`、`target_induced_miss_actual` 作为第一阶段主结论，因为它们依赖 counterfactual 或 target-side 归因。
- 不复现 `NUM_HOSTS=64` 的跨 host 行为，也不复现 `T_net_oneway_us` 和 `T_rpc_us`。
- 不把 `summary_update_cost_est_us`、`batch_estimation_cost_est_us`、`target_selection_cost_est_us` 当成物理 overhead；这些必须由真实计时代替。
- 不以 `SERVICE_ESTIMATE_ORACLE` 结果作为物理机主实验结论；oracle 可以作为离线 replay upper bound。
