# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-19T13:54:25Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP4-A 双机身份 | **PASS** | 2026-07-19T13:29:49Z 实时确认 node0=`amd140.utah.cloudlab.us`、node1=`amd136.utah.cloudlab.us`；两机 branch 均为 `codex/infocom2027-integration`，初始 HEAD 均为 `77f06874de97d06ded41b2bd647e02e16973fb9c`，working tree clean；annotated tag `physical-integration-v1` 的 kind/object/target 仍为 `tag`、`2fb914080bc63d357e38b8f4e21a6d5add34dc8e`、`0a88f03a21802be0eadc3065b93cb97876a6bd2f`；profile SHA 未漂移；未发现未知实验可执行进程、端口 9000 listener 或未知 dirty。上一轮 `SHA256SUMS` SHA256 复核为 `456820bdf445c24c5502e6794249d972b57f7e6f319415f5f97e88158a9edd5f`，2755 项全部通过。 |
| WP0.1 租期证据 | **PASS_EXACT_UTC_FROZEN_FOR_WP4** | 两机重新实时执行 `geni-get -n manifest/status/getversion`。manifest 仍为旧值 `2026-07-18T03:00:00Z`，子检查保持 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`；两机 live status 均为 `2026-07-24 05:00:00`。依据实时 Utah CloudLab 官方站点 `-0600`、live deployed AM commit 与保存的 exact source，冻结 `T_expire=2026-07-24T11:00:00Z`、`T_no_new_block=2026-07-23T23:00:00Z`。2026-07-19T13:40:14Z 距到期 `4d 21h 19m 46s`，距安全线 `4d 09h 19m 46s`。该 PASS 只关闭 WP4 的 lease 子门禁，不授权或表示 WP4 workload 已执行。 |
| WP0.2 S3 闭环 | **BLOCKED_MISSING_INPUTS** | 2026-07-19T13:42:15Z 实时确认两机首选仓库外配置 `~/.config/rescuesched/rclone.conf` 均不存在，无法满足 `0600`；用户提供的是 expected remote、private bucket、project-dedicated prefix 占位符而非可用值。未读取或记录 credential，remote list、upload、remote SHA、download、download SHA、exact delete、absence confirmation 全部 `NOT_RUN`。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUBLICATION_WAIVED_BY_OWNER** | 冻结 annotated tag `physical-integration-v1` 的 object/target 保持 `2fb914080bc63d357e38b8f4e21a6d5add34dc8e` → `0a88f03a21802be0eadc3065b93cb97876a6bd2f`。GitHub publication 仍为 owner waiver；本次没有 push、force push 或 remote URL 修改。 |
| WP2 runtime | **PASS_CODE_LEVEL_ON_BOTH_NODES** | WP2 源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 的代码级验证保持有效；它仍不是双机 RPC、pilot 或正式物理结果。 |
| WP3 host profile | **PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION_ON_BOTH_NODES** | 历史 WP3 apply/effective/restore regression 保持有效；两机当前均为 restored/before 状态。本次 WP4-A 没有 apply profile、host tuning 或 workload。 |
| WP4 workload 及以后 | **BLOCKED_S3_NOT_PASS_AND_NOT_AUTHORIZED** | identity 与 lease 子门禁已实时关闭，但 S3 最小权限恢复闭环未通过；本次硬停止于 WP4-A。不得运行双机 RPC、short cohort、infrastructure pilot、scheduler period、calibration、WP5 或 formal；端口 `9000` 与 ring `4096` 未使用。 |

**当前允许事项：** 保存并复核本轮 WP4-A identity/lease/S3 证据，提交和同步同一状态文档，或由负责人在仓库外补齐 S3 的非敏感 remote/bucket/prefix 与 `0600` 配置后另行重新授权门禁任务。

**当前禁止事项：** host profile apply、host tuning、双机 RPC、WP4 short cohort/pilot、scheduler period 测试、calibration、WP5、formal experiment、端口 `9000`、ring `4096`。identity/lease PASS 不得写成 WP4 workload PASS。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
frozen_integration_commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
current_status_commit: SELF
current_documentation_parent_commit: 77f06874de97d06ded41b2bd647e02e16973fb9c
wp2_source_commit: 7db91095c4d0f84a5eb568b980748051f994c4cc
wp3_tooling_commit_1: b8eec4768d9dc556f43af925a9520e05c503b7a5
wp3_tooling_commit_2: 90c5ff6d774cea0e06563e92681570727b079933
release_tag: physical-integration-v1
release_tag_object: 2fb914080bc63d357e38b8f4e21a6d5add34dc8e
release_tag_target: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
release_tag_kind: annotated
remote_branch_actual: ABSENT_AT_LAST_REMOTE_CHECK
remote_tag_actual: ABSENT_AT_LAST_REMOTE_CHECK
remote_publication: WAIVED_BY_OWNER
this_run_github_push: NOT_PERFORMED
```

