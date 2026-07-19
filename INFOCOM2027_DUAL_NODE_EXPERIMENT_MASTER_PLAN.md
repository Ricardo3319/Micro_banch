# RescueSched INFOCOM 2027 双物理机实验细化实施总计划

> **文档状态：WP2 CODE-LEVEL COMPLETE / WP3 PROFILE+RESTORE COMPLETE / WP4 BLOCKED / 以 `PROJECT_STATE.md` 为实时状态源**
> **计划版本：v1.5**
> **编制日期：2026-07-17（UTC）**
> **物理机窗口：`T_expire=2026-07-24T11:00:00Z`；`T_no_new_block=2026-07-23T23:00:00Z`；旧 Portal `Jul 24, 2026 7:00 PM` 无时区，仅保留为历史补充事实**
> **计划主时区：UTC；涉及投稿截止时另列 PDT**

本文档将既有计划细化为可审计的工作包、逐日窗口、依赖关系、门禁、交付物、失败分类和退出条件。实时结果、阻断项和证据位置以 `PROJECT_STATE.md` 与 `EVIDENCE_INDEX.md` 为准。WP3 host profile apply/effective verification/restore regression 已执行并完成恢复；本文档不把它表述为 workload、双机 RPC、pilot、calibration 或 formal run。

---

## 0. 本次确认、计划边界与当前仓库事实

### 0.1 已确认事实

- 2026-07-19 在 node0/node1 分别实时执行 `geni-get -n manifest`；两机仍显示 `expires="2026-07-18T03:00:00Z"`，完整原始 XML SHA256 均为 `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8`。旧值和 manifest 子检查 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE` 均保留，不被新结论覆盖。
- 同轮两机实时 `geni-get -n status` 均给出两个 sliver 的原始 `geni_expires=2026-07-24 05:00:00`，status SHA256 均为 `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12`。实时 Utah CloudLab 官方站点响应给出 `-0600`；保存的 deployed source `0b1fdb15cd434591f3fab98799d78189698686fc` 和站点配置证明字段按 `America/Denver` 解释。因此 2026-07-19T07:28:13Z 冻结 `T_expire=2026-07-24T11:00:00Z`、`T_no_new_block=2026-07-23T23:00:00Z`，剩余 `5d 03h 31m 47s`，状态为 `PASS_EXACT_UTC_FROZEN_FOR_WP3`。证据：`physical-results/wp0-wp3-20260719T070542Z/wp0.1/`。
- 旧 Portal 曾显示 `Jul 24, 2026 7:00 PM`，截图无时区，因此只保留为历史补充事实，不作为精确 UTC authority。2026-07-18 的 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2` 也仍仅是历史 WP2 范围决定，不扩写为 WP3 PASS。
- WP0.2 当前为 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS`：两机 `~/.config/rescuesched/rclone.conf` 均不存在，expected remote、private bucket、project-dedicated prefix 未提供；未读取 credential，upload/remote SHA/download/download SHA/delete/absence confirmation 全部 `NOT_RUN`。它不阻塞 WP3，但继续硬阻塞 WP4/pilot/formal。
- WP1 冻结 annotated tag `physical-integration-v1` 保持 object `2fb914080bc63d357e38b8f4e21a6d5add34dc8e`、target `0a88f03a21802be0eadc3065b93cb97876a6bd2f`，不得移动、删除、重建或覆盖。GitHub publication 仍为 `WAIVED_BY_OWNER`，本轮没有 push、force push 或 remote URL 修改。
- WP2 源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 的 `PASS_CODE_LEVEL_ON_BOTH_NODES` 保持有效：两机各自 Release 26/26、ASan/UBSan 26/26、TSan 2/2、本机 loopback/mapping/fail-closed gate 和 synthetic 12/12。它不是双机 RPC、pilot 或 formal。
- WP3 工具/profile 提交 `b8eec4768d9dc556f43af925a9520e05c503b7a5`、`90c5ff6d774cea0e06563e92681570727b079933` 已部署到两机。node0/node1 topology validator、profile freeze、before capture、dry-run、atomic apply、89/89 effective verification、management SSH、独立 restore、第二次零修改 restore 和 restored diff 全部 PASS；两机均已恢复到 before 状态。证据根：`physical-results/wp0-wp3-20260719T070542Z/wp3/`。
- WP4、双机 RPC、pilot、scheduler period calibration、arrival calibration、formal paired block、正式端口 `9000`、ring `4096` 和正式结果目录均未执行且未获授权。
- 动态时间边界：
  - `T_expire = 2026-07-24T11:00:00Z`；
  - `T_no_new_block = 2026-07-23T23:00:00Z`；
  - `T_restore = T_expire - 6h = 2026-07-24T05:00:00Z`；
  - WP4/pilot/formal 在 S3 完整闭环 PASS 且获得新授权前不得启动。

### 0.2 当前仓库基线（仅用于规划）

截至编制本计划时，本地仓库状态为：

```text
repository: /users/Mingyang/Micro_banch
current branch: codex/infocom2027-integration
frozen integration commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
release tag: physical-integration-v1 (annotated, target 0a88f03a21802be0eadc3065b93cb97876a6bd2f)
integration base: codex/rescuesched-baselines @ 5379f1a
current WP2 source commit: 7db91095c4d0f84a5eb568b980748051f994c4cc
WP3 tooling/profile commits: b8eec4768d9dc556f43af925a9520e05c503b7a5, 90c5ff6d774cea0e06563e92681570727b079933
current documentation/config commit: SELF (to be replaced by the final commit identity)
remote branch/tag publication: WAIVED_BY_OWNER; no push in this WP3 task
```

当前物理分支已包含双节点 UDP RPC、固定 worker、trace、两 client coordinator、验证和基础采集脚本，但它是聚焦物理实现的裁剪分支。后续集成不能整体合并导致以下内容丢失：

- 完整 simulator；
- `paper/`；
- `artifacts/step-21-corrected-full/`；
- 修正仿真分析和绘图脚本；
- 论文合同与历史证据。

### 0.3 已解决的文档/配置冲突

原 `docs/CLOUDLAB_RUNBOOK.md` 中的 `0,2,4,...,30` worker CPU 示例会在当前 topology 上重复使用八个 physical cores。WP1 已修正文档；WP3 已根据实时 topology 冻结并验证 profile。两机均为 16 physical cores / 32 logical CPUs，SMT sibling 为 `CPU n ↔ CPU 16+n`。node0 以 0–15 作为 16 个唯一 worker physical cores，并通过合同字段显式允许辅助/IRQ logical CPUs 使用对应 SMT siblings；validator 已证明 auxiliary physical-core 集合与 IRQ physical-core 集合互不重叠。

### 0.4 当前执行边界

WP0.1 exact UTC gate 和 WP3 host profile apply/restore regression 已完成。当前只允许保存/复核 WP0.1、WP0.2、WP3 证据，提交并同步控制文档，核验两机 final identity/tag，以及接收补齐 WP4 前 S3 gate 所需的非敏感输入。主机已恢复，`host_tuning_allowed=false`。

仍明确禁止：

- WP4、双机 RPC smoke、pilot、scheduler period calibration、arrival calibration 或 formal paired block；
- 使用正式实验端口 `9000`、正式结果目录或 ring `4096` pilot 条件；
- 生成或解释正式性能结果；
- 在 remote streaming SHA 验证前清理任何 raw evidence；
- 把 stale manifest 子检查改写成 PASS，或把旧 Portal 无时区显示当作精确 UTC authority；
- 把 S3 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS` 写成 PASS，或把 GitHub `WAIVED_BY_OWNER` 写成已 push；
- 把 WP2 本机 gate 或 WP3 host transaction 写成双机 RPC、pilot 或正式物理结果。

