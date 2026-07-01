# Step-17 RescueSched Closure Summary

本轮补齐 Step-16 readiness 报告中标出的三个缺口：W2 burst-skew、10-seed/CI、以及 migration-cost 与 service-time estimate 校准。

## 1. 新增实验产物

- `rescue_w2_burst.csv`: W2 MMPP burst-skew 单 host 16 core 实验。
- `rescue_robustness_10seed.csv`: W3/W2 rho=0.85 的 10-seed 稳健性实验。
- `rescue_calibration.csv`: migration cost 与 service-time estimate sensitivity。
- `migration_cost_microbench.csv`: 本地 descriptor queue/handoff 粗粒度成本微基准。
- `closure_median_summary.csv` 与 `ci_summary.csv`: 自动汇总表。

## 2. W2 Burst-Skew 结论

| method | P99 | P999 | SLO violation | move rate | BMR | UMR | RPM |
|---|---:|---:|---:|---:|---:|---:|---:|
| L1_WorkStealing | 2390 | 4270 | 0.622935 | 0.203941 | 0 | 0 | 0 |
| M0_IntraHostProactive | 1390 | 3710 | 0.630752 | 0.587538 | 0 | 0 | 0 |
| M1_RescueSched | 10000 | 10000 | 0.687816 | 0.129427 | 0.999981 | 0 | 0.999981 |
| M1_RescueSched_NoTargetSafety | 10000 | 10000 | 0.681988 | 0.139755 | 0.999976 | 0 | 0.999976 |
| M1_RescueSched_NoRescuable | 2180 | 4080 | 0.539359 | 0.33731 | 0.461836 | 0.538149 | 0.461836 |

解释：W2 burst-skew 是由单 host 内 MMPP burst 期间 hot-core 偏置产生的局部不均衡。它补上了只看 W3 heavy-tail 的泛化缺口，但仍是仿真压力源，不等同于真实线上 trace。

## 3. 10-Seed/CI 稳健性

| scenario | method | metric | mean | 95% CI | n |
|---|---|---|---:|---:|---:|
| rescue_w3_robustness_10seed | M1_RescueSched | slo_violation_rate | 0.095612 | [0.092662, 0.098563] | 10 |
| rescue_w2_robustness_10seed | M1_RescueSched | slo_violation_rate | 0.711415 | [0.676093, 0.746738] | 10 |

解释：10 seeds 主要用于给导师汇报时避免只讲 median。CI 仍来自本地模拟器，论文级强结论需要 CloudLab 或 trace replay 复现。

## 4. Cost 与 Estimate 校准

| setting | SLO violation | move rate | BMR | UMR |
|---|---:|---:|---:|---:|
| cost=0us oracle | 0.09344 | 0.286941 | 0.999983 | 0 |
| cost=5us oracle | 0.105568 | 0.276205 | 0.999982 | 0 |
| mean service estimate | 0.155596 | 0.305554 | 0.98403 | 0.019662 |
| noisy oracle cv=0.5 | 0.131688 | 0.292488 | 0.952051 | 0.069086 |

解释：`oracle` 仍是上界；`mean` 和 noisy-oracle 是第一版估计误差消融。真实服务时间预测还需要 EWMA/quantile 或线上观测校准。

## 5. 可向导师汇报的结论

1. RescueSched 的论文问题已经从负载均衡转成 SLO rescue：只迁移 queued-but-not-running 且 locally doomed、remote feasible、target safe 的任务。
2. 在 W3 rho=0.85 主场景中，收益不是靠迁移更多任务，而是靠高 BMR/低 UMR。
3. W2 burst-skew 和 10-seed/CI 已补齐本地仿真闭环，足够作为下一轮组会材料。
4. 高负载或无 slack 时 RescueSched 会抑制迁移；它不声称解决全局过载。

## 6. 仍需谨慎的边界

- target safety 的实际 harmful 统计在当前 FIFO append-to-tail 模型下通常为 0；它验证的是 predicted unsafe exposure，而不是强 target-induced miss 现象。
- migration-cost microbench 是本地笔记本、非 CPU pinning 的粗粒度估计，只能支持扫参范围，不能替代 CloudLab 成本测量。
- service-time estimate 目前只有 mean/noisy-oracle 消融，真实系统需要基于 RPC 历史或类别的预测器。

## 7. Generated Figures

- `figures/fig_w2_burst_slo_vs_rho.png`
- `figures/fig_w2_burst_slo_vs_rho.pdf`
- `figures/fig_rescue_calibration.png`
- `figures/fig_rescue_calibration.pdf`
