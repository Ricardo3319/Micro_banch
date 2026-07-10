# 05 Implementation Plan

更新时间：2026-07-07

本文档基于 `docs/04-experiment-design.md`，把物理机复现方案拆解为后续可逐步交给 Codex 实现的开发任务。当前只生成计划，不修改核心代码、不创建脚本、不创建目录、不运行实验。

标记说明：

- 【可复用】：当前项目已有代码、参数、脚本或 schema，可作为实现依据。
- 【需要新增】：当前项目尚无物理机实现，需要后续开发。
- P0：必须先做；P1：最小 Demo 后立即做；P2：完整实验需要；P3：论文材料与扩展。

## 阶段 0：环境检查与项目保护

目标：在开始写物理机 runtime 前，先保护现有仿真、确认依赖和建立可回滚的实验分支。阶段 0 不改变核心算法，只做审计、记录和准备。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0.1 | git 状态检查 | 确认当前工作区是否已有未提交改动，避免覆盖用户或历史工作 | Git worktree；`docs/ai-log.md`【可复用】 | `git status --short`、`git diff --stat` | 一段状态记录，列出已修改/未跟踪文件 | 能清楚区分“已有改动”和“本轮计划改动”；不执行 reset/checkout | 误删或覆盖既有实验改动 | P0 |
| 0.2 | 依赖检查 | 确认 C++ 构建、Python 分析、绘图、系统采集工具是否可用 | `CMakeLists.txt`【可复用】、`requirements.txt`【待确认】、`scripts/`【可复用】 | `cmake --version`、编译器版本、Python 版本、`pip list`、`perf/mpstat/numactl` | `docs/06-test-record.md` 中的依赖检查记录【后续填写】 | 依赖缺失项全部标为【待安装】或【待确认】；不在本任务中安装 | 物理机环境和本地开发机不同 | P0 |
| 0.3 | 当前仿真是否可运行 | 建立“迁移前基线”，确认 RescueSched 仿真入口仍能跑通 | `src/app/main.cpp::run_rescue_smoke()`【可复用】、`config/rescuesched.yaml`【可复用】 | 现有构建产物或一次干净构建 | `rescue-smoke` 运行记录、输出 CSV 路径、关键指标摘要 | smoke 能完成，且 `total_finished == MEASUREMENT_REQUESTS`；若失败则记录失败原因 | 仿真已有未修复问题会阻塞物理实现对齐 | P0 |
| 0.4 | CSV schema 基线确认 | 确认物理机 summary 需要兼容的字段集合 | `src/app/main.cpp::write_rescue_header()`【可复用】、`tests/integration/validate_rescue_csv_schema.py`【可复用】 | 当前 `write_rescue_header()` 输出字段 | 字段清单和必须兼容字段列表 | `summary.csv` 最小字段列表与 `docs/04` 一致 | 后续 physical CSV 字段漂移导致绘图脚本不可复用 | P0 |
| 0.5 | 建立实验分支 | 为物理机实现创建独立开发分支，避免污染主线 | Git branch | 当前 clean/dirty 状态记录 | 新分支，如 `codex/physical-runtime-plan`【建议】 | 分支创建前明确当前未提交状态；不自动提交用户改动 | 在 dirty worktree 上建分支会带入历史改动，需要记录 | P0 |
| 0.6 | 物理机环境清单模板 | 明确后续每次物理实验必须记录的环境字段 | `docs/04-experiment-design.md`【可复用】、`docs/06-test-record.md`【可复用】 | c6525-25g 机器信息【待确认】 | `host_inventory.txt` 字段模板 | 模板包含 CPU、NUMA、SMT、kernel、governor、IRQ、NIC、commit | 环境字段缺失会使实验无法解释 | P1 |

## 阶段 1：最小 Demo

