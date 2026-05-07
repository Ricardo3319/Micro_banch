# Step-00 Readiness Summary

## Scope
- Step ID: `step-00-readiness`
- Round date: 2026-03-11
- This round executes gate review only and checks whether the project can enter Step-01.

## Inputs Reviewed
- `微秒级主动迁移调度仿真实验计划.md`
- `微秒级主动迁移调度仿真指导 (2).md`
- `Flowstep/Flowstep.md`
- `Flowstep/CODE_STRUCTURE_GUIDE.md`

## Gate Result

| # | 检查项 | 判定 |
|---|--------|:----:|
| 1 | 冻结协议完整性（SLO/统计/负载/B2/M0） | PASS |
| 2 | 公平性硬约束定义完整性 | PASS |
| 3 | rho->lambda 与 W2 口径完整性 | PASS |
| 4 | 最小日志字段定义完整性 | PASS |
| 5 | 代码目录结构是否已落地（src/include/config/scripts/tests/docs） | PASS |
| 6 | 可编译入口与运行脚本是否存在 | PASS |
| 7 | 本地最小编译验证 | PASS |

## Decision
- **Step-00 status: PASS**

## Tail-Fix Actions
- 工具链复核：确认本机可用 `cmake + g++ + ninja`；`cl/msbuild` 不可用但不阻塞 Step-00。
- 脚本最小修复：更新 `scripts/run_step00.ps1`，优先检测 `cl`，否则自动回退到 `Ninja + g++`。
- 增加 clean build：每次 Step-00 先清理 `build/`，避免生成器缓存冲突（NMake/Ninja 切换失败）。
- 增加失败显式报错：configure/build/二进制缺失时直接 `throw`，避免误判通过。

## Key Deliverables

| 文件 | 说明 |
|------|------|
| `readiness_report.md` | 门禁报告（含阻塞项与修复动作） |
| `checklist.md` | Step-00 可勾选清单 |
| `config_used.md` | 冻结参数快照 |
| `run_log.md` | 本轮执行记录 |
| `structure_diff.md` | 目录与文件新增清单 |
| `next_prompt.md` | 下一轮可直接复制提示词 |

## Next Step
- 进入 Step-01（Tier-1，W2），按冻结口径实现/联通 B1/B2/M0 运行链路与指标导出。
