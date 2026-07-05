# Step-18 INFOCOM Readiness Summary

本报告由 `python scripts/infocom_readiness_analysis.py` 生成，目标是判断 RescueSched 是否已经从本地预实验推进到 INFOCOM 投稿前验证状态。

## 1. 当前主 Claim 判断

主 claim 可以作为 INFOCOM 预备 claim：RescueSched 在 heavy-tail、有局部 slack 的单 host 多核队列中，通过 rescuability-aware migration 提高迁移质量并降低 SLO violation。

仍不能写成最终论文强 claim：它不是通用负载均衡器，不解决全局过载；target safety 的真实 harmful 证据依赖 stress 模型；真实系统结论仍需要 CloudLab 或 trace replay。

## 2. W3 主结果与 95% CI

| method | SLO mean | 95% CI | note |
|---|---:|---:|---|
| L1_WorkStealing | 0.20377 | [0.200519, 0.20702] | n=10 |
| M0_IntraHostProactive | 0.153904 | [0.149875, 0.157934] | n=10 |
| M1_RescueSched oracle | 0.095612 | [0.092662, 0.098563] | n=10 |

| setting | P99 | P999 | SLO | move rate | BMR | UMR | RPM |
|---|---:|---:|---:|---:|---:|---:|---:|
| L1_WorkStealing oracle | 221 | 366 | 0.202958 | 0.328147 | 0 | 0 | 0 |
| M0_IntraHostProactive oracle | 195 | 348 | 0.152486 | 0.399951 | 0 | 0 | 0 |
| M1_RescueSched oracle | 261 | 569 | 0.094485 | 0.285965 | 0.999984 | 0 | 0.999984 |

## 3. Estimator Sensitivity

| estimator | SLO | move rate | BMR | UMR | interpretation |
|---|---:|---:|---:|---:|---|
| oracle | 0.094485 | 0.285965 | 0.999984 | 0 | oracle upper bound |
| class_mean | 0.136248 | 0.291415 | 0.960421 | 0.059142 | realistic/sensitivity |
| ewma | 0.142165 | 0.290945 | 0.95528 | 0.06791 | realistic/sensitivity |
| quantile_guard | 0.165368 | 0.307499 | 0.989715 | 0.013521 | realistic/sensitivity |
| mean | 0.155633 | 0.305844 | 0.984062 | 0.019728 | realistic/sensitivity |
| noisy_oracle | 0.132161 | 0.292324 | 0.951785 | 0.06962 | realistic/sensitivity |

## 4. Migration Cost Sensitivity

| setting | migration_cost_us | SLO | move rate | BMR | UMR |
|---|---:|---:|---:|---:|---:|
| default | 0.5 | 0.094486 | 0.285511 | 0.999982 | 0 |
| measured local handoff upper bound | 7.73805 | 0.113438 | 0.269818 | 0.999978 | 0 |

## 5. W2 Boundary 与 Hybrid 必要性

W2 boundary sweep 中，NoRescuable 在 23/36 个 hot-core/prob/rho 组合上 SLO 低于标准 RescueSched。

| method | rho | SLO | move rate | BMR | UMR | relief moves | relief BMR |
|---|---:|---:|---:|---:|---:|---:|---:|
| M1_RescueSched | 0.85 | 0.687816 | 0.129427 | 0.999981 | 0 | 0 | 0 |
| M1_RescueSched_NoRescuable | 0.85 | 0.539359 | 0.33731 | 0.461836 | 0.538149 | 0 | 0 |
| M1_RescueSched_Hybrid | 0.85 | 0.680379 | 0.133911 | 0.999863 | 0.000121 | 398 | 1 |

解释：W2 burst-skew 下 NoRescuable 的优势说明纯 SLO rescue 不是所有 burst/skew 场景的最优 SLO 策略；Hybrid 是投稿前需要继续打磨的边界策略，而不是 W3 主 claim 的必要条件。

## 6. Target Safety Stress

| policy | method | PredUnsafe | HarmActual | TargetInducedMiss | SLO |
|---|---|---:|---:|---:|---:|
| append_tail | M1_RescueSched_NoTargetSafety | 8643 | 0 | 0 | 0.095297 |
| head_insert_stress | M1_RescueSched_NoTargetSafety | 19206 | 35449 | 39336 | 0.13672 |

解释：append-tail 是真实默认模型；head-insert-stress 是为了让 target-side delay 可观测的压力模型。若 stress 下 NoTargetSafety 明显增加 actual harm，可把 target safety 写成 stress/ablation 证据；真实系统仍要用 CloudLab trace 复核。

## 7. Overload Sanity

W1 rho=0.95 下 RescueSched SLO=1，moves=0。这支持“不解决全局过载、不创造容量”的边界叙事。

## 8. INFOCOM 风险清单

强结论：W3 heavy-tail rho=0.85 的 SLO gain、低迁移率、高 BMR/低 UMR；NoRescuable 在 W3 的迁移质量退化；全局过载不误迁移。
中等结论：realistic estimator 下仍保留多少收益；measured local handoff cost 对 rescue window 的影响；target safety stress 是否产生 actual harm。
必须上 CloudLab/trace 后才能写成主结论：真实 migration cost、真实 RPC service-time estimator、真实 target-side interference、W2 burst/skew 是否需要 Hybrid。