目标：新增一个用户态 physical runtime，单机 16 worker 跑通 W3 workload，对比 `L1_WorkStealing`、`M0_IntraHostProactive`、`M1_RescueSched`，输出原始日志和 summary CSV，并生成一张简单图。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1.1 | physical runtime 目录骨架 | 为物理机实现建立独立模块，避免把仿真事件循环改成真实线程 runtime | `src/physical/`【需要新增】、`CMakeLists.txt`【需要新增修改】 | `docs/04` 推荐目录结构 | 可构建的空 physical target | 不影响现有 simulator target；现有测试仍可运行 | CMake 结构改动影响原仿真构建 | P0 |
| 1.2 | request descriptor 与时间戳 | 定义真实请求结构，承载 arrival/start/finish/service/SLO/migration 字段 | `src/physical/runtime/request.h`【需要新增】；参考 `include/sim/model/task.h`【可复用】 | `Task` 字段语义、`docs/04` 日志 schema | `Request`/`RequestState` 定义 | 字段能覆盖 `request_log.csv` 与 `migration_log.csv` 所需信息 | 并发状态设计不严谨会导致重复迁移 | P0 |
| 1.3 | workload 生成 | 实现 W3 Poisson + lognormal request 生成 | `src/physical/runtime/workload.*`【需要新增】；参考 `include/sim/workloads/generators.h`【可复用】、`include/sim/common/constants.h`【可复用】 | seed、rho、W3 mu/sigma、SLO 阈值 | 可复现 request stream，记录 target service 与 class | 固定 seed 下输入 trace 可复现；target service 分布接近仿真参数 | 用户态定时精度导致 inter-arrival 偏差 | P0 |
| 1.4 | busy-work service 校准 | 让物理 handler 的实际 service time 尽量接近 target service | `src/physical/runtime/worker.*`【需要新增】、`scripts/physical_analyze.py`【后续新增】 | target service_us、CPU clock | `actual_service_us` 分布记录 | 空 handler 和 busy handler 都能输出 service histogram | CPU 频率、cache、抢占导致服务时间漂移 | P0 |
| 1.5 | worker queue runtime | 实现 16 个 pinned worker queue，支持 enqueue、dequeue、bounded scan、queued request remove | `src/physical/runtime/queue.*`【需要新增】、`worker.*`【需要新增】 | worker_count、CPU list、request descriptors | 单机多 worker runtime | 能跑 `L0_RandomCore` 或 no-migration 模式并完成 warmup/measurement | 并发 remove、锁争用、ABA 问题 | P0 |
| 1.6 | CPU affinity 接入 | 将 generator、scheduler、worker、logger 绑定到指定 core | `src/physical/runtime/affinity.*`【需要新增】 | run config 中的 CPU list | runtime 启动日志和 manifest 中的 CPU 绑定 | `worker_id -> cpu_id` 可验证；绑定失败时明确报错 | c6525-25g 拓扑【待确认】；SMT/NUMA 配置错误 | P0 |
| 1.7 | baseline 运行：L0/L1 | 实现随机派发和 work stealing baseline，建立物理对照组 | `src/physical/runtime/scheduler.*`【需要新增】；参考 `enqueue_task_on_random_core()`、`steal_one_task()`【可复用】 | W3 workload、worker queues | `L0_RandomCore`、`L1_WorkStealing` 可运行结果 | L1 至少输出 steal attempt/success/stolen task 计数 | stealing 语义与仿真不同，需要日志解释 | P0 |
| 1.8 | baseline 运行：M0 | 实现 `M0_IntraHostProactive` 的周期检查和 queued request handoff | `scheduler.*`【需要新增】；参考 `run_intra_proactive_check()`【可复用】 | queue snapshot、check period、scan depth | M0 summary 和 migration log | 能输出 `proactive_intra_attempt_count`、`proactive_intra_success_count`、`intra_move_count` | 1 us tick 不稳定，可能需要先支持 5/10 us | P1 |
| 1.9 | 我的算法接入：M1 RescueSched | 实现 locally doomed、remote feasible、target safe、candidate scoring、budgeted commit | `scheduler.*`【需要新增】；参考 `run_rescue_sched_check()`、`move_rescue_task_intra_host()`【可复用】 | queue snapshot、service estimator、migration cost、M0Config 参数 | `M1_RescueSched` 可运行结果 | 输出 `rescue_attempt_count`、`rescue_candidate_count`、`locally_doomed_count`、`remote_feasible_count`、`target_safe_count`、`rescue_success_count` | service estimator 误差、target snapshot 陈旧、commit fail | P0 |
| 1.10 | 指标采集 | 采集 latency、SLO、throughput、CPU、queue length、migration、scheduler overhead | `src/physical/runtime/metrics.*`【需要新增】；参考 `include/sim/metrics/stats.h`【可复用】 | request events、scheduler events、migration events | in-memory counters 和 raw event buffers | 指标能覆盖 `docs/04` 第 11 节的 P0 项 | 日志采集本身污染 tail latency | P0 |
| 1.11 | CSV 输出 | 输出 raw logs 与仿真兼容 summary | `src/physical/runtime/metrics.*`【需要新增】、`results/physical/...`【需要新增】；参考 `write_rescue_header()`【可复用】 | runtime metrics、run config | `request_log.csv`、`scheduler_log.csv`、`decision_log.csv`、`migration_log.csv`、`overhead_log.csv`、`summary.csv` | `summary.csv` 至少包含 `docs/04` 第 12 节最小字段 | 字段命名漂移导致后续脚本不可复用 | P0 |
| 1.12 | 简单画图 | 生成第一张物理 Demo 图：method vs SLO/P99/migration rate | `scripts/physical_plot.py`【需要新增】；参考 `scripts/rescue_analysis.py`【可复用】 | `summary.csv` | `fig_physical_demo_w3.png/pdf` | 图中包含 L1/M0/M1 三个方法和核心指标 | 小样本误导；需要标注 demo 而非论文结论 | P1 |