---

## 1. 研究目标与冻结主张

继续现有 RescueSched 主线，验证以下主张：

1. 稳定 flow/RSS 初始放置会产生瞬时 per-core FIFO 不平衡。
2. RescueSched 只迁移同时满足以下条件的 queued descriptor：
   - 本地预计无法在 deadline 前完成；
   - 目标核预计可以按时完成；
   - descriptor 尚未开始执行。
3. 物理实验主指标固定为：
   - **server-side deadline violation rate**；
   - **SLO goodput**。
4. 次要指标包括 P50/P99/P999 server completion、client RTT、吞吐、迁移、handoff 和 scheduler 开销。
5. W3 低负载边界与 W2 burst/tail 边界无论结果方向如何都必须报告。
6. 不宣称 RescueSched 普遍改善尾延迟；若物理结果与仿真方向相反，必须作为结果或限制报告。

### 1.1 固定方法

```text
L0_RandomCore
L1_WorkStealingPolling
M0_AltoThreshold
M1_RescueSched
```

四种方法必须共享：

- UDP protocol；
- ingress socket/shard 配置；
- worker 数和 CPU 列表；
- queue、estimator 和 handoff primitive；
- trace bytes；
- source/destination port 规则；
- 日志 schema；
- host profile；
- 同一 anchor 的 arrival scale。

### 1.2 正式实验矩阵

| Anchor | 角色 | Seeds | 每 seed 方法数 | 有效 method-runs |
| --- | --- | ---: | ---: | ---: |
| W3, ρ=0.85 | 主确认 | 10 | 4 | 40 |
| W3, ρ=0.90 | 主确认 | 10 | 4 | 40 |
| W3, ρ=0.70 | 负面边界 | 10 | 4 | 40 |
| W2, ρ=0.85 | burst/tail 边界 | 10 | 4 | 40 |
| **合计** |  | **40 paired blocks** |  | **160** |

固定 seeds：

```text
11, 23, 37, 47, 59, 71, 83, 97, 109, 127
```

固定 cohort：

```text
warmup:     200,000 requests
measurement: 1,000,000 requests
```

约束：

- 一个 `anchor × seed` 的四方法构成不可拆分的 paired block。
- block 内方法顺序使用现有 SHA-256 balanced schedule。
- 在看到正式结果后，不得更换 seed、增加 seed、删减不利 block、调整参数或提前停止。
- 基础设施失败后必须从该 block 的第一个方法重新运行，禁止跨 attempt 拼接。

---

## 2. 两台主机职责与控制面

### 2.1 node0 / `10.10.1.1`

负责：

- RPC server；
- ingress receivers；
- 固定核 workers；
- scheduler；
- descriptor handoff；
- response queues 与 response senders；
- server-side request、migration、decision、scheduler 和 host counters。

禁止：

- 正式 method 运行期间压缩或上传；
- 正式 method 运行期间执行 Git 操作；
- 把正式 worker 放到同一 physical core 的 SMT siblings 上；
- 在不同方法间复用 queue、estimator 或 scheduler 状态。

### 2.2 node1 / `10.10.1.2`

负责：

- 两个并发 open-loop client；
- coordinator 和持久状态机；
- client-side send/receive 与 RTT；
- formal validator；
- block 完成后的压缩、SHA256、S3 上传和远端复核；
- 运行前后 host/NIC 状态汇总。

node1 缺少可用 `cpufreq` policy 时：

- 不将其直接判为失败；
- 必须在 manifest 中如实记录；
- 运行前、中、后采样实际频率；
- 以 send failure、send-lag、实际 offered load 和 request completeness 判定有效性。

### 2.3 控制原则

- SSH/control traffic 使用管理网络；RPC 使用实验网络。
- coordinator 必须核对 node0/node1 commit、dirty status、config SHA 和 host-profile hash。
- 任一不一致时拒绝启动正式 method。
- 所有正式操作从 node1 coordinator 发起，node0 不进行人工并行操作。

---

## 3. 项目控制文件与单一事实来源

执行阶段只长期维护以下四份控制文档；本计划是总纲，不替代它们：

1. `PROJECT_STATE.md`
   - 当前阶段、UTC 更新时间、branch/commit；
   - 两机精确 lease 到期时刻；
   - host-profile hash、部署 commit、当前阻断项；
   - 最近完成项和下一步最多三个任务；
   - scheduler period、worker 数、arrival scales；
   - 最新有效 block、失败 attempt 和 S3 状态。

2. `EXPERIMENT_CONTRACT.md`
   - `contract_version`；
   - 正式矩阵、methods、seeds、cohort 和统计规则；
   - worker/CPU/IRQ/NIC profile；
   - scheduler/poll period；
   - 12-worker fallback 条件；
   - calibration、有效性与失败定义；
   - formal config SHA256。

3. `RUNBOOK.md`
   - 安装、构建、部署、host apply/verify/restore；
   - smoke、pilot、calibration、formal、resume、validate、archive；
   - 每条命令的执行主机、输入、预期输出、成功条件和恢复入口；
   - 只引用仓库外 secret，不出现凭据。

4. `EVIDENCE_INDEX.md`
   - `claim → figure/table → analysis output → block/attempt → trace SHA → commit → S3 object`；
   - 有效结果和失败 attempts 都入索引；
   - 论文中每个数字可追溯到 CSV 行、归档 SHA 和代码 commit。

更新纪律：

- 每完成一个工作包，至少更新 `PROJECT_STATE.md`。
- 合同值改变时增加 `contract_version`，同步修改 YAML 和合同。
- 操作方法改变时同步 `RUNBOOK.md`。
- 产生证据时同步 `EVIDENCE_INDEX.md`。
- 代码、测试、配置和对应文档放在同一可审计 commit。
- 正式结果可见后若修改合同，必须重跑所有受影响 paired blocks。

---

## 4. 总体里程碑与逐日窗口

### 4.1 日历总览

