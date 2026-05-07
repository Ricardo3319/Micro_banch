# Step-03 Tier-3 Summary

## Scope
- Step ID: `step-03-tier3`
- Round date: 2026-03-12
- 场景: W3（Poisson + Lognormal σ=1.0）+ 边界负例
- 目标: 补齐重尾泛化与负例边界

## Inputs Reviewed
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-02-tier2/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## Experiment Configuration

| 参数 | 值 |
|------|------|
| 集群拓扑 | 64 hosts × 16 cores = 1024 cores，均质 C=1.0 |
| W3 工作负载 | Poisson + Lognormal (σ=1.0, μ≈2.678, E[S]=24us) |
| 冻结常量 | T_host=2.1us, T_net=3.15us, SYNC=10us |
| M0 参数 | α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us, budget=5% (effective 4.5%) |
| B2 参数 | K_DST=2, cooldown=2us, budget=5% |
| 统计协议 | warmup=200k, measurement=1M, seeds={11,23,37,47,59} |
| W3 代表点 | rho={0.50, 0.70, 0.85, 0.92} |
| 总运行数 | 60 (W3 main) + 15 (W1 boundary) + 15 (W3 boundary) + 15 (W3 overload) = 105 runs |

## New Implementations

### W3 Lognormal Workload
- `LognormalService` 生成器: `include/sim/workloads/generators.h`
- 参数: μ=ln(24)−0.5=2.678, σ=1.0 → E[S]=exp(μ+σ²/2)=24us
- `WorkloadType::W3_POISSON_LOGNORMAL` 枚举: `include/sim/common/types.h`
- 常量: `include/sim/common/constants.h` (W3_LOGNORMAL_MU, W3_LOGNORMAL_SIGMA, W3_MEAN_SERVICE_US)
- 到达过程复用 PoissonArrival，λ_global = ρ × 1024 / 24

### Lognormal 分布特性
- CV(Lognormal σ=1.0) = √(e¹−1) ≈ 1.31（高变异系数）
- 对比：Bimodal CV ≈ 1.63，但 Lognormal 的尾部是连续的
- Lognormal 在 P99 处的分位数 ≈ exp(μ+2.326σ) ≈ 147us，P999 ≈ exp(μ+3.09σ) ≈ 321us
- 这意味着即使无排队，服务时间本身就会产生较高的 P99

## Gate Result

**Step-03 status: PASS**

验收标准复核：
- ✓ 完成 W3 代表点运行（4 rho × 3 methods × 5 seeds = 60 runs）
- ✓ 至少 1 个可信负例且机制解释完整（W3 rho=0.95 M0 P999 退化, 5/5 seeds 一致）
- ✓ 重尾对比结论与 W1/W2 方向自洽

## Final Results

### Median P99 对比表（5 seeds 中位数，单位 us）

| rho | B1 | B2 | M0 | M0 vs B2 | M0 vs B1 | Seeds 一致 |
|------|------|------|------|----------|----------|-----------|
| 0.50 | 154 | 154 | 154 | 0% | 0% | — |
| 0.70 | 170 | 160 | 158 | **+1.3%** | **+7.1%** | 4/5 |
| 0.85 | 202 | 200 | 180 | **+10.0%** | **+10.9%** | 5/5 |
| 0.92 | 304 | 322 | 302 | **+6.2%** | +0.7% | 4/5 |

### Median P999 对比表（5 seeds 中位数，单位 us）

| rho | B1 | B2 | M0 | M0 vs B2 | M0 vs B1 | Seeds 一致 |
|------|------|------|------|----------|----------|-----------|
| 0.50 | 324 | 324 | 324 | 0% | 0% | — |
| 0.70 | 354 | 330 | 328 | +0.6% | **+7.3%** | 3/5 |
| 0.85 | 400 | 394 | 344 | **+12.7%** | **+14.0%** | 5/5 |
| 0.92 | 528 | 556 | 502 | **+9.7%** | +4.9% | 4/5 |

### Migration Metrics（M0，5 seeds 中位数）

| rho | mr | imr | mr≤0.05? | imr≤0.30? |
|------|------|------|----------|-----------|
| 0.50 | 0.001 | 0 | ✓ | ✓ |
| 0.70 | 0.030 | 0.000 | ✓ | ✓ |
| 0.85 | 0.045 | 0.052 | ✓ | ✓ |
| 0.92 | 0.045 | 0.181 | ✓ | ✓ |

### Migration Metrics（B2，5 seeds 中位数）

| rho | mr | imr | mr≤0.05? | imr≤0.30? |
|------|------|------|----------|-----------|
| 0.50 | 0.002 | 0.131 | ✓ | ✓ |
| 0.70 | 0.027 | 0.149 | ✓ | ✓ |
| 0.85 | 0.022 | 0.298 | ✓ | ✓ (borderline) |
| 0.92 | 0.005 | 0.299 | ✓ | ✓ (borderline) |

