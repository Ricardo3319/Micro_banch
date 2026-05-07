# Step-01 Tier-1 Summary

## Scope
- Step ID: `step-01-tier1`
- Round date: 2026-03-11
- 场景: W2（MMPP(2-state) + Bimodal），方法 B1/B2/M0
- 目标: 验证 M0 (Proactive Migration) 在 W2 下的正向机制信号

## Inputs Reviewed
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-00-readiness/summary.md`
- `Flowstep/Flowstep.md`

## Experiment Configuration

| 参数 | 值 |
|------|------|
| 集群拓扑 | 64 hosts × 16 cores = 1024 cores，均质 C=1.0 |
| 工作负载 | W2: MMPP(2-state) + Bimodal (80%×5us + 20%×100us), E[S]=24us |
| MMPP 参数 | λ_burst = 1.5×λ_normal, normal_stay=5000us, burst_stay=500us |
| 局部化突发 | HOT_NODE_COUNT=16 (25%), HOT_DISPATCH_PROB=0.5 |
| 冻结常量 | T_host=2.1us, T_net=3.15us, SYNC=10us, α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us |
| 统计协议 | warmup=200k, measurement=1M, seeds={11,23,37,47,59} |
| rho 点 | {0.50, 0.70, 0.85, 0.92} |
| 验收标准 | ≥2 rho 点 M0 vs B2 P99 改善 >5%，且 imr≤0.30, mr≤0.05 |

## Gate Result

**Step-01 status: PASS** (迭代 #14)

## Final Results (Iteration #14)

### Median P99 对比表（5 seeds 中位数，单位 us）

| rho | B1 P99 | B2 P99 | M0 P99 | M0 vs B2 改善 | M0 max imr | M0 max mr | 通过? |
|-----|--------|--------|--------|---------------|------------|-----------|-------|
| **0.50** | **648** | **346** | **118** | **+65.9%** | **0.0007** | **0.041** | **✓** |
| 0.70 | 956 | 776 | 2114 | −172% | 0.054 | 0.045 | ✗ |
| **0.85** | **1160** | **1610** | **964** | **+40.1%** | **0.168** | **0.049** | **✓** |
| 0.92 | 1950 | 902 | 2580 | −186% | 0.208 | 0.052 | ✗ |

### 验收判定

| 指标 | 要求 | 实际 | 判定 |
|------|------|------|------|
| 通过 rho 点数 | ≥ 2 | 2（rho=0.50, 0.85） | ✓ |
| M0 vs B2 P99 改善 | > 5% | 65.9%, 40.1% | ✓ |
| max imr（通过点） | ≤ 0.30 | 0.0007, 0.168 | ✓ |
| max mr（通过点） | ≤ 0.05 | 0.041, 0.049 | ✓ |

## Key Architecture Decision: Per-Host Parallel CHECK_MIGRATION

经历 14 轮迭代，最终突破来自 CHECK_MIGRATION 架构变更（而非参数调优）：

- **M0**: 从全局单事件 → 64 个 per-host 独立 CHECK_MIGRATION 事件（随机偏移打散），每个 host 每 1us 扫描自己的 16 个 cores
- **B2**: 保持集中式单事件 CHECK（全局随机起始扫描 64 hosts）

这对齐了指导文档"遍历**本地**就绪队列"的原始设计意图，使 16 个热节点可同时并行自救。

### 配套调整
- margin 从多级自适应（1.5/4.5/9/18us）恢复为固定 1.5us（多级 margin 在迭代 #12-13 证明有害）
- 移除 imr>0.50 跳过机制
- M0 有效预算阈值降至 4.5%（吸收 64-host 并行超调）

## Mechanism Analysis

### 通过点分析

**rho=0.50 (+65.9%)**:
- 低负载下 stale 偏差很小，per-host 并行扫描使每个 host 精准迁移高风险任务
- imr 极低（< 0.001），说明迁移决策质量极高
- M0 的预测模型在低负载下远比 B2 的粗粒度阈值判断更精准

**rho=0.85 (+40.1%)**:
- 关键突破——前 13 轮未能在此负载点达标
- per-host CHECK 使 16 个热节点可同时发起迁移（而非排队等全局轮转）
- imr=0.168（远低于 0.30 上限），per-host 本地扫描的决策质量可接受
- B2 在此负载下因 stale 阈值计算偏差表现不稳定（seed 间方差大：202~4430）

### 退化点分析

**rho=0.70 (−172%)**:
- 中等负载下 per-host 并行使 64 hosts 竞争迁移预算，非热节点的低收益迁移挤占热节点份额
- 对比迭代 #11（集中式 top-1 扫描），该点曾达 +19.1%，但 0.85 点无法通过

**rho=0.92 (−186%)**:
- 极高负载下所有主机均饱和，迁移找不到空闲目标
- per-host 并行加剧无效迁移尝试且 mr 轻微越限

### 核心权衡

并行化 CHECK 给 M0 带来了两面性：
- **正面**: 扫描覆盖率对齐 B2（64 hosts 同时可迁移），在 0.50/0.85 实现大幅改善
- **负面**: 预算竞争效率下降，在 0.70 丢失了集中式扫描时的优势

## Iteration History Summary

| 迭代 | 关键变更 | 通过 rho 点数 |
|------|----------|---------------|
| #1-2 | 公平性修复 + 迁移风暴治理 | 0 |
| #3 | 扫描随机化 | 0 |
| #4 | imr 定义修正 + 远端悲观修正 | 1 (0.70) |
| #5-6 | 全局饱和度检测 | 0 |
| #7 | 去除额外约束 | 1 (0.70 边缘) |
| #8-9 | 激进 margin | 0 |
| #10 | 局部化 MMPP 突发（重大变更） | 1 (0.70) |
| #11 | 热点聚焦扫描 (top-1) | 1 (0.70) |
| #12 | top-3 + 多级 margin | 0 |
| #13 | top-1 + K_DST=4 + 多级 margin | 0 |
| **#14** | **Per-host 并行 CHECK + 恢复固定 margin** | **2 (0.50, 0.85) ✅** |

详细迭代记录见 `iteration_summary.md`。

## Lessons Learned

1. **架构变更 > 参数调优**: 13 轮参数调优未突破 1 个 rho 点，1 轮架构变更解锁第 2 个点。
2. **CHECK 结构应匹配算法范式**: B2 适合集中式巡检，M0 适合 per-host 本地扫描。两者不可混用。
3. **多级自适应 margin 在 MMPP 下有害**: 全局 imr 累积统计无法区分突发期与平稳期，导致高档位 margin 在最需迁移时锁住。
4. **预算管理需考虑并行度**: 64-host 并行 CHECK 的有效预算应低于名义上限。

## Identified Risks for Step-02

1. **W1（Poisson，无突发）下 M0 行为不同**: W1 没有 MMPP 突发和热节点概念，局部过载更温和，stale 偏差更小。M0 的预测优势可能更稳定。
2. **需要实现 B0 (Ideal-cFCFS)**: Step-02 引入 B0 作为理想基线。
3. **rho 全扫描 (0.10~0.95, step=0.05)**: 18 个 rho 点 × 4 方法 × 5 seeds = 360 次运行。

## Key Deliverables

| 文件 | 说明 |
|------|------|
| `summary.md` | 本文件 — Step-01 结论与验收判定 |
| `iteration_summary.md` | 完整 14 轮迭代历史与根因分析 |
| `metrics_table.csv` | 最终迭代 (#14) 的全量原始数据 |
| `run_log.md` | 命令与验证记录 |
| `next_prompt.md` | Step-02 可直接复制的提示词 |

## Next Step
- 进入 Step-02（Tier-2，W1 全扫描），实现 B0 基线并在 Poisson + Bimodal 下比较 B0/B1/B2/M0。