| 日期（UTC） | 主目标 | 当日硬交付 | 禁止跨越的门禁 |
| --- | --- | --- | --- |
| 2026-07-17 | 计划、集成设计、控制文档设计 | integration allowlist、四文档骨架设计、风险登记 | 不启动物理实验 |
| 2026-07-18 | 集成基线与 runtime 正确性修复 | 可构建 integration、关键单元测试、拓扑 validator | 集成/测试不通过不得部署 |
| 2026-07-19 | 完成精确 lease UTC freeze、两机可恢复 host profile | 两机 profile SHA、apply/effective/restore/幂等证据；不运行 workload | restore 或 management SSH 不可靠立即停止 |
| 2026-07-20 | 仅在 S3 PASS 且另获授权后才可考虑 WP4 | 当前任务不产生 WP4 交付 | S3/授权任一缺失不得启动 RPC/pilot |
| 2026-07-21 | 正式矩阵执行主日 1 | 连续有效 paired blocks、逐 block 远端 SHA | 任何 block 未归档不得清理 raw |
| 2026-07-22 | 正式矩阵执行主日 2 | 累计进度与容量复核 | 不得因结果不利改变计划 |
| 2026-07-23 | 正式矩阵执行主日 3、完成目标日 | 40 个有效 blocks 的目标完成、初步 evidence index | 不完整矩阵不得宣称完整确认 |
| 2026-07-24 | `T_restore=05:00Z` 后只允许恢复/备份复核；`T_expire=11:00Z` | host restore report、证据冻结状态 | 到 `T_restore` 必须停止实验 |
| 2026-07-25 | 主机租期已结束后的离线证据整理 | 本地/远端索引复核 | 不依赖 CloudLab 节点 |
| 2026-07-26—29 | 离线统计、绘图、论文同步 | 可复建分析、claim-evidence 完整 | 不依赖 CloudLab 主机 |
| 2026-07-30 12:00 PDT | 内部上传截止 | 上传 PDF 并下载复核 | 不把 7 月 31 日当正常工作日 |
| 2026-07-31 | INFOCOM full-paper 官方截止窗口 | 仅紧急修复 | 不安排常规实验或大改 |

### 4.2 关键内部截止

- **2026-07-19 23:59 UTC**：16-worker runtime 和 host profile 必须具备 pilot 资格。
- **2026-07-20 12:00 UTC**：12-worker fallback 最晚决策点；晚于此时触发只能降级论文范围，不能混入 16-worker 正式数据。
- **2026-07-20 23:59 UTC**：formal contract、period、arrival scale、handoff 和 flow map 冻结目标。
- **2026-07-23 18:00 UTC**：40-block 完成目标；剩余时间留给失败 attempt、远端复核和证据整理。
- **2026-07-23 23:00 UTC / `T_no_new_block`**：不得启动新的完整 block。
- **2026-07-24 05:00 UTC / `T_restore`**：无条件进入恢复、备份复核和停止实验流程。
- **2026-07-24 11:00 UTC / `T_expire`**：冻结的租期到期时刻。

---

## 5. 工作分解结构（WBS）

## WP0：租期证据与异地备份门禁

**计划窗口：** 2026-07-17 至 19
**状态：** **WP0.1 PASS_EXACT_UTC_FROZEN_FOR_WP3 / WP0.2 DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS**
**依赖：** 无

### WP0.1 租期证据登记

2026-07-19 两机分别实时采集完整 manifest 与 live status，记录开始/结束 UTC、hostname、exit code、原始字段和 SHA256。结果：

```text
node0 manifest expiration = 2026-07-18T03:00:00Z
node1 manifest expiration = 2026-07-18T03:00:00Z
manifest SHA256 (both) = ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
manifest subcheck = BLOCKED_STALE_OR_NON_PROPAGATED_VALUE
live status raw (both) = 2026-07-24 05:00:00
live status SHA256 (both) = 1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12
site timezone = America/Denver / -0600
T_expire = 2026-07-24T11:00:00Z
T_no_new_block = 2026-07-23T23:00:00Z
status = PASS_EXACT_UTC_FROZEN_FOR_WP3
```

时区解释依据为实时 Utah CloudLab 官方站点响应、保存的 exact deployed source commit `0b1fdb15cd434591f3fab98799d78189698686fc` 和站点配置。旧 manifest、旧 Portal 无时区显示、旧 WP2 owner attestation 与所有 AM 直连失败 attempts 均保留。证据：`physical-results/wp0-wp3-20260719T070542Z/wp0.1/`。

该 PASS 只解除 WP3 host profile apply/restore，不授权 WP4/pilot/formal。

### WP0.2 S3 安全与可恢复性

计划配置保持：private bucket、Block Public Access、SSE-S3、项目专用最小权限 prefix、仓库外 `0600` 凭据文件。完整 gate 仍必须执行：

```text
local random test file
→ local SHA256
→ upload unique object
→ remote streaming SHA256
→ download to a different path
→ downloaded SHA256
→ verify all three hashes equal
→ delete only this object
→ confirm this object is absent
```

2026-07-19 非敏感检查结果：两机 `~/.config/rescuesched/rclone.conf` 不存在，expected remote、private bucket、project-dedicated prefix 缺失；未读取或输出 credential，所有对象操作均 `NOT_RUN`。状态为 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS`，证据：`physical-results/wp0-wp3-20260719T070542Z/wp0.2/`。

**门禁解释：** S3 不阻塞 WP3 apply/restore regression；它是 WP4/pilot/formal 的独立硬门禁。任一输入缺失、SHA 不一致、删除范围不安全或 secret 泄露时不得声称 PASS。

---

## WP1：集成分支与文档基线

**状态：** **PASS_LOCAL / REMOTE_PUBLICATION_WAIVED_BY_OWNER**；冻结基线 `0a88f03a21802be0eadc3065b93cb97876a6bd2f` 的两机构建测试保持通过，2026-07-18T15:17:01Z 两机 clean/same commit `50362f346c7fcdbbc064760e5670841eb5ac5db4` 且 tag 未移动；远端 branch/tag 实际均 `ABSENT`，没有执行 push，负责人同意远端发布不阻塞 WP2
**计划窗口：** 2026-07-17 至 18
**依赖：** WP0.1 的 Portal+负责人 WP2-only 确认；S3 不阻塞 WP2 代码工作，但进入 pilot/formal 前须 PASS

### WP1.1 分支策略

目标：

```text
base:   codex/rescuesched-baselines @ 5379f1a
branch: codex/infocom2027-integration
```

集成原则：

- 从 baseline 创建 integration branch；
- 按 allowlist 导入物理 runtime/RPC/trace/tests/scripts；
- 手工合并 `CMakeLists.txt`、`.gitignore`、`README.md` 和 docs；
- 不整体 cherry-pick 会删除 simulator/paper/artifacts 的裁剪提交；
- 对每个删除项做反向检查，确保历史研究资产仍存在。

### WP1.2 物理实现 allowlist

优先检查并按实际依赖导入：

```text
include/physical/
src/physical/
src/app/physical_runtime_main.cpp
src/app/handoff_microbench_main.cpp
src/app/rpc_server_main.cpp
src/app/rpc_client_main.cpp
src/app/trace_generator_main.cpp
tests/unit/physical_runtime_tests.cpp
tests/unit/rpc_protocol_tests.cpp
scripts/capture_physical_host_state.sh
scripts/generate_cloudlab_run_order.py
scripts/run_background_job.sh
scripts/run_local_physical_runtime_smoke.sh
scripts/run_local_rpc_smoke.sh
scripts/run_physical_preflight.sh
scripts/run_pinned_handoff_microbench.sh
scripts/run_sanitizers.sh
scripts/run_two_node_rpc_smoke.sh
scripts/validate_rpc_two_node_run.py
```

公共 trace/simulator 文件只做最小合并，避免物理分支中的裁剪版本覆盖完整 simulator。

### WP1.3 基线文档和配置

创建四个长期文档和：

```text
config/infocom2027-physical.yaml
```

YAML 至少包含：

- contract version；
- methods、anchors、seeds、cohort；
- worker/control CPU IDs；
- NIC/IRQ profile；
- flow sockets、ports；
- scheduler/poll period；
- handoff estimate；
- arrival scales；
- decision logging mode/cap/bucket；
- output schema version；
- failure thresholds。

### WP1.4 集成验收

- simulator、paper 和修正 artifacts 未丢失；
- Release 构建和全部测试在两机分别通过；
- 两机 clean 且 commit 完全一致；
- coordinator 对 commit mismatch 和 dirty tree 能 fail closed；
- 基线 tag `physical-integration-v1` 已作为 annotated tag 固定指向 `0a88f03a21802be0eadc3065b93cb97876a6bd2f`，不得移动或重建；
- 远端发布另行记录：2026-07-18T15:03:28Z 远端 branch/tag 均 `ABSENT`，两机认证不可用且没有 push；负责人于 2026-07-18T15:17:01Z 将其设为 `WAIVED_BY_OWNER`，不阻塞 WP2，但不得冒充已发布。证据见 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp1/` 和 `physical-results/gate-policy-revision-20260718T151701Z/`。

