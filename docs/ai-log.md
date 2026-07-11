# AI Log

## 2026-07-12 RescueSched validity milestone

- AI system: OpenAI Codex, GPT-5 family.
- Purpose: repository audit, paper-claim scoping, simulator refactoring,
  deterministic trace design, tests, and documentation.
- AI contribution: proposed and implemented changes under human-selected
  requirements. It did not generate experimental observations or invent
  citations.
- Human validation required: inspect all model semantics, review every diff,
  run the complete CTest suite, reproduce the versioned CSV twice, and approve
  every paper claim and citation before submission.
- Disclosure status: this work materially affects simulation software and
  experimental methodology and therefore must be described in the paper's
  `Use of AI Disclosure` section.
- Current paper line: RescueSched only. AQB/DQB code and artifacts are retained
  as legacy history and are excluded from current evidence.
- Implemented validation: explicit RPC methods and deadlines, method-keyed
  estimators, policy-independent SHA-256 traces, paid migration events, target
  reservations, calibrated offered load, task-ID measurement cohorts, exact
  percentiles, and strict CSV v2.
- Automated verification: Release build plus seven CTest cases, including two
  byte-identical independently generated result files. No historical CSV was
  rewritten during validation.

更新时间：2026-07-06

本文档记录本次 AI 协作整理过程，便于后续审计哪些信息来自代码扫描、哪些是复现规划、哪些仍需人工确认。

## 本次用户目标

用户要求为科研仿真项目创建标准化复现文档体系，在 `docs/` 下生成：

- `01-project-overview.md`
- `02-simulation-analysis.md`
- `03-physical-mapping.md`
- `04-experiment-design.md`
- `05-implementation-plan.md`
- `06-test-record.md`
- `07-result-analysis.md`
- `08-paper-writing-notes.md`
- `ai-log.md`
- `reflection.md`

本轮要求只生成文档初稿，不修改核心代码。

## 本次执行原则

- 不联网。
- 不安装依赖。
- 不修改核心代码。
- 不编造不存在的物理实验结果。
- 不确定信息标记为【待确认】。
- 需要从代码确认的信息引用具体路径。

## 扫描依据

本次文档主要依据以下项目文件：

- `src/app/main.cpp`
- `src/core/simulator.cpp`
- `include/sim/core/simulator.h`
- `include/sim/common/types.h`
- `include/sim/common/constants.h`
- `include/sim/workloads/generators.h`
- `include/sim/metrics/stats.h`
- `CMakeLists.txt`
- `config/rescuesched.yaml`
- `tests/integration/validate_rescue_csv_schema.py`
- `scripts/rescue_analysis.py`
- `scripts/infocom_readiness_analysis.py`
- `scripts/generate_charts.py`
- `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md`
- `docs/ARTIFACT_PROVENANCE.md`

## 已知事实

- 项目是 C++17 离散事件仿真项目。
- 当前主线按用户说明聚焦 RescueSched。
- 主入口为 `src/app/main.cpp`。
- 核心仿真逻辑为 `src/core/simulator.cpp`。
- workload 类型定义在 `include/sim/common/types.h`。
- workload generator 位于 `include/sim/workloads/generators.h`。
- RescueSched 相关指标位于 `include/sim/metrics/stats.h`。
- 当前已有最小 CTest gate。
- 当前已有图表溯源文档和物理复现计划文档。

## 本次生成内容

本次新增 10 个文档初稿，用于形成持续维护的复现文档体系。

文档覆盖：

- 项目概览。
- 仿真系统分析。
- 仿真变量到物理机变量映射。
- 物理机实验设计。
- 分阶段实现计划。
- 测试记录模板。
- 结果分析模板。
- 论文写作素材。
- AI 协作日志。
- 当前阶段复盘。

## 未确认内容

- 物理 runtime 的最终技术路线。
- CloudLab 具体机器型号、节点数和拓扑。
- 物理机上的 service-time 控制方式。
- trace replay 输入格式是否采用本文档建议 schema。
- beneficial/useless migration 的物理反事实估计方法。
- 是否将 DQB/AQB 作为论文主实验 baseline。

## 后续维护建议

- 每次实验后更新 `docs/06-test-record.md`。
- 每次生成图表后更新 `docs/ARTIFACT_PROVENANCE.md` 或在结果目录写 manifest。
- 每次物理机设计变化后更新 `docs/03-physical-mapping.md` 和 `docs/04-experiment-design.md`。
- 写论文前从 `docs/08-paper-writing-notes.md` 中筛选素材，不直接照搬未确认内容。
