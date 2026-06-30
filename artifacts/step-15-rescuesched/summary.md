# Step-15 RescueSched 单机 16 core SLO Rescue 实验闭环

日期：2026-06-30

## 1. 问题重新定义

本轮实验把主机内调度问题从“负载均衡”改为“SLO rescue”：

- `L1_WorkStealing` 回答的是“哪个 core 空了，可以从哪里偷一个 waiting task”。
- `M0_IntraHostProactive` 回答的是“哪个 waiting task 有风险，迁移后 gain 是否超过 margin”。
- `M1_RescueSched` 回答的是“这个 queued-but-not-running task 留在本地是否必然 miss，迁到某个目标 core 后是否还能 meet SLO，并且目标 core 是否安全”。

因此 RescueSched 的目标不是迁移更多任务，也不是让队列长度更均匀，而是把迁移当成稀缺的 SLO rescue 动作。

## 2. RescueSched 最小原型

新增方法：

- `M1_RescueSched`
- `M1_RescueSched_NoTargetSafety`
- `M1_RescueSched_NoRescuable`

共同约束：

- 仅运行在单 host 16 core 内。
- 新任务初始仍随机分配到 core。
- 只迁移 waiting task，不迁移 running task。
- 不启用跨 host migration。
- 服务时间估计使用 `Task::expected_service_time_us`。
- 使用 `Core::queued_work_us` 和 running residual time 估计 core workload。

默认 RescueSched 参数：

- `check_period_us=1`
- `K=16`
- `H=4`
- `B=1`
- `epsilon=2us`
- `theta=0`
- `migration_cost_us=0.5us`
- `scan_depth=64`

## 3. 判断流程和公式

对 source core `s` 上的 queued task `i`：

```text
D_i = generate_time_i + SLO_i
C_local(i,s) = now + R_s(now) + sum(service of tasks before i) + service_i
locally doomed <=> C_local(i,s) > D_i
```

对 target core `j`：

```text
C_remote(i,j) =
  now + migration_cost + R_j(now) + queued_work_j(now) + service_i

remotely feasible <=> C_remote(i,j) + epsilon <= D_i
```

目标安全判断：

```text
Risk_before(j) = number of predicted misses in bounded target queue window
Risk_after(j|i) = Risk_before(j) + predicted miss of appended i
DeltaRisk(j|i) = Risk_after(j|i) - Risk_before(j)

target safe <=> Risk_before(j) == 0 and DeltaRisk(j|i) <= theta
```

说明：严格版本额外要求 `Risk_before(j)==0`，这是 Algorithm 1 中 “low-risk target core” 的保守落地。由于第一版只 append 到 target tail，已有 target queued tasks 不会被新 task 延迟，所以真实 target-side harmful counterfactual 尚未完整实现；当前 `harmful_migration_count` 记录的是“接受了预测 target-risk 不安全目标”的近似计数。

## 4. 与 M0_IntraHostProactive 的区别

`M0_IntraHostProactive`：

- 以 local latency risk 触发。
- 选择 workload 最低的目标 core。
- 迁移条件是迁移后 gain 超过 `margin_us`。
- 更像风险任务提前重排和负载压力释放。

`M1_RescueSched`：

- 必须先证明 task locally doomed。
- 必须证明某个 target remotely feasible。
- 必须通过 target safe 判断。
- 迁移质量用 BMR、UMR、RPM 评价，而不只看迁移数。

## 5. 主实验：W3 heavy-tail

以下为 5 seeds 中位数，输出来自 `artifacts/step-15-rescuesched/rescue_main.csv`。