**门禁：** 本地 integration/build/test 条件已满足，可进入 WP2；仍不得进入 host tuning。

---

## WP2：物理 runtime 正确性修复

**状态：** **PASS_CODE_LEVEL_ON_BOTH_NODES**（源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc`；不自动授权 WP3）
**计划窗口：** 2026-07-18 至 19
**依赖：** WP1 可构建 integration；2026-07-18T15:17:01Z 的负责人调整只允许 WP2 runtime 代码工作和代码级测试，不包含 host tuning、两机 RPC smoke、pilot、calibration 或 formal

修复按以下顺序进行；后项不得掩盖前项失败。

### WP2.1 receiver idle 竞争

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** predicate wait、独立时间读取、饱和减法和 counters 已实现；L0/L1/M0/M1 各 100 次生命周期并发回归纳入 CTest。

- `condition_variable::wait_for` 使用 predicate；
- `now_ns` 与 `last_receive_ns` 分开读取；
- 时间差使用饱和减法；
- 不允许无符号下溢触发错误 idle termination；
- L0/L1/M0/M1 各至少 100 次并发回归。

**输出：** race regression test、termination counters、失败重现说明。
**验收：** 无 premature shutdown、hang 或请求丢失。

### WP2.2 CPU-time synthetic service

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** `CLOCK_THREAD_CPUTIME_ID`、完成后 actual CPU-time EWMA 更新和 5/24/100 µs × contention matrix 已纳入测试。

- payload 使用 `CLOCK_THREAD_CPUTIME_ID` 消耗目标 CPU time；
- wall start/finish 继续计算 server completion latency；
- 完成后测量 actual thread CPU service；
- EWMA 只使用完成后测得值；
- policy 不可读取当前请求隐藏 synthetic service 字段。

**测试矩阵：** target 5/24/100 µs × 无抢占/强制抢占。
**误差门限：** `abs(error) <= max(2 µs, 5%)`；wall time 不得小于 CPU time。

### WP2.3 topology 与 affinity validator

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** validator 对 duplicate/offline/SMT sibling/role overlap fail closed；formal topology/profile SHA 仍留给 WP3。

- 自动读取 `lscpu`/sysfs；
- 每个 worker 映射到唯一 `(socket, core)`；
- 拒绝重复 logical CPU、offline CPU、SMT sibling 重复 physical core；
- 控制线程和 IRQ CPU 不得与 worker 列表冲突，除非合同明确允许 sibling 隔离布局；
- affinity 失败分类为 `INFRASTRUCTURE_FAILURE`；
- 修正文档中的不安全 CPU 示例。

### WP2.4 L1 distributed polling baseline

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** idle worker absolute timed polling、attempt/success/moved-work/cost counters 已实现。

- 由 eligible idle worker 自己发起 steal；
- 每个 poll period 每个 idle worker最多一次 attempt；
- 不使用中央 scheduler 模拟 distributed polling；
- 成功 steal 经过与 M0/M1 相同 handoff primitive；
- 记录 attempts、successes、moved work、poll frequency 和 poll cost。

### WP2.5 ingress 与 response 解耦

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** 两个固定核 epoll receivers、两个固定核 response senders 和 bounded queue 已通过本机 executable gate；这不是双机 RPC。

- 保留一个 `SO_REUSEPORT` socket 对应一个 ingress shard；
- 16 个未固定 receiver 改为两个固定核 epoll receivers；
- 每个 receiver 管理固定 socket 子集；
- worker completion 只 enqueue response；
- 两个固定核 response senders 独立 `sendto()`；
- response queue 有界；overflow 不得静默丢弃。

**失败条件：** enqueue failure、overflow、send failure 或 response count 不完整。

### WP2.6 scheduler absolute epoch

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** absolute deadline、missed-epoch skip 和 lag counters 已实现并测试。

- 使用 absolute deadline；
- 错过 epoch 时跳过过期 epoch，不连续追赶；
- 记录 `scheduled/executed/missed epochs` 和 `max_epoch_lag_us`；
- 候选选择缩短全局锁持有；
- 日志采样/聚合不在 coordination mutex 内执行；
- 压力测试证明 ingress 持续前进。

### WP2.7 production handoff

**状态：PASS_CODE_PATH_ON_BOTH_NODES。** production remove/reserve/transfer/append-tail 与完整 handoff duration logging 已实现；formal `handoff_estimate_us` 仍未由后续 microbenchmark 冻结。

- 删除 `std::this_thread::yield()` 等模拟成本；
- 使用真实 `remove/reserve/transfer/append-tail` 路径；
- 记录完整 handoff wall distribution；
- 非最终 microbenchmark 后冻结 median 为 `handoff_estimate_us`；
- P95/P99 只用于 sensitivity 和限制报告。

### WP2.8 有界日志

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** bounded sample cap `100000`、aggregate bucket `1000 µs` 和 `decision_aggregates.csv` 已固定；formal config SHA 仍未冻结。

正式模式固定：

```text
decision_logging: bounded
deterministic_sample_cap: 100000
aggregate_bucket: 1 ms
```

必须完整保存：requests、client responses、migrations。
禁止：正式运行生成无界 `decisions.csv`。

### WP2.9 ingress mapping freeze

**状态：PASS_CODE_MECHANISM_ON_BOTH_NODES。** `ingress_mapping.csv` 与同节点四方法 mapping projection 比较已 PASS；formal trace/flow map 的最终 freeze 仍待后续阶段。

- 固定 destination port、client source-port base 和 flow-socket 数；
- formal trace 冻结前保存 `flow/source-port → ingress shard`；
- 四方法 mapping 必须完全一致；
- 论文表述为 kernel `SO_REUSEPORT` ingress shard，除非另有硬件证据，不称为 RX queue。

### WP2 总门禁

**状态：PASS_CODE_LEVEL_ON_BOTH_NODES。** 源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 的结果：

| 项目 | node0 | node1 |
| --- | --- | --- |
| Release CTest | PASS 26/26 | PASS 26/26 |
| ASan/UBSan | PASS 26/26 | PASS 26/26 |
| TSan 核心并发 | PASS 2/2 | PASS 2/2 |
| receiver lifecycle / topology negative / CPU-time / epoch unit-concurrency tests | PASS | PASS |
| 本机 loopback 四策略 UDP mapping gate | PASS | PASS |
| response queue failure injection | PASS_EXPECTED_FAIL_CLOSED | PASS_EXPECTED_FAIL_CLOSED |
| 3 repetitions × 4-policy synthetic stress | PASS 12/12 | PASS 12/12 |

证据：

```text
physical-results/wp2-runtime-20260718T153855Z/
physical-results/wp2-final-node0-20260718T163835Z/
physical-results/wp2-final-node1-20260718T164147Z/
physical-results/wp2-node1-sync-20260718T164112Z/
physical-results/wp2-node1-evidence-transfer-20260718T164352Z/
```

限定：两机只是分别运行相同的代码级 gate；没有运行 node0↔node1 RPC。正式端口 `9000` 未使用，没有 host tuning。TSan 的早期 `pthread_cond_clockwait` 假阳性、一次异常挂起、中断快照、20/20 生命周期压力和最终重试/PASS 证据全部保留。

该 WP2 PASS **不等于“可以立即进入两机 pilot”**。此后 WP0.1 exact UTC 与 WP3 host profile regression 已分别完成，但 S3 仍为 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS`，且 WP4 未获授权；因此仍不得启动双机 RPC、pilot 或 formal。