WP3 新增的 tracked 工具和冻结 profile：

```text
config/host-profiles/infocom2027-node0.env
config/host-profiles/infocom2027-node1.env
scripts/lib/host_profile_common.sh
scripts/capture_physical_host_state.sh
scripts/validate_host_profile.py
scripts/prepare_host_restore_plan.sh
scripts/apply_host_profile.sh
scripts/verify_host_profile.sh
scripts/restore_host_state.sh
```

选择性集成保留了 `src/core/simulator.cpp`、`paper/main.tex`、`artifacts/step-21-corrected-full/manifest.md`、`scripts/corrected_eval_analysis.py` 和所有旧失败/BLOCKED/TSan/coordinator 证据。

## 3. WP0.1：WP4 精确租期 UTC freeze

2026-07-19 WP4-A 两机实时采集结果：

| 节点 | hostname | manifest expiration | manifest SHA256 | live status raw expiration | live status SHA256 |
| --- | --- | --- | --- | --- | --- |
| node0 | `amd140.utah.cloudlab.us` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` | `2026-07-24 05:00:00` | `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12` |
| node1 | `amd136.utah.cloudlab.us` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` | `2026-07-24 05:00:00` | `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12` |

两机 status 均包含 slice `urn:publicid:IDN+emulab.net:micro3319+slice+Racked` 和 sliver `2419568`、`2419569`，均为 `geni_provisioned` / `geni_ready`。manifest、status、getversion 和 live timezone 响应在两机逐字节一致。

权威来源与时区审计：

1. manifest 原始 XML、开始/结束 UTC、hostname、exit code、expiration 和 SHA 均本轮实时采集；其过期旧值保留为 stale/non-propagated contradiction，不作为当前 lease authority。
2. 两机 live `geni-get -n status` 返回相同原始数据库时间 `2026-07-24 05:00:00`。
3. 两机 live `geni-get -n getversion` 均指向 Utah CloudLab AM v3 endpoint 和 deployed commit `0b1fdb15cd434591f3fab98799d78189698686fc`，输出 SHA256 均为 `132ba180bcbc64a659aca4f9ed6d3b3d15e8e78053731c55f620f54ca679b33c`。
4. 本轮实时官方 Utah CloudLab RSS 的 `pubDate` 为 `Sun, 19 Jul 2026 07:33:37 -0600`，与 HTTP GMT Date 表示同一时刻；RSS SHA256 两机均为 `287601b9d33155778e70c8d62dd9ee32eea334601817854299494cfce53a48f4`。本地 CA 链导致首次 TLS 验证 exit 60，失败 attempt 原样保留；重试仅对公开时区响应使用 `curl --insecure` 并明确记录，未涉及 credential。
5. 上一轮完整证据根已重新执行 `sha256sum -c`，2755 项全部通过；其 exact deployed source 证明 tmcd 原样输出 DB datetime、站点配置为 `America/Denver`。2026-07-24 的偏移为 `-0600`，故原始值精确映射为 `2026-07-24T11:00:00Z`。

本轮冻结结果：

```text
decision_utc=2026-07-19T13:40:14Z
T_expire=2026-07-24T11:00:00Z
T_no_new_block=2026-07-23T23:00:00Z
remaining_to_T_expire=4d 21h 19m 46s (422386 seconds)
remaining_to_T_no_new_block=4d 09h 19m 46s (379186 seconds)
status=PASS_EXACT_UTC_FROZEN_FOR_WP4
safe_execution_window_for_wp4=true
wp4_workload_authorized_or_run=false
```

证据根：

```text
physical-results/wp4-a-gates-20260719T132156Z/03-lease/
```

关键入口为 `decision/GATE_STATUS.env`、`decision/decision.json`、两机 `manifest/`、`status/`、`authority/getversion/` 和 `authority/live-site-timezone/`。所有 TLS、parser 和 wrapper 失败 attempts 均保留。

## 4. WP0.2：S3 最小权限闭环

状态：`BLOCKED_MISSING_INPUTS`；decision UTC=`2026-07-19T13:42:15Z`。

本轮只实时检查非敏感事实，未读取配置内容：

- node0/node1 的首选仓库外配置 `~/.config/rescuesched/rclone.conf` 均不存在；
- 因配置不存在，required mode `0600` 不满足，remote list 未执行；
- 两机 `rclone` binary 可用；
- expected remote name 缺失；
- private bucket name 缺失；
- project-dedicated prefix 缺失。