## 跨工作负载对比（M0 vs B2 P99 改善率，中位数口径）

| rho | W1 (Bimodal) | W2 (MMPP+Bimodal) | W3 (Lognormal) |
|------|-------------|-------------------|----------------|
| 0.50 | 0% | +65.9% | 0% |
| 0.70 | +1.8% | — | +1.3% |
| 0.85 | 0% | +40.1% | **+10.0%** |
| 0.92 | — | — | **+6.2%** |

**结论自洽性**: W1 < W3 < W2，M0 优势随工作负载变异性/突发性递增。

## 跨工作负载 P99 绝对值对比（M0 P99 中位数，us）

| rho | W1 | W3 | W3/W1 比率 |
|------|------|------|-----------|
| 0.50 | 106 | 154 | 1.45× |
| 0.70 | 112 | 158 | 1.41× |
| 0.85 | 202 | 180 | 0.89× |

W3 在低中负载的绝对 P99 高于 W1（因 Lognormal 服务时间分位数≈147us vs Bimodal 100us），但在 rho=0.85 处 M0+W3 的 P99 反而低于 M0+W1，因为 M0 在 W3 下成功迁移了更多即将违约的重尾任务。

## Mechanism Analysis

### 为什么 M0 在 W3 下显现优势（与 W1 对照）

1. **Lognormal 连续重尾 vs Bimodal 离散双模态**: Bimodal 只有 5us/100us 两种服务时间，排队行为高度可预测，Power-of-2 分发已足够均衡。Lognormal 的连续重尾使得排队时间方差大幅增加，产生更多"意外"排队积压场景。

2. **M0 预测扫描的有效命中率提升**: Lognormal 下，同一核心队列中可能同时存在 3us 和 200us+ 的任务。当一个 200us 长任务阻塞了后续短任务，M0 的 CHECK_MIGRATION 能通过 Wi 预测精确识别这些被阻塞的短任务，并在违约前迁移。Bimodal 下类似场景只有 100us 一种长任务，触发频率较低。

3. **迁移收益/成本比改善**: 在 rho=0.85，M0 下的 imr=0.052（仅 5.2% 无效迁移），远低于 B2 的 imr=0.298（30%）。M0 的三重判定（风险+收益+防抖）在连续分布下比 B2 的阈值触发更精准。

### 为什么 M0 在 W3 下仍弱于 W2

W2 的 MMPP 突发到达创造了局部化热点（16 热节点吸收 50% 突发流量），迫使部分主机严重过载。这种**空间不均衡**是 M0 最大的优势场景。W3 仍然是 Poisson 到达（空间均匀），M0 的优势仅来自**服务时间变异性**，而非到达率变异性。

### 负例与退化场景

详见 `boundary_case.md`。核心负例：
- **W3 rho=0.95**: M0 P999=2050us，B2 P999=1660us（M0 劣 23.5%），5/5 seeds 一致
- **机制**: 近饱和下 M0 仍消耗 4.6% 迁移预算，但所有目标主机均满负载，迁移增加 5.25us 固定开销却无法缩短排队

## Lessons Learned

1. **M0 优势的三级递进**: W1（无优势）→ W3（中等优势，+10% at rho=0.85）→ W2（显著优势，+40%）。决定因素是局部排队不均衡的程度：到达率突发 > 服务时间重尾 > 平滑到达。

2. **Lognormal 的 SLO 评估陷阱**: Lognormal σ=1.0 的 P99 服务时间≈147us，已逼近 SLO_long=200us。即使无排队延迟，约 0.5% 的任务在到达后即接近违约边界。这使得 M0 的 α=0.8 风险阈值更容易触发，产生了更多"必要"的迁移。

3. **B2 在 W3 重尾下的 imr 恶化**: B2 在 rho=0.85 的 imr 达到 0.298（接近 0.30 上限），因为 Lognormal 的高变异性使得 B2 的阈值触发机制（基于队列长度）更容易产生误判。相比之下，M0 基于排队论 Wi 预测的 imr 仅为 0.052。

4. **过载退化一致性**: W3 rho=0.95 的 M0 退化模式与 W1 rho=0.95 一致（P999 恶化），确认这是 M0 在系统饱和时的通用弱点，而非工作负载特异性问题。

## Key Deliverables

| 文件 | 说明 |
|------|------|
| `summary.md` | 本文件 — Step-03 结论与验收判定 |
| `metrics_table.csv` | 全量原始数据 (105 runs) |
| `boundary_case.md` | 边界负例分析 |
| `run_log.md` | 命令与验证记录 |
| `next_prompt.md` | Step-04 可直接复制的提示词 |

## Next Step
- 进入 Step-04（统计封板与复现封板），固定所有统计结果、生成 bootstrap CI、产出可一键重建命令清单。