---

## WP3：主机 profile、部署与恢复

**状态：** **PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION_ON_BOTH_NODES**
**执行窗口：** 2026-07-19T08:02:42Z 至 2026-07-19T08:08:15Z
**依赖：** WP2 PASS；WP0.1 `PASS_EXACT_UTC_FROZEN_FOR_WP3`
**证据根：** `physical-results/wp0-wp3-20260719T070542Z/wp3/`

### WP3.0 topology 与 NIC 发现

两机 before snapshot 确认：1 socket、1 NUMA node、16 physical cores、32 logical CPUs、online `0-31`，SMT sibling 为 `CPU n ↔ CPU 16+n`。experiment NIC 为 `enp65s0f0np0`，control NIC 为 `eno33np0`；两机 experiment NIC 均为 `mlx5_core`、firmware `16.28.4512 (DEL0000000015)`、PCI `0000:41:00.0`、NUMA `-1`。apply 只修改 experiment NIC；management SSH route 始终使用 control NIC。

### WP3.1 node0 16-worker 主配置

冻结 role map：

```text
workers:          CPU 0-15
receivers:        CPU 16-17
response senders: CPU 18-19
scheduler:        CPU 20
server main:      CPU 21
housekeeping:     CPU 22-23
experiment IRQs: CPU 24-31
async IRQs:       CPU 22-23
XPS:              CPU 18-21
```

`workers=0-15` 是 16 个唯一 physical cores。由于机器只有 16 physical cores，辅助/IRQ logical CPUs 必然是 worker 的 SMT siblings；profile 以 `ALLOW_WORKER_AUX_SMT_SIBLINGS=true` 显式合同化该布局。validator 仍拒绝 duplicate/offline logical CPU、worker physical-core 重复和 role logical overlap，并验证 auxiliary physical-core 集合与 IRQ physical-core 集合互不重叠。

profile：`config/host-profiles/infocom2027-node0.env`；SHA256：`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`。governor `performance`、8 combined queues、irqbalance stopped、RPS off、GRO/GSO/TSO off、LRO off、RX/TX checksum on、ring 1024、socket/sysctl/ulimit 目标均通过 effective verification。attempt：`node0/attempt-20260719T080242Z/`。

### WP3.2 node1 配置

冻结 role map：

```text
client-0 sender/receiver: CPU 0 / 1
client-1 sender/receiver: CPU 2 / 3
coordinator:              CPU 4
monitor:                  CPU 5
post-run archive:         CPU 6-7
experiment IRQs:          CPU 8-15
unused for experiment:    CPU 16-31
async IRQs:               CPU 4-5
XPS:                      CPU 0,2,4
```

profile：`config/host-profiles/infocom2027-node1.env`；SHA256：`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e`。节点真实不存在 cpufreq policy，profile 使用 `GOVERNOR_MODE=absent`，没有伪造 `performance`；频率采样入口已保留但本次没有运行 pilot。8 queues、IRQ affinity、RPS/XPS、offloads、ring 1024、socket/sysctl/ulimit 目标均通过 effective verification。attempt：`node1/attempt-20260719T080344Z/`。

### WP3.3 可恢复调优事务

两机均严格完成：

```text
capture before
→ validate proposed profile
→ dry-run
→ generate independent restore plan
→ restore-host-state syntax/check-plan
→ atomic apply
→ verify effective state (89/89)
→ verify management SSH/control route
→ no workload
→ independent restore
→ verify restored
→ repeated restore idempotency
→ before/effective/restored diff
```

结果：

| 项目 | node0 | node1 |
| --- | --- | --- |
| topology validator | PASS | PASS |
| dry-run | PASS | PASS |
| atomic apply | PASS | PASS |
| effective verification | PASS 89/89 | PASS 89/89 |
| management SSH after apply | PASS via `eno33np0` | PASS via `eno33np0` |
| independent restore | PASS | PASS |
| repeated restore | `PASS_ALREADY_RESTORED`, mutation 0 | `PASS_ALREADY_RESTORED`, mutation 0 |
| before/restored semantic diff | 0 bytes | 0 bytes |
| idempotency diff | 0 bytes | 0 bytes |
| control diff | 0 bytes | 0 bytes |
| workload / port 9000 / ring 4096 | NOT_RUN | NOT_RUN |

apply 入口在发生 mutation 后失败时会调用独立 restore；restore 入口可单独重复运行，不只依赖 shell trap。本轮未 reboot/reset，未修改 control NIC，最终主机状态与 before snapshot 无未解释差异。

**WP3 门禁：PASS。** 该结论只覆盖 profile deployment/recovery，不授权 WP4。

---

## WP4：基础设施 pilot 与 scheduler period 冻结

**计划窗口：** 2026-07-19 至 20
**依赖：** WP3 PASS；WP0.2 S3 完整闭环 PASS；负责人对 WP4 的新授权（当前后两项均不满足）

**当前状态：BLOCKED_NOT_AUTHORIZED_AND_S3_NOT_PASS。以下仅为未来计划，2026-07-19 本次任务未执行。**

使用非正式种子：

```text
infrastructure seeds: 1009, 1013
period candidates: 10, 25, 50, 100 us
```

### WP4.1 执行顺序

1. short cohort 验证配置和 schema；
2. W3 ρ=0.90 full cohort；
3. W2 ρ=0.85 full cohort；
4. 至少覆盖 L1 和 M1；
5. validator 只读取基础设施列，不读取 deadline、tail 或方法效果列。

### WP4.2 每个周期的连续通过条件

每个 candidate 在两个 anchors 上各连续通过三次：

- expected requests 全部发送、接受并可核对；
- send failures、malformed、unknown、duplicate 为 0；
- response queue overflow 为 0；
- runtime invariants 和 affinity 全部 PASS；
- NIC/UDP drop counter 增量为 0；
- realized offered load 与目标偏差不超过 2%；
- client send-lag P99 不超过 4 µs；
- scheduler missed-epoch fraction 不超过 5%；
- 无 ingress progress stall；
- 三次基础设施指标方向一致。

### WP4.3 period 选择

选择满足全部门禁的**最小周期**，写入：

- `EXPERIMENT_CONTRACT.md`；
- `config/infocom2027-physical.yaml`；
- `PROJECT_STATE.md`；
- aligned-simulation manifest。