没有读取、打印、复制或提交 credential；没有创建随机文件或 S3 对象；upload、remote streaming SHA、download、download SHA、三 SHA 比对、exact object delete 和 absence confirmation 均为 `NOT_RUN`。不得把该状态写成 S3 PASS，也不得把 lease 子门禁 PASS 扩写为 WP4 整体 PASS。

证据根：

```text
physical-results/wp4-a-gates-20260719T132156Z/04-s3/
```

缺失项只登记于 `MISSING_INPUTS.txt`；门禁判定为 `GATE_STATUS.env`。

## 5. WP3.0：实际 topology 与 NIC

两机一致的硬件事实：

```text
online logical CPUs: 0-31
physical packages: 1
physical cores: 16
threads per core: 2
SMT sibling pairs: CPU n <-> CPU 16+n, n=0..15
NUMA nodes: 1
experiment NIC: enp65s0f0np0 (10.10.1.x)
control NIC: eno33np0 (128.110.219.x)
experiment NIC driver: mlx5_core
experiment NIC firmware: 16.28.4512 (DEL0000000015)
experiment NIC PCI: 0000:41:00.0
experiment NIC sysfs NUMA node: -1
```

validator 在修改前验证 online/duplicate/SMT/role/NIC/queue/ring/governor 合同。experiment NIC 与 management NIC 被明确区分；所有 NIC、IRQ、RPS/XPS、offload、ring、RSS、sysctl 变更只针对 experiment profile，control NIC 未修改。

## 6. WP3.1 node0 profile

冻结 role map：

```text
workers: 0-15
receivers: 16-17
response senders: 18-19
scheduler: 20
server main: 21
housekeeping: 22-23
experiment IRQs: 24-31
async IRQ affinity: 22-23
XPS CPUs: 18-21
```

worker `0-15` 覆盖 16 个唯一 physical cores。由于机器只有 16 个 physical cores，而合同需要 32 个逻辑角色，profile 显式冻结 `ALLOW_WORKER_AUX_SMT_SIBLINGS=true`：辅助/IRQ logical CPUs 是 worker 的 SMT siblings；辅助角色物理 core 集合与 IRQ 物理 core 集合互不重叠，所有 logical CPU 唯一且各角色不重用同一 logical CPU。该例外由 validator 明确检查，不是绕过 validator。

```text
profile_sha256=50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b
governor_effective=performance
combined_queues=8
ring_rx_tx=1024/1024
irqbalance_effective=inactive
RPS=off
GRO/GSO/TSO/LRO=off
RX/TX checksum=on
rmem_max/wmem_max=16777216
netdev_max_backlog=250000
socket_request_probe=8388608
ulimit_probe=65536
```

事务结果：dry-run PASS；apply `PASS_APPLIED_AND_EFFECTIVE_VERIFIED`；effective verification 89/89；独立 restore PASS；重复 restore `PASS_ALREADY_RESTORED` 且 mutation count 0；before/restored、idempotency 和 control diff 均 0 bytes。node1 经 management IP 新建 SSH 到 node0，effective/restored 后均通过且 route 为 `eno33np0`。

证据：

```text
physical-results/wp0-wp3-20260719T070542Z/wp3/node0/attempt-20260719T080242Z/
```

## 7. WP3.2 node1 profile

冻结 role map：

```text
client-0 sender/receiver: 0,1
client-1 sender/receiver: 2,3
coordinator: 4
monitor: 5
post-run archive: 6-7
experiment IRQs: 8-15
unused for experiment: 16-31
async IRQ affinity: 4-5
XPS CPUs: 0,2,4
```

CPU `0-15` 各自位于唯一 physical core；`16-31` 为其 SMT siblings，本次不运行实验线程。node1 没有 cpufreq policy，profile 和 validator 均登记 `GOVERNOR_MODE=absent`，未伪造 `performance`。

```text
profile_sha256=fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e
cpufreq_policy=absent
combined_queues=8
ring_rx_tx=1024/1024
irqbalance_effective=inactive
RPS=off
GRO/GSO/TSO/LRO=off
RX/TX checksum=on
rmem_max/wmem_max=16777216
netdev_max_backlog=250000
socket_request_probe=8388608
ulimit_probe=65536
```

事务结果：dry-run PASS；apply `PASS_APPLIED_AND_EFFECTIVE_VERIFIED`；effective verification 89/89；独立 restore PASS；重复 restore `PASS_ALREADY_RESTORED` 且 mutation count 0；before/restored、idempotency 和 control diff 均 0 bytes。node0 经 management IP 新建 SSH 到 node1，effective/restored 后均通过且 route 为 `eno33np0`。

