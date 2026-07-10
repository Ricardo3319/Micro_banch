# 08 Paper Writing Notes

更新时间：2026-07-06

本文档记录后续论文写作可用的实验设计、指标、baseline、创新点和限制。当前只作为素材池，不代表最终论文结论。

## 可能的论文主线

RescueSched 面向微秒级 RPC 队列中的局部尾延迟风险：当某个 core 队列中的任务本地预计会错过 SLO，而迁移到另一个 core 后仍可按时完成，并且不会显著伤害目标队列时，系统执行受预算约束的 rescue migration。

可强调的问题：

- 传统 work stealing 更关注空闲资源利用，不一定保护即将违约的任务。
- 粗粒度主动迁移可能带来无效迁移或目标队列伤害。
- RescueSched 试图在“可抢救性”和“目标安全性”之间做决策。

## 可能的创新点

| 创新点 | 当前代码依据 | 论文表述状态 |
| --- | --- | --- |
| 可抢救任务识别 | `run_rescue_sched_check()` in `src/core/simulator.cpp` | 可写，需补公式化描述。 |
| 目标安全检查 | `target_safe_count`、`target_unsafe_reject_count` 相关逻辑 | 可写，需解释 risk model。 |
| 受预算约束的 rescue migration | `rescue_budget_per_check` in `include/sim/common/constants.h` | 可写。 |
| 有益/无效/有害迁移诊断 | `include/sim/metrics/stats.h` | 可写，物理机反事实需谨慎。 |
| 与 no-target-safety/no-rescuable 消融对比 | `M1_RESCUE_NO_TARGET_SAFETY`、`M1_RESCUE_NO_RESCUABLE` | 可写。 |
| 仿真到物理机的校准路径 | `rescue-cost-microbench`、物理复现计划 | 需要物理结果后再写成贡献。 |

## 实验设计可写内容

仿真实验：

- W1/W2/W3 三类 workload。
- rho sweep。
- multi-seed robustness。
- ablation: target safety、rescuable filter、budget/check period。
- calibration: migration cost、service estimate mode。
- boundary/stress: W2 burst、target safety stress。

物理实验：

- 第一阶段只写为 implementation feasibility 或 prototype evaluation【待确认】。
- 若完成多机/trace replay，可写为 external validity。
- 若只完成单机 demo，不应夸大为完整系统验证。

## Baseline

论文中建议至少包含：

- `L0_RandomCore`
- `L1_WorkStealing`
- `M0_IntraHostProactive`
- `M1_RescueSched`
- `M1_RescueSched_NoTargetSafety`
- `M1_RescueSched_NoRescuable`

是否包含 legacy `B1_PowerOf2`、`B2_Reactive`、`M1_AQB_PM`、`M2_DQB_PM`：【待确认】。若包含，应解释它们和 RescueSched 主问题的关系，避免论文焦点发散。

## 指标

核心论文指标：

- P99 latency
- P999 latency
- SLO violation rate
- migration rate
- rescue success count/rate
- beneficial migration ratio
- useless migration ratio
- harmful actual ratio
- target-induced miss count

物理机论文指标还应加入：

- CPU utilization
- scheduler overhead
- migration handoff latency
- run-to-run variance
- trace replay fidelity

## 可写图表

| 图表 | 目的 | 当前来源 |
| --- | --- | --- |
| SLO violation vs rho | 主结果 | `artifacts/step-15-rescuesched/rescue_main.csv` |
| P99/P999 vs rho | 尾延迟结果 | `rescue_main.csv` |
| Ablation quality | 证明 target safety/rescuable filter 必要性 | `rescue_ablation.csv` |
| Budget/check-period sweep | 参数敏感性 | `rescue_check_sweep.csv` |
| W2 burst result | 突发场景鲁棒性 | `rescue_w2_burst.csv` |
| Migration cost calibration | 物理可行性连接 | `rescue_calibration.csv` + physical microbench【待测试】 |
| Simulator vs physical alignment | 外部有效性 | 【待测试】 |

## 限制

需要诚实写明：

- 当前仿真是离散事件模型，不包含完整 OS scheduler jitter。
- 微秒级 timer 和 service-time 控制在物理机上存在误差。
- beneficial/useless migration 在真实系统中需要反事实估计，不能天然观测。
- 物理机 replay 若只使用合成 workload，不能完全代表真实 production trace。
- 如果物理机只做单机 demo，则不能声称已验证跨机数据中心环境。

## 写作待补材料

- RescueSched 算法伪代码。
- 可抢救判定和目标安全判定公式。
- 物理 runtime 架构图。
- trace schema 和数据来源。
- 每张图的 provenance 表。
- 失败 case 和边界条件分析。
- 与现有队列调度/负载均衡工作的 related work 对比【待确认】。
