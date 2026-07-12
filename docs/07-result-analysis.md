# 07 Result Analysis

## 2026-07-12 corrected pilot status

Strong paid baselines change the interpretation of RescueSched. Across both
development and holdout pilots, polling work stealing wins at W3 rho 0.70.
RescueSched shows a consistent candidate advantage at rho 0.90 over both
polling work stealing and ALTO-style threshold migration while moving less work;
the holdout rho 0.85 interval versus work stealing crosses zero. These runs use
5,000 measurement requests and are directional only. The next gate is a W3
full-size run before expanding the full W1/W2 boundary matrix.

更新时间：2026-07-06

本文档是后续实验结果分析模板。当前不填入新的实验结论，未运行的部分标记为【待测试】或【待确认】。

## 分析原则

- 不混用仿真结果和物理机结果，除非指标定义已经对齐。
- 每个图表必须能追溯输入 CSV、脚本、命令和 commit。
- 先报告完整实验矩阵，再报告筛选后的关键结果。
- 对负结果、反向结果和高方差结果保留记录。

## 数据来源登记

| 数据集 | 类型 | 路径 | 生成命令 | commit | 状态 |
| --- | --- | --- | --- | --- | --- |
| RescueSched main sim | 仿真 | `artifacts/step-15-rescuesched/rescue_main.csv` | `./build/simulator --mode rescue-main` | 【待填写】 | 已有历史文件 |
| RescueSched ablation sim | 仿真 | `artifacts/step-15-rescuesched/rescue_ablation.csv` | `./build/simulator --mode rescue-ablation` | 【待填写】 | 已有历史文件 |
| W2 burst sim | 仿真 | `artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv` | `./build/simulator --mode rescue-w2-burst` | 【待填写】 | 已有历史文件 |
| Physical demo | 物理机 | 【待确认】 | 【待确认】 | 【待填写】 | 【待测试】 |
| Physical trace replay | 物理机 | 【待确认】 | 【待确认】 | 【待填写】 | 【待测试】 |

## 仿真结果汇总模板

| workload | rho | seed count | method | P99 median | P999 median | SLO violation median | migration rate | rescue success | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| W3 | 0.85 | 【待填写】 | L1 | 【待计算】 | 【待计算】 | 【待计算】 | 【待计算】 | N/A | 【待确认】 |
| W3 | 0.85 | 【待填写】 | M0 | 【待计算】 | 【待计算】 | 【待计算】 | 【待计算】 | N/A | 【待确认】 |
| W3 | 0.85 | 【待填写】 | M1 RescueSched | 【待计算】 | 【待计算】 | 【待计算】 | 【待计算】 | 【待计算】 | 【待确认】 |

## 物理机结果汇总模板

| run id | machine | workload | rho | seed | method | P99_us | P999_us | slo_violation_rate | migration_count | rescue_success_count | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 【待填写】 | 【待确认】 | W3 | 0.85 | 11 | L1 | 【待测试】 | 【待测试】 | 【待测试】 | 【待测试】 | N/A | 【待测试】 |
| 【待填写】 | 【待确认】 | W3 | 0.85 | 11 | M0 | 【待测试】 | 【待测试】 | 【待测试】 | 【待测试】 | N/A | 【待测试】 |
| 【待填写】 | 【待确认】 | W3 | 0.85 | 11 | M1 | 【待测试】 | 【待测试】 | 【待测试】 | 【待测试】 | 【待测试】 | 【待测试】 |

## 仿真与物理机结果对齐表

| 指标 | 仿真字段 | 物理机字段 | 对齐方法 | 容忍误差 | 当前状态 |
| --- | --- | --- | --- | --- | --- |
| P99 latency | `P99_us` | `P99_us` 或 request log 计算 | 同一 completed request 口径 | 第一阶段【待确认】 | 【待测试】 |
| P999 latency | `P999_us` | `P999_us` 或 request log 计算 | 样本量足够后比较 | 【待确认】 | 【待测试】 |
| SLO violation | `slo_violation_rate` | `deadline_miss / completed` | 相同 SLO 阈值 | 【待确认】 | 【待测试】 |
| generated count | `total_generated` | generator submitted count | 以 request_id 去重 | 0 差异 | 【待测试】 |
| finished count | `total_finished` | completed count | 排除 warmup/timeout 后对齐 | 【待确认】 | 【待测试】 |
| migration rate | `migration_rate` | migration log / generated | 相同迁移定义 | 【待确认】 | 【待测试】 |
| rescue success | `rescue_success_count` | rescue migration success count | 迁移提交成功且任务完成 | 【待确认】 | 【待测试】 |
| target unsafe reject | `target_unsafe_reject_count` | target-safety reject log | 同一 reject 条件 | 【待确认】 | 【待测试】 |
| beneficial ratio | `beneficial_migration_ratio` | 【待确认】 | 需要反事实估计 | 【待确认】 | 第二阶段 |
| useless ratio | `useless_migration_ratio` | 【待确认】 | 需要反事实估计 | 【待确认】 | 第二阶段 |

## 图表计划

| 图表 | 输入 | 输出 | 状态 |
| --- | --- | --- | --- |
| RescueSched SLO vs rho | `rescue_main.csv` + physical aligned CSV | sim/physical overlay | 【待测试】 |
| Ablation quality | `rescue_ablation.csv` + physical ablation CSV | bar/line chart | 【待测试】 |
| Migration cost calibration | `rescue_calibration.csv` + physical microbench | sensitivity chart | 【待测试】 |
| W2 burst boundary | `rescue_w2_burst.csv` + physical W2 CSV | line chart | 【待测试】 |
| Trace replay alignment | trace replay CSVs | table + scatter | 【待确认】 |

## 解释框架

分析每个实验点时填写：

1. RescueSched 是否降低 P99/P999。
2. RescueSched 是否降低 SLO 违约率。
3. 迁移率是否过高。
4. target safety 是否避免引入目标队列伤害。
5. 物理结果与仿真差异来自 workload、服务时间、迁移成本、调度 jitter、NUMA 还是日志口径。
6. 该结果是否支持论文主张。

## 异常结果记录

| 实验 | 异常 | 可能原因 | 需要追加的诊断 |
| --- | --- | --- | --- |
| 【待填写】 | 【待填写】 | 【待确认】 | 【待确认】 |