证据：

```text
physical-results/wp0-wp3-20260719T070542Z/wp3/node1/attempt-20260719T080344Z/
```

## 8. WP3.3 可恢复事务门禁

实际顺序严格为：

```text
capture before
→ standalone topology validator
→ dry-run (zero mutation)
→ final independent restore plan
→ restore entry bash -n and plan integrity/host check
→ atomic apply
→ effective-state verification
→ management SSH verification
→ effective snapshot
→ independent restore
→ restored verification and snapshot
→ repeated restore zero-mutation path
→ repeated restored verification
```

两机最终结果：

| Gate | node0 | node1 |
| --- | --- | --- |
| topology validator | PASS | PASS |
| dry-run | PASS | PASS |
| atomic apply | PASS | PASS |
| effective verification | PASS 89/89 | PASS 89/89 |
| management SSH after apply | PASS via `eno33np0` | PASS via `eno33np0` |
| independent restore | PASS | PASS |
| restored verification | PASS 9/9 | PASS 9/9 |
| repeated restore | PASS_ALREADY_RESTORED, mutation 0 | PASS_ALREADY_RESTORED, mutation 0 |
| before/restored semantic diff | 0 bytes | 0 bytes |
| repeated-restore diff | 0 bytes | 0 bytes |
| control diff | 0 bytes | 0 bytes |

WP3 总状态：`PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION_ON_BOTH_NODES`。恢复后 `irqbalance` 再次 active；没有 reboot/reset，没有用 `git reset --hard`，没有关闭 management NIC，没有运行 workload。

## 9. 测试、证据保留与后续门禁

WP3 tracked 工具与最终文档/config 验证：

- `git diff --check`: PASS；
- WP3 shell `bash -n`: PASS；
- `python3 -m py_compile scripts/validate_host_profile.py`: PASS；
- `config/infocom2027-physical.yaml` parse 与冻结字段断言：PASS；
- tracked-file credential-pattern scan: PASS；
- node0/node1 最终 Release CTest: 26/26 PASS；
- `shellcheck`: UNAVAILABLE，exit 127，已记录而非伪造 PASS。

最终验证证据：

```text
physical-results/wp0-wp3-20260719T070542Z/final-validation-20260719T082516Z/
```

其中保留了两次不影响测试结论的证据核验调用问题：一次外层 wrapper 报告 exit 1 但捕获的 SSH/远端 CTest 均为 exit 0；一次在错误 cwd 校验 repository-relative `SHA256SUMS` 导致 readback 失败。两者均保留为 `failed-attempt-*`，随后已从 repository root 独立复核并复制 node1 Release 证据，SHA 校验 PASS。

所有旧 BLOCKED、失败 attempt、TSan 诊断、coordinator bug 和意外 invocation 证据继续保留；上一轮证据未修改且 2755 项 SHA 校验全部通过。

本轮 WP4-A 新证据根：

```text
physical-results/wp4-a-gates-20260719T132156Z/
```

本轮只执行 identity、lease 与 S3 非敏感门禁检查。identity=`PASS`，lease=`PASS_EXACT_UTC_FROZEN_FOR_WP4`，S3=`BLOCKED_MISSING_INPUTS`，所以 WP4 整体仍未获准启动。

本轮 tracked 文档/config 的最终静态验证证据位于 `physical-results/wp4-a-gates-20260719T132156Z/05-git/validation-final/`：`git diff --check`、YAML parse/exact assertions、credential-pattern scan、Git ignore、frozen tag 与 profile SHA 复核 PASS；无 tracked shell/Python 变更，相关语法/compile 检查为 `NOT_APPLICABLE`；`shellcheck` 不可用并如实记录。首次 YAML validator 使用错误顶层 key 的失败 invocation 保留于 `05-git/validation-20260719T135240Z/`，未修改 tracked 文件。

继续未执行、未授权：

```text
host_tuning=NOT_RUN
profile_apply=NOT_RUN
WP4=NOT_RUN
two_node_RPC=NOT_RUN
pilot=NOT_RUN
scheduler_period_test=NOT_RUN
calibration=NOT_RUN
WP5=NOT_RUN
formal_experiment=NOT_RUN
formal_port_9000=NOT_USED
ring_4096=NOT_USED
GitHub_push=NOT_PERFORMED
```

进入 WP4/pilot/formal 前，至少必须完成 WP0.2 S3 最小权限闭环，并获得对应阶段的新授权。scheduler/poll period、handoff estimate、arrival scales、formal config SHA、formal archive/S3 object 和 16/12-worker 最终选择仍保持未冻结。