| rho | method | P99/us | P999/us | SLO violation | migration rate | moved tasks | BMR | UMR | RPM |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.50 | `L0_RandomCore` | 388 | 704 | 0.232146 | 0.000000 | 0 | 0.000000 | 0.000000 | 0.000000 |
| 0.50 | `L1_WorkStealing` | 152 | 322 | 0.012427 | 0.339738 | 407691 | 0.000000 | 0.000000 | 0.000000 |
| 0.50 | `M0_IntraHostProactive` | 158 | 320 | 0.004563 | 0.175668 | 210803 | 0.000000 | 0.000000 | 0.000000 |
| 0.50 | `M1_RescueSched` | 184 | 356 | 0.004639 | 0.143888 | 172666 | 0.999994 | 0.000000 | 0.999994 |
| 0.50 | `M1_RescueSched_NoTargetSafety` | 184 | 356 | 0.004639 | 0.143888 | 172666 | 0.999994 | 0.000000 | 0.999994 |
| 0.70 | `L0_RandomCore` | 704 | 1130 | 0.452664 | 0.000000 | 0 | 0.000000 | 0.000000 | 0.000000 |
| 0.70 | `L1_WorkStealing` | 156 | 322 | 0.042602 | 0.416029 | 499243 | 0.000000 | 0.000000 | 0.000000 |
| 0.70 | `M0_IntraHostProactive` | 158 | 322 | 0.008723 | 0.271755 | 326108 | 0.000000 | 0.000000 | 0.000000 |
| 0.70 | `M1_RescueSched` | 190 | 380 | 0.006625 | 0.231298 | 277559 | 0.999993 | 0.000000 | 0.999993 |
| 0.70 | `M1_RescueSched_NoTargetSafety` | 190 | 380 | 0.006667 | 0.231192 | 277432 | 0.999993 | 0.000122 | 0.999993 |
| 0.85 | `L0_RandomCore` | 2050 | 3260 | 0.761112 | 0.000000 | 0 | 0.000000 | 0.000000 | 0.000000 |
| 0.85 | `L1_WorkStealing` | 220 | 364 | 0.203070 | 0.328258 | 393913 | 0.000000 | 0.000000 | 0.000000 |
| 0.85 | `M0_IntraHostProactive` | 194 | 346 | 0.153639 | 0.399815 | 479784 | 0.000000 | 0.000000 | 0.000000 |
| 0.85 | `M1_RescueSched` | 258 | 558 | 0.094486 | 0.285511 | 342642 | 0.999982 | 0.000000 | 0.999982 |
| 0.85 | `M1_RescueSched_NoTargetSafety` | 248 | 532 | 0.095297 | 0.286216 | 343465 | 0.999980 | 0.025164 | 0.999980 |
| 0.92 | `L0_RandomCore` | 10000 | 10000 | 0.986228 | 0.000000 | 0 | 0.000000 | 0.000000 | 0.000000 |
| 0.92 | `L1_WorkStealing` | 4670 | 7200 | 0.891699 | 0.032516 | 39078 | 0.000000 | 0.000000 | 0.000000 |
| 0.92 | `M0_IntraHostProactive` | 4300 | 6810 | 0.935923 | 0.331843 | 398739 | 0.000000 | 0.000000 | 0.000000 |
| 0.92 | `M1_RescueSched` | 10000 | 10000 | 0.933827 | 0.017547 | 21137 | 1.000000 | 0.000000 | 1.000000 |
| 0.92 | `M1_RescueSched_NoTargetSafety` | 10000 | 10000 | 0.923918 | 0.025304 | 30498 | 1.000000 | 0.502458 | 1.000000 |

主观察：

- W3 `rho=0.70/0.85` 是 RescueSched 的主要收益区间。相对 `L1_WorkStealing` 和 `M0_IntraHostProactive`，它显著降低 SLO violation。
- W3 `rho=0.85` 下，RescueSched 的迁移率 `0.285511` 低于 M0 的 `0.399815`，但 SLO violation 从 M0 的 `0.153639` 降到 `0.094486`。
- RescueSched 的 P99/P999 不一定低于 M0。它牺牲了部分尾部分位数表现，换来了更少 SLO miss。这正是“SLO rescue”而不是“尾延迟最小化”的区别。
- W3 `rho=0.92` 是高负载边界。RescueSched 会强烈抑制迁移，只有约 `21137` 次中位迁移，说明当 slack 不足时不会强行重排。此时不应声称 RescueSched 解决全局过载。