## 阶段 2：物理机实验脚本

目标：把单次运行变成可重复、可审计、可批量执行的脚本体系。脚本应围绕 `configs/physical/*.yaml`、`results/physical/<date>/<run_id>/` 和 raw logs 工作。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2.1 | `setup_env.sh` | 检查并记录物理机环境，不自动做破坏性系统修改 | `scripts/setup_env.sh`【需要新增】 | c6525-25g 节点、工具链、系统工具 | `host_inventory.txt`、环境检查输出 | 记录 OS、kernel、CPU、NUMA、SMT、governor、IRQ、工具版本 | 权限不足；不同发行版命令输出不同 | P0 |
| 2.2 | `build.sh` | 标准化构建仿真和 physical runtime | `scripts/build.sh`【需要新增】、`CMakeLists.txt`【需要新增修改】 | build type、target | build logs、可执行文件路径 | 一条命令能构建 simulator 和 physical target；失败有日志 | 构建脚本破坏现有本地流程 | P0 |
| 2.3 | `run_baseline.sh` | 运行 L0/L1/M0 baseline 并生成标准 run directory | `scripts/run_baseline.sh`【需要新增】、`configs/physical/demo_w3_single_node.yaml`【需要新增】 | method、rho、seed、worker_count | baseline raw logs 与 summary | 每个 baseline 生成独立 manifest 和 summary | 参数传递不一致导致方法不可比 | P0 |
| 2.4 | `run_method.sh` | 运行 M1 RescueSched 或后续消融方法 | `scripts/run_method.sh`【需要新增】 | method、config、seed、rho | M1 raw logs 与 summary | 能跑 `M1_RescueSched`，后续可扩展 NoTargetSafety/NoRescuable | 方法名和仿真 `method_name()` 不一致 | P0 |
| 2.5 | `run_all.sh` | 批量运行一个矩阵点的所有方法 | `scripts/run_all.sh`【需要新增】 | config、rho、seed、method list | 多个 run directory 和合并 summary | 同一 config 下 L1/M0/M1 全部完成 | 某个方法失败导致批处理丢失部分结果 | P1 |
| 2.6 | `collect_metrics.sh` | 运行时或运行后采集 perf/mpstat/pidstat 等系统指标 | `scripts/collect_metrics.sh`【需要新增】 | PID、run directory、采样周期 | `perf_stat.txt`、`mpstat.csv`、`pidstat.csv`【待定】 | 指标文件能和 run_id 对齐 | 采集工具本身引入 overhead | P1 |
| 2.7 | `parse_logs.py` | 将 raw logs 转为兼容仿真 schema 的 summary | `scripts/parse_logs.py`【需要新增】 | request/migration/scheduler/overhead logs | `summary.csv`、校验报告 | P99、P999、SLO、throughput、migration rate 与手工抽查一致 | 大日志内存占用；timestamp 单位混乱 | P0 |
| 2.8 | `plot_results.py` | 画物理实验结果和初步仿真对齐图 | `scripts/plot_results.py`【需要新增】；参考 `scripts/rescue_analysis.py`【可复用】 | one or more `summary.csv` | latency/SLO/throughput/overhead 图 | 能从阶段 1 Demo 输出至少一张图 | 图表字段依赖过死，矩阵扩展困难 | P1 |
| 2.9 | run manifest 自动生成 | 每次运行自动写入 commit、dirty status、命令、环境、配置 | `scripts/run_*.sh`【需要新增】、runtime manifest writer【需要新增】 | git info、config、host inventory | `run_manifest.json` | 每个 run directory 都有完整 manifest | dirty 状态未记录会影响审计 | P0 |

