# Reflection

更新时间：2026-07-06

## 当前阶段判断

项目已经从“只有仿真和零散结果”的状态，进入到“可以组织复现体系”的阶段。RescueSched 主线具备以下基础：

- 明确的仿真入口。
- 可配置的 RescueSched 主实验。
- baseline 和消融变体。
- CSV 输出和分析脚本。
- 最小测试门禁。
- 初步物理复现计划。
- 图表和 artifact 溯源草案。

但项目仍处于仿真到物理机迁移的准备阶段，不能把现有结果等同于真实系统验证。

## 做得好的部分

- RescueSched 的实验链路比较集中，主要围绕 `src/app/main.cpp` 和 `src/core/simulator.cpp`。
- 指标命名较丰富，包含 tail latency、SLO、migration quality、target safety 等。
- 已经有 historical artifacts，可作为论文图表和回归检查的基础。
- CLI/config 初步建立后，后续实验矩阵更容易脚本化。
- CTest + schema validation 降低了“实验输出变了但没人发现”的风险。

## 当前主要缺口

| 类别 | 缺口 |
| --- | --- |
| 文档 | 算法公式、伪代码、真实系统架构图仍缺。 |
| 工程 | 物理 runtime、trace loader、物理日志 schema 尚未实现。 |
| 实验 | 物理机结果、trace replay、NUMA/网络 sensitivity 尚未运行。 |
| 测试 | 目前只有最小 smoke/schema，缺少算法级单元测试和分析脚本测试。 |
| 论文 | 创新点已有雏形，但还缺统一叙事和外部有效性证据。 |

## 风险

- 如果过早扩展物理机实验矩阵，可能在日志和指标尚未稳定前产生大量不可分析数据。
- 如果直接复用仿真指标名但物理口径不同，会导致结果对齐困难。
- 如果不先校准迁移成本，RescueSched 的仿真收益可能无法解释物理差异。
- 如果 P999 样本量不足，论文图可能不稳定。
- 如果 beneficial/useless migration 没有反事实估计方法，物理论文中应谨慎表述。

## 下一步任务

建议优先处理：

1. 冻结物理机日志 schema，至少包括 request log、migration log、scheduler log、manifest。
2. 实现单机最小 worker queue runtime，先支持 W3 + fixed core binding。
3. 做 physical migration-cost microbench，并和 `rescue_migration_cost_us` 对齐。
4. 编写物理 CSV 到仿真指标 schema 的转换脚本。
5. 跑第一阶段最小 Demo：W3, rho=0.85, seed=11, L1/M0/M1。

## 维护约定

- 新实验先更新设计文档，再运行。
- 新结果先进入测试记录和结果分析模板，再进入论文笔记。
- 不确定项保留【待确认】，不要在文档中默默改成事实。
- 每个 artifact 需要可追溯到命令、输入、commit 和环境。

## 当前结论

RescueSched 已经适合进入物理机最小 Demo 阶段。下一步不宜继续堆仿真实验，而应先把物理 runtime 的日志、计量和校准路径打通。只有当物理结果能与仿真 CSV 做字段级对齐后，才适合扩展到完整实验矩阵和论文图表。
