# Step-15 RescueSched 本地实验摘要

日期：2026-06-30

## 1. 当前问题定义

本阶段把主机内 16 core 调度从“负载均衡”改为“SLO rescue”。RescueSched 不追求迁移更多任务，也不保证 P99/P999 总是最低；它只在 queued-but-not-running task 同时满足以下条件时迁移：

```text
C_local(i,s) > D_i
C_remote(i,j) + epsilon <= D_i
DeltaRisk(j|i) <= theta
per-check migration budget not exhausted
```

其中：

```text
D_i = generate_time_i + SLO_i
C_local(i,s) = now + R_s(now) + work_before_i + service_i
C_remote(i,j) = now + migration_cost + R_j(now) + queued_work_j(now) + service_i
```

实现约束：

- 单 host 16 core。
- 新任务初始随机分配到 core。
- 只迁移 waiting task，不迁移 running task。
- 不启用跨 host migration。
- 服务时间估计使用 `Task::expected_service_time_us`。
- 默认参数：`check_period=1us`、`K=16`、`H=4`、`B=1`、`epsilon=2us`、`theta=0`、`migration_cost=0.5us`。

## 2. 新增方法和指标

新增方法：

- `M1_RescueSched`
- `M1_RescueSched_NoTargetSafety`
- `M1_RescueSched_NoRescuable`

新增关键指标：

- rescue 漏斗：`rescue_attempt_count`、`rescue_candidate_count`、`locally_doomed_count`、`remote_feasible_count`、`target_safe_count`、`rescue_success_count`
- 迁移质量：`needless_migration_count`、`unsaved_migration_count`、`beneficial_migration_count`、`beneficial_migration_ratio`、`useless_migration_ratio`、`rescue_per_migration`
- target 风险：`predicted_target_unsafe_accept_count`、`target_harm_watch_count`、`harmful_actual_count`、`harmful_actual_ratio`、`target_induced_miss_actual`

重要修正：`harmful_migration_count`/`harmful_actual_count` 现在表示 actual target-induced harmful migration。旧口径中的“预测不安全目标被接受”已经拆到 `predicted_target_unsafe_accept_count`。

由于当前策略是 FIFO append-to-tail，迁入任务不会延迟 target 队列中已有任务，因此 actual target-side harmful 在当前事件模型中预期为 0。实现中仍加入了 watch-based counterfactual：迁移前记录 target 队列中本来能 meet SLO 的已有任务，若这些任务最终 actual miss，则计入 `target_induced_miss_actual`，并按 migration id 去重为 `harmful_actual_count`。

## 3. 主实验：W3 heavy-tail

5 seeds 中位数，来自 `rescue_main.csv`。

| rho | method | P99/us | P999/us | SLO violation | move rate | moved | BMR | UMR(actual) | PredUnsafe | HarmActual |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.50 | `L0_RandomCore` | 388 | 704 | 0.232146 | 0.000000 | 0 | 0 | 0 | 0 | 0 |
| 0.50 | `L1_WorkStealing` | 152 | 322 | 0.012427 | 0.339738 | 407691 | 0 | 0 | 0 | 0 |
| 0.50 | `M0_IntraHostProactive` | 158 | 320 | 0.004563 | 0.175668 | 210803 | 0 | 0 | 0 | 0 |
| 0.50 | `M1_RescueSched` | 184 | 356 | 0.004639 | 0.143888 | 172666 | 0.999994 | 0 | 0 | 0 |
| 0.70 | `L1_WorkStealing` | 156 | 322 | 0.042602 | 0.416029 | 499243 | 0 | 0 | 0 | 0 |
| 0.70 | `M0_IntraHostProactive` | 158 | 322 | 0.008723 | 0.271755 | 326108 | 0 | 0 | 0 | 0 |
| 0.70 | `M1_RescueSched` | 190 | 380 | 0.006625 | 0.231298 | 277559 | 0.999993 | 0 | 0 | 0 |
| 0.70 | `M1_RescueSched_NoTargetSafety` | 190 | 380 | 0.006667 | 0.231192 | 277432 | 0.999993 | 0 | 34 | 0 |
| 0.85 | `L0_RandomCore` | 2050 | 3260 | 0.761112 | 0.000000 | 0 | 0 | 0 | 0 | 0 |
| 0.85 | `L1_WorkStealing` | 220 | 364 | 0.203070 | 0.328258 | 393913 | 0 | 0 | 0 | 0 |
| 0.85 | `M0_IntraHostProactive` | 194 | 346 | 0.153639 | 0.399815 | 479784 | 0 | 0 | 0 | 0 |
| 0.85 | `M1_RescueSched` | 258 | 558 | 0.094486 | 0.285511 | 342642 | 0.999982 | 0 | 0 | 0 |
| 0.85 | `M1_RescueSched_NoTargetSafety` | 248 | 532 | 0.095297 | 0.286216 | 343465 | 0.999980 | 0 | 8643 | 0 |
| 0.92 | `L1_WorkStealing` | 4670 | 7200 | 0.891699 | 0.032516 | 39078 | 0 | 0 | 0 | 0 |
| 0.92 | `M0_IntraHostProactive` | 4300 | 6810 | 0.935923 | 0.331843 | 398739 | 0 | 0 | 0 | 0 |
| 0.92 | `M1_RescueSched` | 10000 | 10000 | 0.933827 | 0.017547 | 21137 | 1.000000 | 0 | 0 | 0 |
| 0.92 | `M1_RescueSched_NoTargetSafety` | 10000 | 10000 | 0.923918 | 0.025304 | 30498 | 1.000000 | 0 | 14968 | 0 |