## 阶段 3：实验矩阵

目标：从单点 Demo 扩展到可支持结论的实验矩阵，覆盖负载强度、worker 数、workload、baseline 和重复实验。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 3.1 | 不同负载强度 | 扫描 `rho`，观察 SLO/P99 随负载变化 | `configs/physical/sweep_w3_rho.yaml`【需要新增】、`scripts/run_all.sh`【需要新增】 | W3，rho=0.50/0.70/0.85/0.92，seed list | `physical_w3_rho_sweep_summary.csv` | 每个 rho 下 L1/M0/M1 都有结果 | physical `rho` 只能近似，需要 measured rho | P1 |
| 3.2 | 不同 worker 数量 | 验证算法是否依赖固定 16 worker，并评估扩展性 | `configs/physical/worker_sweep.yaml`【需要新增】、runtime worker config【需要新增】 | worker_count=4/8/16/32【待确认】 | worker sweep summary 和图 | worker_count 变化时 CPU 绑定和 summary 正确 | c6525-25g 可用物理 core 数【待确认】 | P2 |
| 3.3 | 不同 workload 分布：W1 | 加入 Poisson + bimodal，复现短长任务混合 | `src/physical/runtime/workload.*`【需要新增】；参考 `BimodalService`【可复用】 | W1 参数、rho、seed | W1 summary | 短/长比例接近 0.8/0.2；SLO 口径正确 | 5 us 短任务受系统 overhead 主导 | P2 |
| 3.4 | 不同 workload 分布：W2 | 加入 MMPP burst + bimodal，验证 burst/skew 边界 | `workload.*`【需要新增】；参考 `MMPPArrival`【可复用】 | W2 burst 参数、hot core 参数、rho、seed | W2 burst summary | 状态切换日志和热点派发比例可验证 | 用户态微秒级状态切换 jitter | P2 |
| 3.5 | 不同 baseline | 增加消融方法和 sanity baseline | `scheduler.*`【需要新增】；参考 `method_has_target_safety()`、`method_has_rescuable_filter()`【可复用】 | L0/L1/M0/M1/NoTargetSafety/NoRescuable | ablation summary | 每个方法使用同一 workload trace 或同 seed config | 消融开关实现不等价于仿真 | P2 |
| 3.6 | 重复实验 | 引入 5 seeds 和关键点多次重复 | `scripts/run_all.sh`【需要新增】、`configs/physical/*.yaml`【需要新增】 | seeds=11/23/37/47/59，repeats=3【待确认】 | multi-seed summary | 能按 seed/repeat 聚合 median 与 variance | 总运行时间和日志体积增加 | P1 |
| 3.7 | check period sensitivity | 评估用户态调度周期对收益和 overhead 的影响 | `configs/physical/check_period_sweep.yaml`【需要新增】 | check_period_us=1/2/5/10【待确认】 | check-period sweep 图 | 同时报告 tick jitter 和 SLO/P99 | 1 us tick 不稳定，结果可能不可解释 | P2 |
| 3.8 | migration cost calibration | 用实测 handoff 成本替换仿真固定 `0.5 us` | `scripts/physical_handoff_microbench.*`【需要新增】、`overhead_log.csv`【需要新增】 | queue implementation、CPU/NUMA layout | handoff cost distribution | 输出 median/P99 commit latency，供 M1 参数使用 | microbench 与真实负载下成本不同 | P2 |

