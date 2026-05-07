# Step-00 Config Snapshot（冻结参数快照）

> Frozen as of: 2026-03-11
> Source: `微秒级主动迁移调度仿真实验计划.md`

---

## 1 集群拓扑

| 参数 | 值 | 来源 |
|------|----|------|
| `num_hosts` | 64 | §2 |
| `cores_per_host` | 16 | §2 |
| 总核心 | 1024 | 推导 |
| 标准算力系数 `C_j` | 1.0 | §指导文档 1.1 |
| 慢节点算力系数 `C_j` | 0.2（实验二异构用） | §指导文档 5.2 |

---

## 2 物理常数

| 常数 | 值 | 单位 | 来源 |
|------|----|------|------|
| `T_host_us` | 2.1 | us | §2 |
| `T_net_oneway_us` | 3.15 | us | §2 |
| `T_rpc_us` | 6.3 | us | 指导文档 |
| 迁移单向总惩罚 | 5.25 | us | 3.15 + 2.1 |

---

## 3 SLO 分层

| 分类 | 阈值 | 默认 SLO | 敏感性扫描 |
|------|------|----------|-----------|
| 短任务 | `E ≤ 20 us` | 40 us | (30, 150), (40, 200), (50, 250) |
| 长任务 | `E > 20 us` | 200 us | 同上 |

---

## 4 统计协议

| 参数 | 值 |
|------|----|
| warm-up | 200,000 requests |
| measurement window | 1,000,000 requests |
| 独立运行次数 / 配置 | 5 |
| seed 集合 | `{11, 23, 37, 47, 59}` |
| 显著性判据 | 95% CI 不重叠 + 相对提升 >5% + ≥4/5 seeds 方向一致 |
| P99/P99.9 区间 | Bootstrap 95% CI（固定重采样次数，全方法统一） |

---

## 5 方法集合

| 标签 | 全名 | 角色 |
|------|------|------|
| B0 | Host-LocalOnly | 入口分发，不迁移 |
| B1 | Host-Power-of-k | 入口 k 选最短主机，不迁移 |
| B2 | Host-ReactiveMigration | 阈值被动迁移 |
| M0 | Host-ProactiveMigration | 预测型主动迁移（目标方法） |
| U0 | Ideal-Oracle | 上界参考，不参与显著性对比 |

---

## 6 B2 参数

| 参数 | 值 |
|------|----|
| `Q_hi` | p75(Q_host)，预热统计后冻结 |
| `Q_lo` | p25(Q_host)，预热统计后冻结 |
| `n_move` | 1（尾部选择） |
| `k_dst` | 2 |
| `T_cooldown_us` | 2 |
| `H_q` | 2 |
| `r_bad` | 0.3 |
| 迁移预算上限 | 5% |

---

## 7 M0 参数

| 参数 | 值 |
|------|----|
| `T_check_us` | 1.0 |
| `alpha` | 0.8 |
| `T_margin_us` | 1.5 |
| `k_dst` | 2 |
| `n_move_per_check` | 1 |
| 风暴保护 | 同 B2 级预算与冷却；`invalid_migration_ratio > 0.3` 时上调 `T_margin_us` |

### M0 三重判定

1. **风险**：$T_{\text{elapsed}} + W_i + E_i / C_{\text{src}} > \alpha \times SLO_i$
2. **收益**：$\text{local\_total}_i > \text{remote\_total}_i$
3. **防抖**：$\text{local\_total}_i > \text{remote\_total}_i + T_{\text{margin\_us}}$

---

## 8 迁移副作用上限

| 指标 | 阈值 | 口径 |
|------|------|------|
| `invalid_migration_ratio` | ≤ 0.30 | 窗口统计 |
| `migration_rate` | ≤ 0.05 | 系统级窗口 |

---

## 9 负载场景

### W1 — Poisson + Bimodal（主结果）

| 参数 | 值 |
|------|----|
| 到达过程 | Poisson |
| 服务时间 | Bimodal: 80% × 5 us, 20% × 100 us |
| $E[S]$ | 24 us |
| rho 扫描 | 0.10 … 0.95, step 0.05 |
| $\lambda_{\text{global}}$ | $\rho \times 1024 / 24$ (req/us) |

### W2 — MMPP(2-state) + Bimodal（突发鲁棒性）

| 参数 | 值 |
|------|----|
| 状态 | Normal / Burst |
| $\lambda_{\text{burst}}$ | $1.5 \times \lambda_{\text{normal}}$ |
| 平均停留 | Normal = 5000 us, Burst = 500 us |
| 初始状态 | Normal |
| $E[S]$ | 24 us（同 W1 Bimodal） |
| 稳态概率 | $\pi_N = 10/11$, $\pi_B = 1/11$ |
| 时间平均到达率 | $\bar{\lambda} = \lambda_N \times 11.5 / 11$ |
| $\lambda_N$ 推导 | $\lambda_N = \frac{\rho \times 1024}{24} \times \frac{11}{11.5}$ |
| $\lambda_B$ 推导 | $\lambda_B = 1.5 \times \lambda_N$ |
| rho 代表点 | {0.50, 0.70, 0.85, 0.92} |

### W3 — Poisson + Lognormal（重尾泛化）

| 参数 | 值 |
|------|----|
| 到达过程 | Poisson |
| 服务时间 | Lognormal($\mu$, $\sigma$) |
| $\sigma$ | 1.0 |
| $\mu$ | $\ln(24) - 0.5 \times 1.0^2 \approx 2.678$ |
| $E[S]$ | 24 us |
| $\lambda_{\text{global}}$ | $\rho \times 1024 / 24$ (req/us)（与 W1 同公式） |
| rho 代表点 | {0.50, 0.70, 0.85, 0.92} |

---

## 10 事件优先级

同时间戳排序（数值越大越先处理）：

| 优先级 | 事件类型 |
|:------:|---------|
| 3 | `TASK_FINISH` |
| 2 | `TASK_ARRIVE` |
| 1 | `TASK_GENERATE` |

其余事件（`TASK_EXECUTE`, `SYNC_LOAD`, `CHECK_MIGRATION`）不出现同时间戳竞争。

---

## 11 Seed 与复现命令模板

```
# 伪命令模板（待仿真器实现后替换为真实命令）
./simulator \
  --method={B0|B1|B2|M0|U0} \
  --workload={W1|W2|W3} \
  --rho=<value> \
  --seed=<11|23|37|47|59> \
  --num_hosts=64 \
  --cores_per_host=16 \
  --warmup=200000 \
  --measurement=1000000 \
  --T_host_us=2.1 \
  --T_net_us=3.15 \
  --sync_period_us=<value> \
  --check_period_us=1.0 \
  --alpha=0.8 \
  --T_margin_us=1.5 \
  --output_dir=artifacts/raw_logs/<method>_<workload>_rho<rho>_seed<seed>/
```

复现条件：固定 seed + 固定参数 → 要求事件队列完全确定性（无浮点非确定性、无并发竞争）。
