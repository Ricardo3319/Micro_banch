# Flowstep: 分对话执行总手册

## 1. 使用目标
- 本文件用于把整个项目拆成多个独立对话执行。
- 每次只执行一个 step，避免上下文窗口不足。
- 每个 step 的结果必须落盘到 `artifacts/step-xx-*`，供下一轮对话直接读取。

## 2. 对话执行总规则
- 只做当前 step，禁止越级扩展。
- 必须先读取以下文件再开始实现：
  - `微秒级主动迁移调度仿真实验计划.md`
  - `微秒级主动迁移调度仿真指导 (2).md`
  - 上一 step 的 `artifacts/step-xx-*/summary.md`
- 必须输出并落盘：
  - `summary.md`（本轮结论）
  - `run_log.md`（命令与验证记录）
  - `next_prompt.md`（下一轮可直接复制的提示词）
- 不允许改动已冻结协议（SLO、统计口径、baseline 公平性、负载协议）除非明确进入“计划修订轮”。

## 3. 代码高内聚框架（目标结构）
```text
Test/
  src/
    app/
      main.cpp
    core/
      simulator.cpp
      scheduler_runtime.cpp
    model/
      task.cpp
      event.cpp
      node.cpp
      core.cpp
    algorithms/
      host_power_of_k.cpp
      host_reactive_migration.cpp
      host_proactive_migration.cpp
    metrics/
      histogram.cpp
      stats.cpp
      logger.cpp
    workloads/
      poisson.cpp
      mmpp.cpp
      lognormal.cpp
  include/sim/
    core/
    model/
    algorithms/
    metrics/
    workloads/
    common/
  config/
    default.yaml
    tier1_w2.yaml
    tier2_w1.yaml
    tier3_w3.yaml
  scripts/
    run_step00.ps1
    run_step01.ps1
    run_step02.ps1
    run_step03.ps1
    run_step04.ps1
    run_step05.ps1
  tests/
    unit/
    integration/
  docs/
    ARCHITECTURE.md
    MODULE_SPEC.md
  artifacts/
    step-00-readiness/
    step-01-tier1/
    step-02-tier2/
    step-03-tier3/
    step-04-freeze/
    step-05-cloudlab/
```

## 4. 每步可执行 AI 提示词模板

### Step-00: readiness 门禁
```text
你现在执行 Step-00（仅门禁检查，不写新算法）。
目标：验证项目可进入 Tier-1。
必须先读取：`微秒级主动迁移调度仿真实验计划.md`、`微秒级主动迁移调度仿真指导 (2).md`。
执行内容：
1) 校验冻结参数是否一致（SLO、统计协议、负载协议、B2/M0规则）。
2) 校验目录结构是否满足高内聚框架目标，给出缺口清单。
3) 生成最小日志字段检查表。
4) 生成可重放执行清单（seed、命令、输出字段）。
输出并落盘到 `artifacts/step-00-readiness/`：
- `summary.md`
- `checklist.md`
- `run_log.md`
- `next_prompt.md`
验收：给出 PASS/FAIL 和阻塞项。
```

### Step-01: Tier-1 正向机制验证（W2）
```text
你现在执行 Step-01（仅 Tier-1，场景 W2）。
必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `artifacts/step-00-readiness/summary.md`
目标：在 W2 下比较 B1/B2/M0，验证正向机制信号。
执行内容：
1) 实现或修复 W2 工作负载生成（MMPP + Bimodal）及 B1/B2/M0 运行链路。
2) 运行代表负载点并导出 P99/P99.9、slo_violation_rate、migration_rate、invalid_migration_ratio。
3) 检查验收阈值：至少2个点 M0 对 B2 的 P99 改善>5%，且 invalid<=0.30、migration<=0.05。
输出并落盘到 `artifacts/step-01-tier1/`：
- `summary.md`
- `metrics_table.csv`
- `run_log.md`
- `next_prompt.md`
```

### Step-02: Tier-2 主结果（W1 全扫描）
```text
你现在执行 Step-02（仅 Tier-2，W1 全扫描）。
必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `artifacts/step-01-tier1/summary.md`
目标：形成主结果与公平性证据。
执行内容：
1) 跑 B0/B1/B2/M0 在 W1 的全扫描。
2) 输出 P99/P99.9 vs rho、migration_rate+invalid_ratio vs rho。
3) 输出 knee 点比较与默认点/同预算最优点双口径对照。
输出并落盘到 `artifacts/step-02-tier2/`：
- `summary.md`
- `metrics_scan.csv`
- `fairness_audit.md`
- `run_log.md`
- `next_prompt.md`
```

### Step-03: Tier-3 泛化与边界（W3 + 负例）
```text
你现在执行 Step-03（仅 Tier-3）。
必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `artifacts/step-02-tier2/summary.md`
目标：补齐重尾泛化与负例边界。
执行内容：
1) 跑 W3 代表负载点，比较 B1/B2/M0。
2) 追加低负载或极短任务占比高的边界场景。
3) 输出至少1个可信负例并给机制解释。
输出并落盘到 `artifacts/step-03-tier3/`：
- `summary.md`
- `boundary_case.md`
- `metrics_table.csv`
- `run_log.md`
- `next_prompt.md`
```

### Step-04: 统计封板与复现封板
```text
你现在执行 Step-04（仅封板，不新增算法）。
必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `artifacts/step-03-tier3/summary.md`
目标：固定统计结果与复现产物。
执行内容：
1) 统一按固定 seed 复跑关键图表。
2) 计算并写入 bootstrap CI。
3) 产出可一键重建的结果清单。
输出并落盘到 `artifacts/step-04-freeze/`：
- `summary.md`
- `final_results_manifest.json`
- `repro_commands.md`
- `run_log.md`
- `next_prompt.md`
```

### Step-05: CloudLab 趋势验证
```text
你现在执行 Step-05（仅 CloudLab 趋势验证）。
必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `artifacts/step-04-freeze/summary.md`
目标：验证仿真与实机趋势一致性。
执行内容：
1) 复现 Step-01 关键场景与 Step-02 knee 邻域点。
2) 按冻结判据核查：排序一致、knee 偏差<=0.05、边界偏差<=2点。
3) 输出偏差解释（内核抖动/IRQ/NIC竞争/时钟噪声）。
输出并落盘到 `artifacts/step-05-cloudlab/`：
- `summary.md`
- `sim_vs_cloudlab.csv`
- `consistency_check.md`
- `run_log.md`
- `next_prompt.md`
```

## 5. 下一步建议
- 从 Step-00 开始执行，先补齐目录与模块框架，再进入算法与实验。
- 每一轮新对话，把上一轮 `next_prompt.md` 原样复制给 AI。