## 阶段 4：结果分析

目标：把 physical raw logs、summary 和仿真结果对齐，解释尾延迟、吞吐、系统开销和差异来源。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4.1 | 仿真 vs 物理机对齐 | 对齐 method/workload/rho/seed/schema，比较方向和相对改善 | `scripts/physical_compare_sim.py`【需要新增】、`scripts/rescue_analysis.py`【可复用】 | 仿真 `rescue_main.csv`、physical `summary.csv` | sim-vs-physical 对齐表 | 表中标明可直接对齐、部分对齐、不可对齐指标 | 绝对值差异被误读为复现失败 | P1 |
| 4.2 | 尾延迟分析 | 分析 P99/P999、miss 请求、queue head age 和 migration 事件关系 | `scripts/physical_tail_analysis.py`【需要新增】 | `request_log.csv`、`queue_sample_log.csv`、`migration_log.csv` | tail breakdown 图和 miss case 样例 | 能解释 RescueSched 改善或失效来自哪些请求类别 | P999 样本不足，结论波动大 | P1 |
| 4.3 | 吞吐分析 | 分析 offered/completed QPS、measured rho、backpressure | `parse_logs.py`【需要新增】、`plot_results.py`【需要新增】 | request log、run manifest | throughput vs method/rho 图 | 同时报告 offered QPS 和 completed QPS | generator 与 server 同机抢 CPU | P1 |
| 4.4 | 系统开销分析 | 报告 scheduler overhead、migration commit cost、CPU utilization | `overhead_log.csv`【需要新增】、`perf_stat.txt`【需要新增】 | overhead log、perf/mpstat | overhead 分布和 CPU 图 | 替代仿真固定 overhead 估计，能定位算法成本 | perf 工具权限或采样扰动 | P1 |
| 4.5 | 队列行为分析 | 验证 RescueSched 是否减少 doomed queue head blocking 或高风险队列积压 | `queue_sample_log.csv`【需要新增】、`decision_log.csv`【需要新增】 | queue length/work/head age、decision counters | queue length/head age 分布 | 能把 SLO 改善和队列状态变化关联起来 | sampling 频率不足或采样扰动 | P2 |
| 4.6 | 失败案例分类 | 记录物理结果与仿真不一致的原因 | `docs/07-result-analysis.md`【可复用】、analysis scripts【需要新增】 | 所有 summary、overhead、manifest | failure taxonomy | 至少区分 timer、service drift、NUMA、logger、estimator、queue contention | 只保留成功结果会削弱审计可信度 | P1 |

## 阶段 5：论文材料

目标：把可复现实验产物整理成论文可用的图、表、实验设置文字、baseline 描述和 limitation 描述。阶段 5 只在阶段 1-4 的结果稳定后推进。

