# Step-13 主机内 Core 调度负载扫描总结


## 目标

本阶段继续围绕单 host 内 16 个 core 的任务调度展开，不分析跨 host 迁移。实验目标是补齐 W3 heavy-tail 负载扫描，并验证简单 W1 bimodal 场景和高负载边界下算法是否稳定。

对比方法如下：

- `L0_RandomCore`：新任务随机分配到 core，不做后续修复。
- `L1_WorkStealing`：core 空闲时，从本 host 内等待队列非空且 workload 最大的 core 窃取队首等待任务。
- `M0_IntraHostProactive`：周期性扫描本 host 内等待队列前缀，对预计存在 SLO 风险且迁移后收益超过 margin 的 waiting task 做主机内主动迁移。

## 执行情况

已完成命令：

```powershell
cmake --build build-aqb-check
.\build-aqb-check\simulator.exe intra-w3-rho-050
.\build-aqb-check\simulator.exe intra-w3-rho-070
.\build-aqb-check\simulator.exe intra-w3-rho-085
.\build-aqb-check\simulator.exe intra-w3-rho-092
.\build-aqb-check\simulator.exe intra-w1-sanity
.\build-aqb-check\simulator.exe intra-w1-high
```

输出文件：

- `artifacts/step-13-intra-host-sweep/intra_w3_rho_050.csv`
- `artifacts/step-13-intra-host-sweep/intra_w3_rho_070.csv`
- `artifacts/step-13-intra-host-sweep/intra_w3_rho_085.csv`
- `artifacts/step-13-intra-host-sweep/intra_w3_rho_092.csv`
- `artifacts/step-13-intra-host-sweep/intra_w1_sanity.csv`
- `artifacts/step-13-intra-host-sweep/intra_w1_high_load.csv`
- `artifacts/step-13-intra-host-sweep/median_summary.csv`

所有已纳入结果均完成 1M measurement 请求，跨 host `migration_rate` 为 0，`invalid_intra_move_ratio` 为 0。

## W3 Heavy-tail 结果

以下结果均为 5 个 seed 的中位数。

| rho | 方法 | P99/us | P999/us | SLO 违约率 | 主机内移动率 | 移动任务数 | 移动工作量/us |
|---:|---|---:|---:|---:|---:|---:|---:|
| 0.50 | `L0_RandomCore` | 388 | 704 | 0.232146 | 0 | 0 | 0 |
| 0.50 | `L1_WorkStealing` | 152 | 322 | 0.012427 | 0.339738 | 407691 | 11160100 |
| 0.50 | `M0_IntraHostProactive` | 158 | 320 | 0.004558 | 0.175575 | 210691 | 4574420 |
| 0.70 | `L0_RandomCore` | 704 | 1130 | 0.452664 | 0 | 0 | 0 |
| 0.70 | `L1_WorkStealing` | 156 | 322 | 0.042602 | 0.416029 | 499243 | 14430200 |
| 0.70 | `M0_IntraHostProactive` | 158 | 322 | 0.008748 | 0.271988 | 326388 | 6998180 |
| 0.85 | `L0_RandomCore` | 2050 | 3260 | 0.761112 | 0 | 0 | 0 |
| 0.85 | `L1_WorkStealing` | 220 | 364 | 0.203070 | 0.328258 | 393913 | 12520900 |
| 0.85 | `M0_IntraHostProactive` | 194 | 348 | 0.153335 | 0.399925 | 479917 | 10274200 |
| 0.92 | `L0_RandomCore` | 10000 | 10000 | 0.986228 | 0 | 0 | 0 |
| 0.92 | `L1_WorkStealing` | 4670 | 7200 | 0.891699 | 0.032516 | 39078 | 1303080 |
| 0.92 | `M0_IntraHostProactive` | 4300 | 6810 | 0.935923 | 0.331843 | 398739 | 10359200 |

## W1 Sanity 与高负载边界