主结论：

- W3 `rho=0.70/0.85` 是 RescueSched 的主要收益区间。
- W3 `rho=0.85` 下，RescueSched 的 SLO violation 从 L1 的 `0.203070`、M0 的 `0.153639` 降到 `0.094486`。
- 同一场景下 RescueSched move rate 为 `0.285511`，低于 M0 的 `0.399815`，说明收益不是来自迁移更多任务。
- RescueSched 的 P99/P999 高于 M0，因此不能声称它总是改善尾延迟；它优化的是 SLO violation 和 migration usefulness。
- W3 `rho=0.92` 是高负载边界。RescueSched 迁移数大幅下降，但 P99/P999 到达 histogram 上限，不应包装成全面胜利。

## 4. 消融实验

W3 `rho=0.85`，5 seeds 中位数，来自 `rescue_ablation.csv`。

| method | SLO violation | moved | needless | unsaved | BMR | UMR(actual) | PredUnsafe | HarmActual |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `M1_RescueSched` | 0.094486 | 342642 | 0 | 0 | 0.999982 | 0 | 0 | 0 |
| `M1_RescueSched_NoTargetSafety` | 0.095297 | 343465 | 0 | 0 | 0.999980 | 0 | 8643 | 0 |
| `M1_RescueSched_NoRescuable` | 0.169362 | 601178 | 339871 | 74512 | 0.310599 | 0.689390 | 0 | 0 |

消融结论：

- `NoRescuable` 明确证明 task-level rescuability 必要：它迁移更多任务，但 BMR 只有 `0.310599`，UMR 为 `0.689390`，SLO violation 也退化到 `0.169362`。
- `NoTargetSafety` 的 actual harmful 为 0，原因是当前 FIFO append-to-tail 不会延迟 target 队列已有任务。因此，在当前仿真里 target safety 只能证明“减少预测 target-risk 暴露”，不能证明“减少真实 target-induced miss”。
- 这不是坏结果，而是模型边界：如果论文要强调 target-side harmful，需要在 CloudLab 原型或更强事件模型中引入会影响 target 已有任务的机制，并明确记录反事实。

## 5. 参数实验

W3 `rho=0.85`，5 seeds 中位数，来自 `rescue_check_sweep.csv`。

| sweep | check/us | epsilon/us | B | SLO violation | moved | BMR | UMR(actual) | unsaved |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| check | 1 | 2 | 1 | 0.094486 | 342642 | 0.999982 | 0 | 0 |
| check | 2 | 2 | 1 | 0.101389 | 336383 | 0.999976 | 0 | 0 |
| check | 5 | 2 | 1 | 0.193655 | 270577 | 0.999985 | 0 | 0 |
| epsilon | 1 | 0 | 1 | 0.091023 | 348191 | 0.999983 | 0 | 0 |
| epsilon | 1 | 2 | 1 | 0.094486 | 342642 | 0.999982 | 0 | 0 |
| epsilon | 1 | 5 | 1 | 0.101969 | 335597 | 0.999982 | 0 | 0 |
| budget | 1 | 2 | 1 | 0.094486 | 342642 | 0.999982 | 0 | 0 |
| budget | 1 | 2 | 2 | 0.102738 | 350235 | 0.965346 | 0.034631 | 12176 |
| budget | 1 | 2 | 4 | 0.105376 | 354993 | 0.947577 | 0.052406 | 18580 |