| 任务编号 | 任务名称 | 目标 | 涉及文件 | 输入 | 输出 | 验收标准 | 风险 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 5.1 | 图表 | 生成论文候选图：SLO/P99 vs rho、method bar、overhead、queue behavior | `scripts/physical_plot.py`【需要新增】、`docs/figures/`【可复用】 | analyzed summaries | PNG/PDF 图表和图表 provenance | 每张图能追溯到 run_id、config、commit、CSV | 图表过早固化，后续结果变动难维护 | P3 |
| 5.2 | 实验设置文字 | 写清机器、OS、CPU 绑定、NUMA、workload、warmup/measurement、统计方法 | `docs/08-paper-writing-notes.md`【可复用】、`docs/04-experiment-design.md`【可复用】 | host inventory、run manifests | paper-ready experiment setup 草稿 | 不声称未做的多机或内核实验；所有【待确认】项已替换或保留说明 | 环境信息不完整导致论文不可复现 | P3 |
| 5.3 | baseline 描述 | 统一描述 L0/L1/M0/M1 和消融方法的物理实现差异 | `docs/08-paper-writing-notes.md`【可复用】、`src/physical/`【需要新增】 | method implementation notes、summary | baseline 描述草稿 | 每个 baseline 都说明与仿真对应路径和物理差异 | baseline 实现不公平或参数不一致 | P3 |
| 5.4 | limitation 描述 | 明确用户态实现、反事实指标、单机范围、网络未覆盖等限制 | `docs/08-paper-writing-notes.md`【可复用】、`docs/03-physical-mapping.md`【可复用】 | analysis results、risk log | limitation 草稿 | 明确哪些结论来自实测，哪些是 estimator-based | 过度声称物理复现范围 | P3 |
| 5.5 | artifact provenance | 为每张图、每张表建立数据来源与命令记录 | `docs/ARTIFACT_PROVENANCE.md`【可复用】、`results/physical/`【需要新增】 | run manifests、CSV、plot command | provenance 表 | 图表可从命令重新生成 | 手工拷贝图表导致来源断裂 | P3 |
| 5.6 | 复现 README | 写给评审或合作者的复现入口说明 | `README.md`【可复用】、`docs/01-project-overview.md`【可复用】、`docs/06-test-record.md`【可复用】 | finalized scripts/configs | physical reproduction README | 包含环境、构建、运行、分析、常见问题 | README 与脚本实际行为不同步 | P3 |

## 建议的实施顺序

| 顺序 | 建议先做的任务 | 原因 |
| --- | --- | --- |
| 1 | 0.1、0.2、0.3、0.4 | 先保护项目并确认仿真基线没有断 |
| 2 | 1.1、1.2、1.3、1.5、1.11 | 先跑通最小 physical data path：生成、排队、完成、落日志 |
| 3 | 1.7、1.8、1.9 | 再接入 baseline 和 RescueSched 决策 |
| 4 | 2.1、2.2、2.7、2.9 | 把运行变成可审计流程 |
| 5 | 1.12、4.1、4.2、4.4 | 做第一轮图和差异解释 |
| 6 | 阶段 3 和阶段 5 | 在 Demo 稳定后扩展矩阵和论文材料 |

## 后续 Codex 分任务建议

后续每次交给 Codex 的任务建议保持小闭环：

- 一次只实现一个任务编号，例如“实现 1.3 workload 生成”。
- 每次实现都要求：不破坏现有仿真、说明新增文件、给出最小运行命令、更新 `docs/06-test-record.md`。
- 每次实验脚本任务都要求：输入配置、输出路径、失败时错误码和日志位置清晰。
- 每次结果分析任务都要求：说明字段来源，区分实测指标和 estimator-based 指标。

当前不建议一开始就实现完整实验矩阵；先完成阶段 0 和阶段 1 的单点 Demo，能跑出可信日志后再扩展。
