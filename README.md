# Micro_banch

## 当前研究（2026-09-26）

跨主机微秒 RPC 的队列放置与批量重调度。已完成 **4640 次正式仿真运行**；结果不支持把批量主动迁移直接定为优于强基线的最终算法。真实多机验证准备使用 CloudLab 普通物理机。

- [研究决策与关键结果](docs/RESEARCH_DECISION_2026-09-26.md)
- [完整实验报告](artifacts/qbr-main-v3/研究结果.md)
- [算法、信息边界与成本模型](docs/QBR_ALGORITHM.md)
- [候选方案与基线映射](docs/QBR_RESEARCH_SPEC.md)
- [论文研究稿](docs/QBR_PAPER_DRAFT.md)
- [CloudLab 实测步骤](docs/CLOUDLAB_VALIDATION.md)
- [参考文献索引](参考文献/阅读索引.md)

### 编译与正确性检查

需要 Linux、C++17 编译器和 Python 3。当前环境使用直接编译脚本；也提供 CMake 的 `batchsched` 和 `qbr_tests` 目标。

```bash
bash scripts/build_qbr.sh
```

### 单次运行

```bash
python3 scripts/qbr_trace.py /tmp/qbr-example.bin --scenario stall --rho 0.85 --seed 11
./build-qbr/batchsched --trace /tmp/qbr-example.bin --method qbr --seed 11
```

方法：`p2c`、`jsw`、`jbsq`、`steal`、`threshold`、`single`、`qbr`；另保留两个失败候选 `work`、`bwc`。基线为论文策略级复现，并非完整硬件/数据面复现。

### 重新生成正式实验

```bash
python3 scripts/qbr_study.py --suite main --out artifacts/qbr-main-reproduction --jobs 4 --keep-outcomes
python3 scripts/qbr_study.py --suite sensitivity --out artifacts/qbr-sensitivity-reproduction --jobs 4 --keep-outcomes
python3 -m venv .venv-research
.venv-research/bin/python -m pip install matplotlib
.venv-research/bin/python scripts/qbr_analyze.py artifacts/qbr-main-reproduction --sensitivity artifacts/qbr-sensitivity-reproduction
```

模拟与实验运行器只用标准库；绘图需要 matplotlib/numpy。原绘图环境的精确依赖见 `requirements-qbr.lock`。已有正式结果在 `artifacts/qbr-main-v3` 和 `artifacts/qbr-sensitivity-v3`，约 1.9 GiB（含完整输入轨迹）；原始结果保留，重复实验请使用新目录。

主实验为 5 场景 × 4 负载 × 9 方法 × 10 种子。敏感性覆盖网络、反馈周期、控制成本、批次、主机数、链路、入口 CPU，以及预留/固定批次/JBSQ 深度。脚本对未完成组支持恢复，并拒绝混用不同模型哈希。

### CloudLab 测量工具

```bash
bash scripts/build_probe.sh
python3 scripts/probe_smoke.py
```

工具为 Linux UDP RTT 与 packet-train 测量器，本机回环仅验证协议；多机命令与资源 profile 见 CloudLab 文档。当前尚未分配 CloudLab 节点，没有真实集群结果。

### 旧实验

原 `src/core`、`src/app` 和 `step-*` 产物保留追溯。新模型不复用旧结果，也不把旧合同的“已冻结/可成文”结论沿用到本次研究。