## 6. 消融实验

输出来自 `artifacts/step-15-rescuesched/rescue_ablation.csv`，场景为 W3 `rho=0.85`。

| method | P99/us | P999/us | SLO violation | moved tasks | needless | unsaved | harmful_pred | BMR | UMR | RPM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `M1_RescueSched` | 258 | 558 | 0.094486 | 342642 | 0 | 0 | 0 | 0.999982 | 0.000000 | 0.999982 |
| `M1_RescueSched_NoTargetSafety` | 248 | 532 | 0.095297 | 343465 | 0 | 0 | 8643 | 0.999980 | 0.025164 | 0.999980 |
| `M1_RescueSched_NoRescuable` | 212 | 364 | 0.169362 | 601178 | 339871 | 74512 | 0 | 0.310599 | 0.689390 | 0.310599 |

消融结论：

- 去掉 target safety 后，SLO violation 与 RescueSched 接近，但会接收大量预测 target-risk 不安全迁移。W3 `rho=0.85` 中位 `harmful_pred=8643`，UMR 上升到 `0.025164`。
- 去掉 rescuable 判断后，迁移数从约 `342k` 暴涨到约 `601k`，其中 needless 约 `340k`、unsaved 约 `75k`，BMR 只有 `0.31`，UMR 约 `0.69`。这说明“高负载队列搬任务”不是 RescueSched 的核心。
- `NoRescuable` 的 P99/P999 反而更低，但 SLO violation 更高。这是重要反例：尾部分位数变好不等于 SLO rescue 成功。

## 7. 参数实验

输出来自 `artifacts/step-15-rescuesched/rescue_check_sweep.csv`，场景为 W3 `rho=0.85`。

| sweep | check/us | epsilon/us | B | P99/us | SLO violation | moved tasks | BMR | UMR |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| rescue_check_period_sweep | 1 | 2 | 1 | 258 | 0.094486 | 342642 | 0.999982 | 0.000000 |
| rescue_check_period_sweep | 2 | 2 | 1 | 260 | 0.101389 | 336383 | 0.999976 | 0.000000 |
| rescue_check_period_sweep | 5 | 2 | 1 | 284 | 0.193655 | 270577 | 0.999985 | 0.000000 |
| rescue_epsilon_sweep | 1 | 0 | 1 | 254 | 0.091023 | 348191 | 0.999983 | 0.000000 |
| rescue_epsilon_sweep | 1 | 2 | 1 | 258 | 0.094486 | 342642 | 0.999982 | 0.000000 |
| rescue_epsilon_sweep | 1 | 5 | 1 | 262 | 0.101969 | 335597 | 0.999982 | 0.000000 |
| rescue_budget_sweep | 1 | 2 | 1 | 258 | 0.094486 | 342642 | 0.999982 | 0.000000 |
| rescue_budget_sweep | 1 | 2 | 2 | 256 | 0.102738 | 350235 | 0.965346 | 0.034631 |
| rescue_budget_sweep | 1 | 2 | 4 | 256 | 0.105376 | 354993 | 0.947577 | 0.052406 |

参数结论：

- `check_period=1us` 最适合作为主实验默认值；`5us` 会错过较多 rescue window，SLO violation 回升明显。
- `epsilon=0` 更激进，SLO violation 最低，但对服务时间估计和 migration cost 更乐观；`epsilon=2us` 是更稳妥默认。
- 预算不是越大越好。`B=2/4` 迁移更多，但 BMR 从接近 1 下降到 `0.965/0.948`，UMR 上升，SLO violation 也变差。这直接支持“收益不是迁移更多任务，而是提高迁移质量”。

## 8. W1 sanity 与 overload boundary

输出来自 `artifacts/step-15-rescuesched/rescue_overload_sanity.csv`。