不得根据正式算法效果选择 period。

### WP4.4 12-worker fallback

仅在正式结果不可见且满足以下任一条件时允许整体切换：

- 10/25/50/100 µs 在 16-worker 上均不稳定；
- 固定 profile 后仍有不可消除 ingress/IRQ/response 基础设施失败；
- worker CPU-service accuracy 或 affinity 无法过门禁。

固定 fallback：

```text
workers:     CPU 0-11
receiver:    CPU 12
responder:   CPU 13
scheduler:   CPU 14
server main: CPU 15
IRQs:        CPU 28-31, 4 combined queues
CPU 16-27:   不运行实验线程
```

切换后必须整体重新：

- 生成 traces；
- 探测 ingress mapping；
- 选择 period；
- 校准 arrival scale 和 handoff；
- 运行 aligned simulation；
- 运行完整 160-method-run 物理矩阵。

**最晚决策：** 2026-07-20 12:00 UTC。
**晚触发规则：** 若正式运行开始后才发现必须切换，禁止混合数据；在时间不足时降级论文 claim，而不是拼接 16/12-worker 结果。

---

## WP5：handoff、arrival calibration 与对齐仿真

**计划窗口：** 2026-07-20
**依赖：** WP4 period/worker profile 冻结

### WP5.1 handoff 冻结

- 使用 production handoff path；
- 使用非正式 workload；
- 输出 median/P95/P99 和样本数；
- `handoff_estimate_us` 冻结为 median；
- 保存 binary/config/host-profile/commit SHA。

### WP5.2 arrival calibration

Calibration seeds：

```text
2003, 2011
```

每个 anchor：

1. 只用 L0；
2. 根据 server 实际 receive span 与 trace CPU work 计算 realized service load；
3. 最多三次乘法修正；
4. 要求 `abs(realized_rho - target_rho) <= 0.01`；
5. scale 必须位于 `[0.8, 1.5]`；
6. 同一 anchor 四方法使用同一 scale；
7. 不查看或使用 deadline/tail 效果调 scale。

### WP5.3 冻结清单

formal 前一次性冻结：

- worker count；
- worker/control CPU IDs；
- IRQ/NIC profile；
- scheduler/poll period；
- handoff estimate；
- four arrival scales；
- source/destination ports；
- flow-socket count；
- ingress flow map；
- trace generator commit；
- run-order CSV SHA；
- config SHA；
- output schema version；
- validator commit。

### WP5.4 aligned simulation

使用冻结的物理参数重跑：

```text
4 anchors × 10 seeds × 4 methods
```

输出新的 claim-evidence baseline。原 Step-21 1 µs 结果只能作为历史仿真，不得直接代表物理配置。

**门禁：** physical contract 与 aligned-simulation manifest 均冻结，并计划打 tag：

```text
physical-formal-contract-v1
```

---

## WP6：formal 容量评估与 GO/NO-GO

**计划窗口：** 2026-07-20 formal 前
**依赖：** WP5 PASS

不得凭感觉启动 40 blocks。必须使用 full-cohort pilot 实测：

- `t_method_setup`；
- `t_method_run`；
- `t_method_collect`；
- `t_block_validate`；
- `t_block_archive`；
- `t_block_remote_verify`；
- 失败 attempt 的恢复时间。

定义：

```text
T_block_p95 = 四方法总耗时 + block validate + archive + remote verify 的 P95
T_remaining = remaining_blocks × T_block_p95
T_reserve = max(12h, 0.20 × T_remaining)
```

Formal GO 条件：

```text
now + T_remaining + T_reserve <= T_no_new_block (2026-07-23T23:00:00Z)
```

若不满足：

- 不得通过缩短 cohort、减少 seeds 或只跑有利 anchors 来制造“完整确认”；
- 可以保留 implementation、smoke、pilot 或限制性物理证据；
- 论文移除“完整 40-block 物理确认”表述；
- 明确记录 scope downgrade 决策及原因。

---

## WP7：正式 40-block / 160-run 执行

**计划窗口：** 2026-07-21 至 23；2026-07-24 仅 catch-up
**依赖：** WP6 GO

### WP7.1 coordinator 状态机

```text
PENDING
→ PREPARED
→ RUNNING
→ VALIDATING
→ ARCHIVING
→ REMOTE_VERIFYING
→ VALID_COMPLETE
 | VALID_APPLICATION_OVERLOAD
 | INFRASTRUCTURE_FAILURE
 | INCOMPLETE
```

每次状态转换必须原子写入，并包含：

- block ID；
- attempt ID；
- method；
- UTC timestamp；
- commit/config/trace/profile SHA；
- 上一状态；
- 转换原因；
- resume 指针。

### WP7.2 目录与命名

建议不可变目录：

```text
physical-results/formal/<contract-sha>/
  W3-rho085-seed011/
    attempt-01/
      L0_RandomCore/
      L1_WorkStealingPolling/
      M0_AltoThreshold/
      M1_RescueSched/
      block-validation/
      archive/
```

S3 object key 保持同一层级，并增加 archive SHA。任何 attempt 目录和 object key 禁止覆盖。

### WP7.3 每个 block 的顺序

1. 核对两机 lease、commit、dirty status、host-profile hash。
2. 核对 contract、trace、flow map、run order 和 config SHA。
3. 采集 block 前 host/NIC/UDP/perf counters。
4. 按 frozen schedule 顺序运行四方法。
5. 每个 method：
   - 启动全新 server process；
   - server readiness barrier PASS；
   - 两 clients 使用同一 absolute start timestamp；
   - 固定 destination/source ports；
   - 运行期间禁止压缩、上传、Git 和后台分析；
   - 完成后采集 node0 输出和 node1 client 输出；
   - 记录 method-level status，但不做算法效果判读。
6. 四方法完成后执行 formal validator。
7. 采集 block 后 counters 和 host drift。
8. 无论有效或失败都生成确定性 archive 与 SHA256。
9. 上传 S3 并用 remote streaming SHA 验证。
10. 远端确认后才允许清理两机 raw payload。
11. 更新 `PROJECT_STATE.md` 和 `EVIDENCE_INDEX.md`。

### WP7.4 method 正式输出

每个正式 method 至少包含：

```text
manifest.json
RUN_STATUS.json
requests.csv.zst
client_requests.csv.zst
migrations.csv.zst
decision_summary.csv.zst
decision_sample.csv.zst
summary.csv
host_before/
host_after/
SHA256SUMS
stdout.log
stderr.log
command.txt
```

Block 级额外包含：

```text
trace + trace SHA
run-order rows + schedule SHA
config snapshot + config SHA
flow map + flow-map SHA
validator report
attempt classification
archive SHA
remote object verification
```

### WP7.5 attempt 规则

- 任一方法为基础设施失败，整个 paired block 成为失败 attempt。
- 修复基础设施后，从 block 第一个方法重启。
- 最多三个 infrastructure attempts。
- 采用第一个完整有效 attempt。
- 所有失败 attempts 保留、压缩、上传并索引。
- 禁止从多个 attempts 拼接四方法。
- `VALID_APPLICATION_OVERLOAD` 是保留结果，不因结果不利重跑。

### WP7.6 进度看板

每完成一个 block 记录：