参数结论：

- `check_period=1us` 是当前默认主实验点；`5us` 会错过 rescue window。
- `epsilon=0` 更激进，SLO 更低但对估计误差更乐观；`epsilon=2us` 更适合作为保守默认。
- B 不是越大越好。B=2/4 会增加 unsaved migration，降低 BMR，提高 UMR，并使 SLO violation 变差。

## 6. W1 sanity 和 overload boundary

5 seeds 中位数，来自 `rescue_overload_sanity.csv`。

| scenario | method | rho | P99/us | P999/us | SLO violation | moved | BMR | PredUnsafe | HarmActual |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| W1 sanity | `L1_WorkStealing` | 0.85 | 240 | 366 | 0.332489 | 0 | 0 | 0 | 0 |
| W1 sanity | `M0_IntraHostProactive` | 0.85 | 204 | 284 | 0.256767 | 0 | 0 | 0 | 0 |
| W1 sanity | `M1_RescueSched` | 0.85 | 300 | 584 | 0.207337 | 415748 | 0.999986 | 0 | 0 |
| W1 sanity | `M1_RescueSched_NoTargetSafety` | 0.85 | 276 | 1340 | 0.205511 | 424675 | 0.999983 | 31907 | 0 |
| W1 overload | `L1_WorkStealing` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0 | 0 | 0 |
| W1 overload | `M0_IntraHostProactive` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0 | 0 | 0 |
| W1 overload | `M1_RescueSched` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0 | 0 | 0 |
| W1 overload | `M1_RescueSched_NoTargetSafety` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0 | 0 | 0 |

结论：

- W1 `rho=0.85` 下 RescueSched 继续降低 SLO violation，但 P99/P999 更高，应作为目标函数差异来解释。
- W1 `rho=0.95` 下 RescueSched 迁移数为 0，证明全局过载时不会无意义迁移，也不能创造容量。

## 7. 已运行命令

```powershell
cmake --build build-aqb-check
.\build-aqb-check\simulator.exe rescue-smoke
.\build-aqb-check\simulator.exe rescue-main
.\build-aqb-check\simulator.exe rescue-ablation
.\build-aqb-check\simulator.exe rescue-check-sweep
.\build-aqb-check\simulator.exe rescue-overload-sanity
.\build-aqb-check\simulator.exe rescue-w3-only
python scripts\rescue_analysis.py
```

## 8. 输出文件

- `artifacts/step-15-rescuesched/rescue_main.csv`
- `artifacts/step-15-rescuesched/rescue_ablation.csv`
- `artifacts/step-15-rescuesched/rescue_check_sweep.csv`
- `artifacts/step-15-rescuesched/rescue_overload_sanity.csv`
- `artifacts/step-15-rescuesched/rescue_w3_only.csv`
- `artifacts/step-15-rescuesched/summary.md`
- `artifacts/step-16-rescuesched-readiness/median_summary.csv`
- `artifacts/step-16-rescuesched-readiness/readiness_report.md`
- `artifacts/step-16-rescuesched-readiness/figures/*.png`
- `artifacts/step-16-rescuesched-readiness/figures/*.pdf`

## 9. 当前可汇报结论

可以汇报：

1. Task-level rescuability 是必要的。
2. RescueSched 在 W3 `rho=0.70/0.85` 和 W1 `rho=0.85` 降低 SLO violation。
3. RescueSched 的收益不是迁移更多任务，而是迁移质量更高。
4. B=1 比 B=2/4 更稳，说明迁移预算不是越大越好。
5. 全局过载下 RescueSched 抑制迁移，不声称创造容量。

需要谨慎汇报：

1. Target safety 在当前 FIFO append 模型中没有表现为 actual harmful reduction；只能说减少 predicted target-risk exposure。
2. P99/P999 与 SLO violation 存在叙事冲突，必须明确 RescueSched 优先优化 SLO rescue。
3. 当前 service-time estimate 使用真实 sampled service time，CloudLab 前需要校准为可观测估计。
4. 还缺 W2 burst-skew RescueSched 结果、10-seed/CI 稳健性和真实 migration cost 标定。