| scenario | method | rho | P99/us | P999/us | SLO violation | rescue moves | BMR | UMR |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| rescue_w1_sanity | `L1_WorkStealing` | 0.85 | 240 | 366 | 0.332489 | 0 | 0.000000 | 0.000000 |
| rescue_w1_sanity | `M0_IntraHostProactive` | 0.85 | 204 | 284 | 0.256767 | 0 | 0.000000 | 0.000000 |
| rescue_w1_sanity | `M1_RescueSched` | 0.85 | 300 | 584 | 0.207337 | 415748 | 0.999986 | 0.000000 |
| rescue_w1_sanity | `M1_RescueSched_NoTargetSafety` | 0.85 | 276 | 1340 | 0.205511 | 424675 | 0.999983 | 0.075611 |
| rescue_w1_overload_boundary | `L1_WorkStealing` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0.000000 | 0.000000 |
| rescue_w1_overload_boundary | `M0_IntraHostProactive` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0.000000 | 0.000000 |
| rescue_w1_overload_boundary | `M1_RescueSched` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0.000000 | 0.000000 |
| rescue_w1_overload_boundary | `M1_RescueSched_NoTargetSafety` | 0.95 | 10000 | 10000 | 1.000000 | 0 | 0.000000 | 0.000000 |

W1 结论：

- W1 `rho=0.85` 下 RescueSched 继续降低 SLO violation，但 P99/P999 变高。这个结果可作为“目标函数差异”的佐证。
- W1 `rho=0.95` 是干净的 overload sanity：所有方法 SLO violation 约等于 1，RescueSched 迁移数为 0。所有 core 都没有 slack 时，它不会创造容量。

## 9. 已执行命令

```powershell
cmake --build build-aqb-check
.\build-aqb-check\simulator.exe intra-smoke
.\build-aqb-check\simulator.exe regression
.\build-aqb-check\simulator.exe rescue-smoke
.\build-aqb-check\simulator.exe rescue-main
.\build-aqb-check\simulator.exe rescue-ablation
.\build-aqb-check\simulator.exe rescue-check-sweep
.\build-aqb-check\simulator.exe rescue-overload-sanity
.\build-aqb-check\simulator.exe rescue-w3-only
```

## 10. 输出文件

- `artifacts/step-15-rescuesched/rescue_main.csv`
- `artifacts/step-15-rescuesched/rescue_ablation.csv`
- `artifacts/step-15-rescuesched/rescue_check_sweep.csv`
- `artifacts/step-15-rescuesched/rescue_overload_sanity.csv`
- `artifacts/step-15-rescuesched/rescue_w3_only.csv`
- `artifacts/step-15-rescuesched/summary.md`

## 11. 最终论文级结论

可向导师汇报的结论：

1. RescueSched 把主机内迁移从负载均衡重新定义为 task-level SLO rescue。
2. 在 W3 heavy-tail `rho=0.70/0.85`，RescueSched 相对工作窃取和 M0 显著降低 SLO violation。
3. RescueSched 的收益不是迁移更多任务。W3 `rho=0.85` 下它比 M0 迁移更少，但 SLO violation 更低，BMR 接近 1。
4. NoRescuable 消融证明：只按高负载队列迁移会制造大量 needless/unsaved migration，UMR 大幅上升。
5. 参数实验表明 B=1 最稳，增加迁移预算会降低 BMR、提高 UMR，并恶化 SLO violation。
6. W1 `rho=0.95` 证明 RescueSched 不解决全局过载；没有 slack 时它会抑制迁移。

需要谨慎表述或待扩展的结论：

- RescueSched 不总是降低 P99/P999。它优化的是 SLO violation 和 migration usefulness。
- `harmful_migration_count` 当前是预测 target-risk 近似，不是完整真实反事实。后续可扩展为记录 target 队列任务的实际 counterfactual miss。
- W3 `rho=0.92` 是边界场景，不应包装成全面胜利。该点主要证明 RescueSched 会减少无 slack 场景下的迁移。