```text
valid_blocks / 40
valid_method_runs / 160
failed_attempts
remaining_blocks
observed_T_block_p50/p95
projected_finish_utc
last_remote_verified_object
host_profile_drift
```

每 4 个有效 blocks 做一次纯基础设施容量复核，不打开方法效果列。

---

## 6. 运行分类、验证与停止规则

### 6.1 `VALID_COMPLETE`

必须满足：

- 请求/响应完整；
- trace/config/commit/profile SHA 一致；
- 无 duplicate/unknown/malformed；
- affinity 和 runtime invariants PASS；
- response queue 无 overflow；
- 输出完整可解压；
- archive 与远端 SHA 一致。

### 6.2 `VALID_APPLICATION_OVERLOAD`

- 请求正确接受；
- 系统因真实应用负载出现 drain/timeout；
- 无基础设施故障或数据损坏；
- 作为有效结果保留，禁止因不利重跑。

### 6.3 `INFRASTRUCTURE_FAILURE`

包括但不限于：

- node/kernel/NIC/control loss；
- commit、trace、config、flow-map 或 schedule mismatch；
- affinity/topology 失败；
- packet corruption/drop；
- malformed/duplicate/unknown response；
- response queue overflow；
- runtime invariant 失败；
- scheduler catch-up storm/ingress stall；
- 输出缺失、损坏或 SHA 不一致；
- host profile 未生效或运行中漂移。

### 6.4 `INCOMPLETE`

- 用户/lease/进程中止；
- 未完成四方法；
- 未完成验证、归档或远端校验；
- 不能被当作有效 block。

### 6.5 全局停止条件

发生以下任一项时暂停正式运行：

- 连续两个 blocks 出现同类基础设施失败；
- 同一 block 达到三个失败 attempts；
- remote SHA 不一致；
- host restore 无法保证；
- profile/commit/config 漂移；
- 容量预测晚于 `T_no_new_block`；
- 剩余时间不足以完成验证、上传和恢复；
- 正式结果可见后有人提出会改变合同的调参。

暂停后只允许：保存现场、归档失败 attempt、定位基础设施问题、记录决策；不得选择性跳过不利 block。

---

## 7. 测试与验收矩阵

### 7.1 代码级

| 类别 | 最低覆盖 | PASS 条件 |
| --- | --- | --- |
| receiver idle race | 四方法各 ≥100 次 | 无早退、hang、丢请求 |
| CPU-time workload | 5/24/100 µs × 两类抢占 | 误差 ≤ `max(2 µs, 5%)` |
| topology | 正向与 sibling/duplicate/offline 负例 | 错误配置 fail closed |
| response queue | 并发、背压、overflow、shutdown、sender failure | 无静默丢失，分类正确 |
| scheduler | missed epoch、lag、压力下 ingress | 无 catch-up storm |
| L1 | distributed polling 和一次 poll 一次 attempt | 所有 steal 走公共 handoff |
| hidden-service | 编译/API/行为测试 | 当前 request service 不可被 policy 读取 |
| sanitizers | ASan/UBSan，核心并发 TSan | PASS 或已记录工具限制 |

### 7.2 两机级

- host tuning apply/restore 回归；
- S3 upload/download/streaming SHA 回归；
- 四方法 short smoke；
- request-to-ingress mapping 四方法一致；
- full-cohort 非正式 pilot；
- coordinator kill/restart/resume；
- node1 frequency 不可控时 send-fidelity gate；
- validator corruption fixtures：错误 commit、缺失文件、重复 request、损坏 trace、模拟 packet drop。

### 7.3 Formal validator 关键阈值

- expected request count 完整；
- send failure = 0；
- malformed/unknown/duplicate = 0；
- response queue overflow = 0；
- NIC/UDP drops 增量 = 0；
- offered load 偏差 ≤2%；
- calibration realized ρ 偏差 ≤0.01；
- send-lag P99 ≤4 µs；
- scheduler missed epoch fraction ≤5%；
- 所有 SHA 和 schema 校验 PASS。

---

## 8. 归档、S3 与证据链

### 8.1 数据保留原则

- CloudLab 本地盘只作暂存，不是唯一副本。
- raw result 在远端 SHA 验证前不得删除。
- failed attempt 与 valid attempt 同等级保存。
- archive 必须确定性生成，文件顺序、mtime/owner 归一化规则写入 runbook。
- S3 object 不覆盖；每个 object key 唯一对应 contract/block/attempt/archive SHA。

### 8.2 Evidence index 最小字段

```text
claim_id
paper_location
figure_or_table
analysis_output
block_id
attempt_id
method
trace_sha256
config_sha256
commit_sha
host_profile_sha256
archive_sha256
s3_object_key
validation_status
```

### 8.3 完整性复核

每日结束时只做基础设施级复核：

- 本地有效 block 数；
- S3 object 数；
- remote SHA PASS 数；
- 未上传/未验证/可清理列表；
- 失败 attempts 是否全部索引。

不得在正式矩阵未完成前查看聚合算法效果并据此调整运行计划。

---

## 9. 统计分析与论文同步

**计划窗口：** 2026-07-23 至 30；主要工作在物理机释放后离线完成。

### 9.1 固定统计

- 每个 seed 是一个 paired unit。
- 主比较：

```text
baseline deadline violation rate - M1 deadline violation rate
```

- 分别比较 L1 vs M1、M0 vs M1。
- paired bootstrap 95% CI；
- 10,000 bootstrap draws；
- 分析 random seed 固定并写入 manifest；
- 效率约束：M1 median migrated-work rate 不高于对应 baseline。

同时报告：

- SLO goodput；
- P50/P99/P999 server completion；
- client RTT；
- migration/handoff；
- scheduler attempts/cost/missed epochs；
- NIC/UDP drops；
- simulator-to-physical direction。

### 9.2 论文更新规则

- 清理旧五种子数字、旧 baseline、placeholder 和过时论断；
- Implementation 只描述真实 Linux UDP、stable source ports、`SO_REUSEPORT` shards、fixed-core workers 和 measured handoff；
- Evaluation 分开报告原 Step-21 仿真、aligned simulation、CloudLab physical；
- W3 ρ=0.70 和 W2 ρ=0.85 无论方向均进入论文；
- 物理/仿真反转如实报告；
- 每个数字在 `EVIDENCE_INDEX.md` 中有唯一来源；
- 私有 raw S3 不进入匿名稿；另生成去身份化只读 artifact。

### 9.3 投稿节点

- 2026-07-24：EDAS 标题、摘要、作者、COI、double-blind 和物理证据备份复核；
- 2026-07-30 12:00 PDT：内部上传截止，上传后下载 PDF 复核；
- 2026-07-31：官方 full-paper 截止，仅保留紧急窗口。

---

## 10. 风险、缓冲与降级策略

