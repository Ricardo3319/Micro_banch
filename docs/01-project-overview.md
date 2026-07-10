# 01 Project Overview

更新时间：2026-07-06

## 项目目标

本项目是一个 C++17 离散事件科研仿真项目，用于研究微秒级 RPC/任务队列中的尾延迟控制、任务迁移与队列修复策略。当前主线按照用户说明聚焦 `RescueSched`，即在主机内部多核队列之间识别“本地将违约”的任务，并尝试迁移到更安全的目标 core，以降低 P99/P999 延迟和 SLO 违约率。

项目中仍保留早期 DQB/AQB/主动迁移相关实现和结果文档；这些内容可作为背景与 baseline 参考，但当前复现体系以 RescueSched 为主。

## 主要研究问题

- 在高负载、突发流量、长尾服务时间场景下，队列中的哪些任务应被抢救迁移。
- 如何在不伤害目标队列任务的前提下降低尾延迟。
- RescueSched 相比 `L1_WorkStealing`、`M0_IntraHostProactive`、`L0_RandomCore` 等 baseline 的收益、代价和边界条件。
- 仿真中的参数、指标和结果如何迁移到真实物理机环境中复现。

## 代码结构

| 路径 | 类型 | 作用 |
| --- | --- | --- |
| `src/app/main.cpp` | 实验入口 | CLI/config 解析、实验 mode 调度、CSV 输出。 |
| `src/core/simulator.cpp` | 仿真核心 | 事件循环、任务生成/到达/完成、迁移提交、RescueSched 检查逻辑。 |
| `include/sim/core/simulator.h` | 核心接口 | `Simulator::configure()`、`run()`、RescueSched 相关成员声明。 |
| `include/sim/common/types.h` | 类型定义 | `MethodType`、`WorkloadType`、集群 profile 枚举。 |
| `include/sim/common/constants.h` | 常量配置 | SLO、workload、seed、RescueSched 参数、M0/AQB/DQB 参数。 |
| `include/sim/workloads/generators.h` | 工作负载 | W1/W2/W3 的服务时间和到达过程生成器。 |
| `include/sim/metrics/stats.h` | 指标统计 | 延迟、迁移、RescueSched 计数器和派生比例。 |
| `include/sim/algorithms/` | 算法头文件 | legacy host-level 算法与 DQB/AQB 相关策略。 |
| `scripts/` | 脚本 | 构建门禁、图表、RescueSched/INFOCOM 分析。 |
| `config/` | 配置 | `config/default.yaml`、`config/rescuesched.yaml`。 |
| `artifacts/` | 实验输出 | 已有 CSV、summary、figure、readiness 结果。 |
| `tests/integration/` | 测试 | RescueSched CSV schema 校验脚本。 |
| `docs/` | 文档 | 架构、实验、物理映射、图表溯源与复现计划。 |

## 运行入口

主要入口：

```bash
cmake -S . -B build
cmake --build build --config Release
./build/simulator --help
```

RescueSched smoke：

```bash
./build/simulator --mode rescue-smoke
```

RescueSched 主实验：

```bash
./build/simulator --mode rescue-main --workload W3 --rho 0.85 --seed 11
```

配置文件入口：

```bash
./build/simulator --config config/rescuesched.yaml
```

Step-00 门禁脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_step00.ps1
```

## 当前完成度

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 仿真核心 | 已实现 | `src/core/simulator.cpp` 中已有事件循环、workload、迁移和 RescueSched 逻辑。 |
| RescueSched 主线 | 已实现 | `M1_RESCUE_SCHED`、no-target-safety、no-rescuable、hybrid 等变体存在。 |
| CLI/config | 已初步统一 | `mode`、`workload`、`rho`、`seed`、输出路径可配置。 |
| baseline | 已有 | `L0_RandomCore`、`L1_WorkStealing`、`M0_IntraHostProactive` 等。 |
| CSV 输出 | 已有 | RescueSched 表头在 `src/app/main.cpp` 的 `write_rescue_header()`。 |
| 图表分析 | 已有 | `scripts/rescue_analysis.py`、`scripts/infocom_readiness_analysis.py`、`scripts/generate_charts.py`。 |
| CTest 门禁 | 已有最小版本 | `rescue_cli_help`、`rescue_smoke_deterministic`、`rescue_csv_schema`。 |
| 物理机实现 | 未完成 | 目前是复现计划与映射，真实 trace replay/runtime 尚未实现。 |

## 是否适合迁移到物理机复现

适合进入第一阶段物理机复现准备，但还不能直接声称已有物理机实现。原因：

- 仿真变量和核心指标已经较清晰。
- RescueSched 有明确 baseline、输入参数、输出 CSV 和图表链路。
- 仍缺少真实 runtime、trace loader、物理日志 schema、CPU/NUMA 绑定脚本和物理指标采集工具链。

结论：适合先做最小 Demo 级物理映射与 microbench，对完整论文级物理复现实验还需要补工程闭环。
