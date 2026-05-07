你现在执行 Step-05（仅 CloudLab 趋势验证）。

必须先读取：
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `artifacts/step-04-freeze/summary.md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## 项目背景

我正在用 C++17 构建一个离散事件仿真器（DES），对比四种微秒级调度算法。工作空间在 `Test`，构建工具链为 cmake 4.2.3 + g++ 15.2.0 (MSYS2 ucrt64) + ninja 1.13.2，Windows 11。

## Step-04 关键结论

### 统计封板结果
- 全量数据: 525 runs (Step-01: 60, Step-02: 360, Step-03: 105)
- Bootstrap CI (B=10000, seed=42):
  - W3 rho=0.85: **M0 P99 CI [178,182] vs B2 [198,200] — 不重叠** (最强证据)
  - W3 rho=0.85: **M0 P999 CI [342,352] vs B2 [390,394] — 不重叠**
  - W2 rho=0.85: M0 P99 CI [622,2710] vs B2 [202,4430] — 重叠 (MMPP 高方差)
  - W3 rho=0.92: M0 P99 CI [262,332] vs B2 [286,344] — 重叠 (高负载方差)

### 跨工作负载对比 (M0 vs B2 P99 改善率)
| rho | W1 | W2 | W3 |
|-----|-----|-----|-----|
| 0.50 | 0% | +65.9% | 0% |
| 0.85 | 0% | +40.1% | +10.0% |
| 0.92 | — | — | +6.2% |

### 负例封板
- W3 rho=0.95: M0 P999 退化 -23.5% (5/5 seeds, 全局饱和)
- W2 rho=0.70/0.92: M0 P99 退化 (MMPP 极端突发)
- W1 rho=0.95: M0 P999 退化 -58% (饱和+迁移开销)

## 冻结参数（不可修改）

- 集群: 64 hosts × 16 cores = 1024 cores, 均质 C=1.0
- T_host=2.1us, T_net=3.15us, SYNC_LOAD=10us
- M0: α=0.8, margin=1.5us, K_DST=4, T_CHECK=1.0us, budget=5% (effective 4.5%)
- B2: K_DST=2, cooldown=2us, budget=5%
- 统计: warmup=200k, measurement=1M, seeds={11,23,37,47,59}

## 本步目标

验证仿真与 CloudLab 实机趋势一致性。

## 执行内容

1. **复现场景选择**: 优先复现 Step-01 关键场景（W2 rho=0.50/0.85）+ Step-02 knee 邻域点（W1 rho=0.85/0.90）
2. **实机配置**: CloudLab c6525 (AMD EPYC 128核, Mellanox 100GbE), 2-4 台
3. **趋势一致性判据** (已冻结):
   - 排序一致: 主方法集合在关键负载点的 P99 排序不反转
   - 拐点一致: 实机与仿真的 knee 点 rho 偏差不超过 0.05
   - 边界一致: 主要退化边界位置偏差不超过 2 个扫描点
4. **偏差解释**: 内核抖动、IRQ、NIC 竞争、时钟噪声等实机因素

## 关键文件

- 主仿真引擎: `src/core/simulator.cpp` + `include/sim/core/simulator.h`
- 入口: `src/app/main.cpp`
- 冻结结果清单: `artifacts/step-04-freeze/final_results_manifest.json`
- 复现命令: `artifacts/step-04-freeze/repro_commands.md`
- 构建: `cmake --build "d:\desktop\Test\build"`

## 验收标准

- 趋势一致（排序/拐点/退化边界），不要求绝对值一致
- 排序一致: P99 排序不反转
- 拐点: knee rho 偏差 ≤ 0.05
- 边界: 退化边界偏差 ≤ 2 扫描点
- 偏差解释完整

## 输出并落盘到 `artifacts/step-05-cloudlab/`

- `summary.md`
- `sim_vs_cloudlab.csv`
- `consistency_check.md`
- `run_log.md`
- `next_prompt.md`