| 风险 | 早期信号 | 预防 | 触发后的动作 |
| --- | --- | --- | --- |
| 精确 lease 小时早于预期 | manifest 到期时刻 | 使用 `T_expire` 动态回推截止 | 提前停止新 block、先备份恢复 |
| integration 删除 simulator/paper | `git diff --name-status` 出现删除 | allowlist 集成、删除项审计 | 回退合并，不进入部署 |
| CPU topology 假设错误 | validator 与手工列表不一致 | sysfs/lscpu 自动选择 | 重冻结 profile，禁止人工绕过 |
| node1 send fidelity 不足 | send-lag/load 偏差 | 固定 CPU/IRQ、full pilot | 调基础设施；不按效果调算法 |
| scheduler 追赶风暴 | missed epochs/lag 激增 | absolute epoch/skip stale | candidate period 上调并重做 pilot |
| response queue overflow | high-water 持续接近容量 | 有界队列和 sender 隔离 | 基础设施失败，修复后整 block 重跑 |
| S3/网络不可用 | upload/remote SHA 失败 | 每 block 后立即归档 | 保留双机 raw，暂停清理和新 block |
| 正式耗时超预算 | `projected_finish_utc` 推迟 | WP6 容量门禁、20%/12h buffer | 降级 claim，不缩 seeds/cohort |
| 16-worker 必须切 12 | pilot 全候选失败 | 7 月 20 日 12:00 前决策 | 全量重冻结；时间不足则降级 |
| 运行中 host drift | profile hash/counters 变化 | block 前后采集 | 当前 attempt 失败，恢复后重跑 |
| 结果不利引发调参冲动 | 提议改 period/scale/seed | 预注册合同 | 拒绝选择性修改；如改则全量重跑 |

缓冲分配：

- 2026-07-23 18:00 至 07-24 12:00：失败 attempt/catch-up；
- 2026-07-23T23:00:00Z (`T_no_new_block`) 后：不得启动新 block，只做完成中的验证、远端校验和恢复准备；
- 2026-07-24T05:00:00Z (`T_restore`) 后：只做恢复、备份复核和应急；2026-07-24T11:00:00Z 为 `T_expire`。

---

## 11. 责任划分与人工冻结点

### 11.1 Codex/执行代理负责

- integration branch 和选择性集成；
- runtime、tests、coordinator、validator、resume；
- 两机部署与 host profile apply/restore；
- period pilot、arrival calibration 和 handoff；
- formal block 执行与失败分类；
- S3 归档/校验与 evidence index；
- 统计、绘图和论文数字同步；
- 每个工作包后的控制文档更新。

### 11.2 用户负责

- 用户于 2026-07-18T15:17:01Z 批准的 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2` 保留为历史 WP2 范围决定；2026-07-19 已另行依靠 live AM status 与明确站点时区冻结 WP3 所需 exact UTC，manifest 子检查仍保持 stale/BLOCKED，不得写成 `PASS_MANIFEST`；
- 用户已批准 S3 在 WP3 阶段保持 deferred 和 GitHub publication `WAIVED_BY_OWNER`；S3 仍须在 WP4/pilot/formal 前真实 PASS；
- pilot/formal 前创建并提供 AWS S3 私有 bucket/最小权限凭据；
- 通过仓库外 secret 文件提供访问；
- 审核以下冻结点：
  1. integration 与研究合同；
  2. 16/12 worker 和 scheduler period；
  3. handoff/arrival/flow map；
  4. formal GO；
  5. 统计与论文 claim freeze；
  6. EDAS/PDF 最终提交。

### 11.3 必须人工确认的 GO 点

| Gate | 输入 | 允许的决定 |
| --- | --- | --- |
| G1 Integration | clean commit、tests、保留资产审计 | GO / 修复 |
| G2 Host profile | topology、profile SHA、apply/effective/restore、management SSH | **PASS（WP3 profile/recovery only）** / 修复；S3 是 WP4/pilot/formal 的独立硬门禁 |
| G3 Formal contract | period、worker、scale、handoff、flow map、capacity | GO / 12-worker fallback / scope downgrade |
| G4 Formal completion | 40 valid blocks、remote SHA、evidence index | freeze / 明确不完整 |
| G5 Paper claim | paired analysis、boundaries、limitations | claim freeze / 降级 claim |

---

## 12. 每日收尾清单

每天结束前记录且只保留一个权威状态：

```text
[ ] UTC timestamp
[ ] current phase/work package
[ ] branch + full commit
[ ] dirty status
[ ] node0/node1 deployed commit
[ ] lease exact timestamp / T_expire
[ ] host-profile SHA
[ ] contract/config/schedule SHA
[ ] completed gates
[ ] valid blocks / 40
[ ] failed attempts
[ ] local-only archives
[ ] S3 uploaded archives
[ ] remote SHA verified archives
[ ] projected finish UTC
[ ] blockers
[ ] next three tasks maximum
[ ] restore readiness
```

---

## 13. 最终完成标准

项目仅在以下条件全部满足时标记完成：

- 40 个有效 paired blocks；
- 160 个有效 method-runs；
- 每个 block 有独立 trace/config/commit/host/schedule/SHA 证据；
- 所有 valid 和 failed attempts 均完成 S3 remote SHA256；
- aligned simulation 和 physical analysis 可从 clean environment 一条入口重建；
- 论文全部数值进入 evidence index；
- 两机设置恢复并有 restore diff；
- integration 分支 clean、tagged、可复建；
- 项目状态为 `PHYSICAL_EVIDENCE_FROZEN`；
- 结果 tag 计划名：`physical-results-v1`。

若截至 **2026-07-24** 未完成全部 40 个有效 blocks：

- 禁止挑选部分 seeds 支撑“完整物理效果确认”；
- 必须移除或降级该表述；
- 只保留其证据等级足以支持的 implementation、smoke、pilot、部分观察或限制性结果；
- 不得临时缩 cohort、删 anchors 或混合 attempts/worker profiles。

---

## 14. 本计划落盘后的状态

截至 2026-07-19 WP3 收尾：

```text
lease gate: PASS_EXACT_UTC_FROZEN_FOR_WP3
manifest subcheck: BLOCKED_STALE_OR_NON_PROPAGATED_VALUE
T_expire: 2026-07-24T11:00:00Z
T_no_new_block: 2026-07-23T23:00:00Z
S3 gate: DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS (no object operations run)
integration branch: codex/infocom2027-integration
frozen integration baseline: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
release tag: physical-integration-v1 (annotated; object/target unchanged)
remote GitHub publication: WAIVED_BY_OWNER; no push in this task
WP2 source commit: 7db91095c4d0f84a5eb568b980748051f994c4cc
WP2 code-level status: PASS_CODE_LEVEL_ON_BOTH_NODES
WP3 tooling/profile commits: b8eec4768d9dc556f43af925a9520e05c503b7a5, 90c5ff6d774cea0e06563e92681570727b079933
WP3 node0 profile SHA: 50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b
WP3 node1 profile SHA: fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e
WP3 topology/dry-run/apply/effective/restore/idempotency: PASS on both nodes
WP3 effective checks: 89/89 on each node
WP3 repeated restore: PASS_ALREADY_RESTORED, mutation 0 on each node
WP3 restored semantic/control diffs: 0 bytes on each node
host final state: RESTORED_TO_BEFORE; tuning not left applied
WP4: BLOCKED_NOT_AUTHORIZED_AND_S3_NOT_PASS
RPC smoke: NOT_RUN
pilot: NOT_RUN
scheduler/arrival calibration: NOT_RUN
formal runs: NOT_RUN
formal port 9000: NOT_USED
ring 4096 pilot condition: NOT_USED
```

这一区分用于确保“WP3 profile/recovery 已完成”不会被误解为“WP4 或正式性能实验已执行”。
