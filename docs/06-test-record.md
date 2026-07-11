# 06 Test Record

## 2026-07-12 RescueSched validity-v2 gate

Release configuration completed successfully. CTest passed 7/7: CLI help,
small deterministic RescueSched smoke, C++ simulator validity tests, two
independent v2 CSV generations, strict schema validation, and byte-for-byte
reproducibility. Generated test files live only under `build/`.

The validity test covers trace SHA-256, W3 conditional method distribution,
method-keyed estimator isolation, W1/W2/W3 offered-load calibration, paid
migration handoff and reservation accounting, measurement-cohort drain, and
unclipped percentiles above 10 ms.

更新时间：2026-07-06

本文档用于持续记录构建、仿真、schema、分析脚本和物理机实验测试。未执行的测试必须标记为【待测试】，不得补写结果。

## 测试环境模板

| 字段 | 内容 |
| --- | --- |
| 日期 | 【待填写】 |
| 执行人/代理 | 【待填写】 |
| commit | 【待填写】 |
| worktree status | 【待填写】 |
| OS | 【待填写】 |
| 编译器 | 【待填写】 |
| CMake | 【待填写】 |
| Python | 【待填写】 |
| CPU/内存 | 【待填写】 |
| 命令工作目录 | 【待填写】 |

## 当前已知本地门禁记录

以下记录来自 2026-07-06 的本地开发环境，用于说明当前门禁链路存在；后续正式实验应重新填写完整环境信息。

| 测试项 | 命令 | 状态 | 备注 |
| --- | --- | --- | --- |
| CMake configure | `cmake -S . -B build` | 通过 | 本地已执行。 |
| CMake build | `cmake --build build --config Release` | 通过 | 生成 `build/simulator.exe`。 |
| CTest gate | `ctest --test-dir build --output-on-failure` | 通过 | 3/3 tests passed。 |
| CLI/config smoke | `.\build\simulator.exe --config config/rescuesched.yaml --output build\rescuesched_cli_config.csv` | 通过 | 生成 5 行 RescueSched 单点 CSV。 |
| schema validation | `python tests/integration/validate_rescue_csv_schema.py build/rescuesched_cli_config.csv` | 通过 | current schema columns present。 |

## CTest 测试项

定义位置：`CMakeLists.txt`

| 测试名 | 目的 | 当前状态 |
| --- | --- | --- |
| `rescue_cli_help` | 验证 simulator CLI 可输出 usage。 | 已有 |
| `rescue_smoke_deterministic` | 验证 deterministic RescueSched smoke 通过。 | 已有 |
| `rescue_csv_schema` | 验证已有 RescueSched CSV 的最小 schema。 | 已有 |

## 仿真测试模板

| 编号 | mode | workload | rho | seed | output | 预期 | 实际 | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SIM-001 | `rescue-smoke` | W3 | 0.85 | 11 | stdout | `RescueSched smoke status: PASS` | 【待测试】 | 【待测试】 |
| SIM-002 | `rescue-main` | W3 | 0.85 | 11 | CSV | 5 methods x 1 seed x 1 rho | 【待测试】 | 【待测试】 |
| SIM-003 | `rescue-w2-burst` | W2 | 0.70/0.85/0.92 | default seeds | CSV | 无运行错误 | 【待测试】 | 【待测试】 |
| SIM-004 | `rescue-cost-microbench` | N/A | N/A | N/A | CSV | 输出 handoff cost | 【待测试】 | 【待测试】 |

## 分析脚本测试模板

| 编号 | 脚本 | 输入 | 输出 | 状态 |
| --- | --- | --- | --- | --- |
| ANA-001 | `scripts/rescue_analysis.py` | step-15/17 RescueSched CSV | step-16/17 summary + figures | 【待测试】 |
| ANA-002 | `scripts/infocom_readiness_analysis.py` | step-18 CSV + overload sanity | step-18 summary tables | 【待测试】 |
| ANA-003 | `scripts/generate_charts.py` | legacy step-01/02/03/04 CSV | `docs/figures` | 【待测试】 |

## 物理机测试模板

| 编号 | 测试项 | 机器 | workload | baseline/method | 指标 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| PHY-001 | CPU affinity smoke | 【待确认】 | W3 | runtime baseline | worker 绑定正确 | 【待测试】 |
| PHY-002 | migration microbench | 【待确认】 | N/A | handoff | per-op us | 【待测试】 |
| PHY-003 | single-node RescueSched demo | 【待确认】 | W3 | L1/M0/M1 | P99/SLO/migration | 【待测试】 |
| PHY-004 | W2 burst replay | 【待确认】 | W2 | L1/M0/M1 | P99/P999/SLO | 【待测试】 |
| PHY-005 | trace replay | 【待确认】 | real trace | L1/M0/M1 | aligned metrics | 【待测试】 |

## 失败记录模板

| 日期 | 测试编号 | 命令 | 失败现象 | 日志路径 | 初步原因 | 修复状态 |
| --- | --- | --- | --- | --- | --- | --- |
| 【待填写】 | 【待填写】 | 【待填写】 | 【待填写】 | 【待填写】 | 【待确认】 | 【待处理】 |
