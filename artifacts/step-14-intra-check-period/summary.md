# Step-14 主机内主动迁移检查周期与高负载边界总结

日期：2026-06-02

## 目标

上一阶段 W3 `rho=0.92` 和 W1 `rho=0.95` 曾出现运行超时。初步怀疑是 `M0_IntraHostProactive` 默认 `check_period=1us` 导致周期性扫描开销过高。本阶段继续做两件事：

1. 给主动迁移新增检查周期实验入口，比较 `check_period=1us/2us/5us`。
2. 修复仿真实现中的 workload 查询开销，确认高负载结果到底是算法边界，还是仿真实现开销造成的假性超时。

## 实现改动

新增 runner mode：

- `intra-w3-092-check-1`
- `intra-w3-092-check-2`
- `intra-w3-092-check-5`
- `intra-w3-092-check-sweep`
- `intra-w1-095-check-1`
- `intra-w1-095-check-2`
- `intra-w1-095-check-5`
- `intra-w1-095-check-sweep`

新增输出目录：

- `artifacts/step-14-intra-check-period/`

同时对 `Core::local_workload_us(now)` 做了等价性能优化：

- 旧实现每次查询都会遍历整个 `wait_queue`。
- 新实现由 `Core` 维护 `queued_work_us`，任务入队、出队、迁移移除时同步更新。
- `local_workload_us(now)` 变为 O(1)，只需加上 running task 的 residual time。

该优化不改变算法判断公式，只改变仿真器计算 workload 的方式。`intra-smoke` 与 `regression` 均已通过。

## 执行命令

```powershell
cmake --build build-aqb-check
.\build-aqb-check\simulator.exe intra-smoke
.\build-aqb-check\simulator.exe regression
.\build-aqb-check\simulator.exe intra-w3-092-check-5
.\build-aqb-check\simulator.exe intra-w3-092-check-2
.\build-aqb-check\simulator.exe intra-w3-092-check-1
.\build-aqb-check\simulator.exe intra-w1-095-check-5
.\build-aqb-check\simulator.exe intra-w1-095-check-2
.\build-aqb-check\simulator.exe intra-w1-095-check-1
```

生成文件：

- `artifacts/step-14-intra-check-period/intra_w3_rho_092_check_1us.csv`
- `artifacts/step-14-intra-check-period/intra_w3_rho_092_check_2us.csv`
- `artifacts/step-14-intra-check-period/intra_w3_rho_092_check_5us.csv`
- `artifacts/step-14-intra-check-period/intra_w1_rho_095_check_1us.csv`
- `artifacts/step-14-intra-check-period/intra_w1_rho_095_check_2us.csv`
- `artifacts/step-14-intra-check-period/intra_w1_rho_095_check_5us.csv`
- `artifacts/step-14-intra-check-period/median_summary.csv`

## W3 rho=0.92 检查周期结果

以下结果为 5 个 seed 的中位数。`L1_WorkStealing` 的 `check_period_us=0` 表示它不使用主动检查周期，只作为基线。

| 方法 | check/us | P99/us | P999/us | SLO 违约率 | 主机内移动率 | 移动任务数 | 移动工作量/us | proactive attempts |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `L1_WorkStealing` | 0 | 4670 | 7200 | 0.891699 | 0.032516 | 39078 | 1303080 | 0 |
| `M0_IntraHostProactive` | 1 | 4300 | 6810 | 0.935923 | 0.331843 | 398739 | 10359200 | 1631697 |
| `M0_IntraHostProactive` | 2 | 4380 | 5860 | 0.936681 | 0.290838 | 349465 | 9069800 | 815843 |
| `M0_IntraHostProactive` | 5 | 4150 | 6260 | 0.941374 | 0.172880 | 207735 | 5392140 | 326346 |

观察：

- 检查周期越大，proactive attempts 近似按比例下降：`1us` 为 1631697，`2us` 为 815843，`5us` 为 326346。
- 检查周期越大，迁移次数和移动工作量也下降，`5us` 的移动工作量约为 `1us` 的一半。
- 三个主动迁移配置都比工作窃取有更低的 P99；`2us` 和 `5us` 也有更低的 P999。
- 但三个主动迁移配置的 SLO 违约率均高于工作窃取，说明在 `rho=0.92` 接近饱和时，主动重排并不必然改善所有 SLO 任务。
- 所有结果的跨 host `migration_rate=0`，`invalid_intra_move_ratio=0`。

## W1 rho=0.95 高负载 sanity

| 方法 | check/us | P99/us | P999/us | SLO 违约率 | 主机内移动率 | 移动任务数 | proactive attempts |
|---|---:|---:|---:|---:|---:|---:|---:|
| `L1_WorkStealing` | 0 | 10000 | 10000 | 1.000000 | 0 | 0 | 0 |
| `M0_IntraHostProactive` | 1 | 10000 | 10000 | 1.000000 | 0 | 0 | 1632729 |
| `M0_IntraHostProactive` | 2 | 10000 | 10000 | 1.000000 | 0 | 0 | 816370 |
| `M0_IntraHostProactive` | 5 | 10000 | 10000 | 1.000000 | 0 | 0 | 326547 |

观察：

- 工作窃取几乎没有移动，因为 core 长期处于非空闲状态，idle-trigger 条件很少成立。
- 主动迁移虽然进行了大量检查，但几乎没有成功迁移，因为所有 core 都处于高 workload 状态，迁移后预计完成延迟难以满足收益 margin。
- 该结果说明 W1 `rho=0.95` 是容量边界 sanity，不应作为主动迁移常规收益的主结果。

## 关键结论

第一，上一阶段高负载超时主要是仿真器 workload 查询实现造成的。`Core::local_workload_us()` 从 O(queue length) 优化为 O(1) 后，W3 `rho=0.92` 的 `1us/2us/5us` 检查周期都可以完成。

第二，检查周期是主动迁移开销控制的有效参数。`5us` 相比 `1us` 显著减少检查次数、迁移次数和移动工作量，但 P99/P999 仍保持在可比较范围内。后续主实验不一定必须使用最激进的 `1us`。

第三，W3 `rho=0.92` 是主动迁移与工作窃取之间的权衡区。主动迁移降低 P99/P999，但 SLO 违约率高于工作窃取，说明高负载下需要增加迁移预算、冷却或短任务保护，而不能只依赖单任务风险预测。

第四，W1 `rho=0.95` 是饱和失效边界。工作窃取需要 idle core 才能触发，主动迁移需要存在明显更低 workload 的目标 core；当所有 core 都长期拥塞时，两者都无法创造容量。

## 下一步

后续算法应增加高负载保护逻辑：

- 给 `M0_IntraHostProactive` 增加 per-check 或 per-time-window 迁移预算。
- 增加 core 或 task 的迁移冷却，避免高频重复重排。
- 增加短任务保护或目标 core tail-harm 判断，避免为了改善部分尾部任务而提高整体 SLO 违约率。
- 将主结论聚焦在 W3 `rho=0.85` 和 W1 `rho=0.85` 的明确收益，把 W3 `rho=0.92` 和 W1 `rho=0.95` 作为边界分析。
