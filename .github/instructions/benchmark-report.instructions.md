---
description: "Use when writing experiment sections, benchmark reports, result analysis, figure/table captions, or paper-style evaluation content for systems research. Covers fairness, statistics, reproducibility, and academic writing conventions."
name: "Benchmark Report Academic Rules"
applyTo:
  - "**/*.md"
  - "**/*.tex"
  - "**/*.txt"
---
# Benchmark Report Academic Rules

- Write with an academic, evidence-first tone. Avoid marketing claims and absolute wording without data support.
- Separate clearly: setup, methodology, metrics, results, and threats to validity.
- Define all symbols, units, and abbreviations at first use (for example SLO, P99, us).
- Keep unit consistency across text, tables, and figures. For latency, default to microseconds (`us`) unless explicitly changed.

## Experimental Design

- Always describe workload generation details: arrival process, distribution family, key parameters, and random seed policy.
- Report cluster topology and heterogeneity assumptions explicitly (node count, cores per node, capacity factors).
- Ensure baseline fairness:
  - Same hardware and resource budget assumptions.
  - Same offered load range and workload traces.
  - No implementation-level handicaps for baselines.
- State warm-up policy, measurement window, and number of independent runs.

## Metrics And Statistics

- Mandatory primary metrics for this project:
  - Tail latency: `P99`, `P99.9`
  - `migration_rate`
  - `invalid_migration_ratio`
- Report central tendency and uncertainty together when possible (for example mean +/- 95% CI, or median with IQR).
- Prefer confidence intervals over only single-point values.
- For comparisons, report both relative (%) and absolute differences.
- If claiming improvement, include denominator and baseline reference explicitly.

## Figures And Tables

- Every figure/table must be self-contained:
  - Clear title/caption with workload and load range context.
  - Axis labels with units.
  - Legend names matching method names in text.
- Use readable scales and avoid misleading axis truncation unless explicitly justified.
- In captions, summarize the key takeaway in one sentence without overclaiming causality.
- Keep naming stable across artifacts (for example `Proactive-Migrate`, `Power-of-2`, `ZygOS`, `Ideal-cFCFS`).

## Result Interpretation

- Explain not only what changed, but why it changed in system terms (queueing delay, stale load view, migration overhead, HoL blocking).
- Distinguish correlation from mechanism; causal claims must map to measured evidence or ablation.
- Include at least one negative or failure case discussion (when improvement is small or regresses).
- Report sensitivity analyses for key knobs (for example `alpha`, `T_margin`, sync period).

## Reproducibility Checklist

- Provide software and compiler versions, compile flags, and runtime environment.
- Document configuration files or command lines used to reproduce figures.
- Fix and report random seeds, or explain seed sweep strategy.
- List data processing steps from raw logs to final plots.
- Avoid manual, non-documented data edits.

## Writing Quality Controls

- Use precise, short claims supported by a nearby figure/table reference.
- Avoid ambiguous terms like "significant" unless statistical meaning is provided.
- Keep one main claim per paragraph in Results.
- End each experiment subsection with a brief takeaway sentence.

## 中文补充（论文规范）

- 按“实验设置-方法-指标-结果-威胁与局限”结构写作，避免结论先行。
- 所有指标必须给出单位，延迟默认 `us`，并在全文统一。
- 结论必须可追溯到图表或统计结果，避免无数据支撑的表述。
- 对比基线要保证公平：相同资源预算、相同负载输入、相同测量窗口。
- 结果建议同时给绝对值和相对提升，优先附带置信区间或离散度信息。
- 至少包含一段局限性分析和一个失败/退化场景讨论。
- 复现信息要完整：版本、编译参数、运行命令、随机种子、数据处理流程。