| 场景 | rho | 方法 | P99/us | P999/us | SLO 违约率 | 主机内移动率 | 移动任务数 |
|---|---:|---|---:|---:|---:|---:|---:|
| W1 sanity | 0.85 | `L0_RandomCore` | 2480 | 3480 | 0.857604 | 0 | 0 |
| W1 sanity | 0.85 | `L1_WorkStealing` | 240 | 366 | 0.332489 | 0.331218 | 397482 |
| W1 sanity | 0.85 | `M0_IntraHostProactive` | 204 | 286 | 0.258119 | 0.538491 | 646229 |
| W1 high | 0.95 | `L0_RandomCore` | 10000 | 10000 | 1.000000 | 0 | 0 |
| W1 high | 0.95 | `L1_WorkStealing` | 10000 | 10000 | 1.000000 | 0 | 0 |
| W1 high | 0.95 | `M0_IntraHostProactive` | 10000 | 10000 | 1.000000 | 0 | 0 |

## 分析

第一，主机内调度是当前课题必须保留的主线。`L0_RandomCore` 在 W3 heavy-tail 下随着 rho 上升快速退化，`rho=0.92` 时 P99/P999 已经达到当前 histogram 的 10000us 桶，SLO 违约率接近 1。这说明如果不先处理 host 内 core 队列不均衡，直接讨论跨 host 迁移不符合现实。

第二，工作窃取是强基线。它在 W3 `rho=0.50/0.70/0.85/0.92` 和 W1 `rho=0.85` 下都显著优于随机 core 分配。其优势来自空闲 core 对局部积压队列的被动修复，尤其适合“部分 core 队列长、部分 core 空闲”的不均衡状态。

第三，主动迁移的核心收益不是替代工作窃取，而是在工作窃取之前提前修复有 SLO 风险的 waiting task。在 W3 `rho=0.85`，主动迁移相对工作窃取将 P99 从 220us 降到 194us，P999 从 364us 降到 348us，SLO 违约率从 0.203070 降到 0.153335。W1 `rho=0.85` 下也没有退化，P99/P999/SLO 均优于工作窃取。

第四，在 W3 `rho=0.50/0.70`，主动迁移的 P99 与工作窃取接近，但 SLO 违约率明显更低，并且移动工作量更少。例如 W3 `rho=0.70` 时，工作窃取移动工作量为 14430200us，主动迁移为 6998180us。这说明主动迁移更像“风险任务修复机制”，不是简单搬运最大工作量。

第五，W3 `rho=0.92` 是边界点。主动迁移仍能相对工作窃取降低 P99 和 P999，但 SLO 违约率更高。这说明在接近饱和的 heavy-tail 场景下，主动迁移可能通过大量重排改善尾部分位点，但对大量短 SLO 任务并不一定更友好。因此论文中不应把 `rho=0.92` 描述成主动迁移全面优于工作窃取，而应描述为高负载边界与权衡点。

第六，W1 `rho=0.95` 是饱和 sanity，而不是主性能结论。此时三种方法 P99/P999 均达到 10000us 桶，SLO 违约率均为 1。工作窃取几乎没有 steal，因为 core 长期非空闲；主动迁移几乎没有 success，因为目标 core 也没有足够低的预计 workload，收益条件难以成立。这个结果说明迁移机制不能创造系统容量，只能修复可利用的局部不均衡。

## 闭环结论

当前实验闭环可以表述为：

```text
在单 host 16 core 场景中，随机 core 分配会在 heavy-tail 长任务下造成严重 core 队列不均衡。
工作窃取是必须对比的强基线，能够被动修复空闲 core 与积压 core 之间的不均衡。
主机内主动迁移的合理定位，是在工作窃取之上提前识别并修复 SLO 风险 waiting task。
在中高负载 W3 rho=0.85 和 W1 rho=0.85 下，主动迁移相对工作窃取有明确收益。
在更高负载边界 W3 rho=0.92 和 W1 rho=0.95 下，需要把结果解释为容量接近饱和时的权衡或失效边界。
```

下一步应围绕主动迁移检查周期、迁移预算和高负载保护机制继续实验，避免在接近饱和时过度重排任务。
