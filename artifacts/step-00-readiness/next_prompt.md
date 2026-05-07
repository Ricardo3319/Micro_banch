你现在执行 Step-01（仅 Tier-1，场景 W2）。

必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-00-readiness/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

目标：在 W2 下比较 B1/B2/M0，验证正向机制信号。

执行内容：
1. 实现或修复 W2 工作负载生成（MMPP + Bimodal）及 B1/B2/M0 运行链路。
2. 运行代表负载点并导出 `P99`、`P99.9`、`slo_violation_rate`、`migration_rate`、`invalid_migration_ratio`。
3. 检查验收阈值：至少 2 个负载点上 M0 相对 B2 的 `P99` 改善 > 5%，且 `invalid_migration_ratio <= 0.30`、`migration_rate <= 0.05`。
4. 保持冻结协议与公平性硬约束，不得引入跨主机实时信息读取。

输出并落盘到 `artifacts/step-01-tier1/`：
- `summary.md`
- `metrics_table.csv`
- `run_log.md`
- `next_prompt.md`

验收标准：
- 完成 Tier-1 指标导出并可复现。
- 给出 Step-01 PASS/FAIL 与阻塞项（若有）。
