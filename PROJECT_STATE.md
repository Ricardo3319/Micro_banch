# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-19T08:27:23Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP0.1 租期证据 | **PASS_EXACT_UTC_FROZEN_FOR_WP3** | 2026-07-19 在 node0/node1 重新实时执行 `geni-get -n manifest` 与 `geni-get -n status`。manifest 仍为旧值 `2026-07-18T03:00:00Z`，子检查保持 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`；两机 live AM status 均为 `2026-07-24 05:00:00`。依据实时 Utah CloudLab 官方站点 `-0600` 时区、精确部署源码和 `America/Denver` 站点配置，冻结 `T_expire=2026-07-24T11:00:00Z`、`T_no_new_block=2026-07-23T23:00:00Z`。该 PASS 的授权范围仅为 WP3 host profile apply/restore。 |
| WP0.2 S3 闭环 | **DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS** | 两机首选仓库外配置 `~/.config/rescuesched/rclone.conf` 均不存在；expected remote、private bucket、project-dedicated prefix 未提供。未读取或记录 credential，upload、remote SHA、download、download SHA、delete、absence confirmation 全部 `NOT_RUN`。该项不阻塞已授权的 WP3，但继续硬阻塞 WP4/pilot/formal。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUBLICATION_WAIVED_BY_OWNER** | 冻结 annotated tag `physical-integration-v1` 的 object/target 保持 `2fb914080bc63d357e38b8f4e21a6d5add34dc8e` → `0a88f03a21802be0eadc3065b93cb97876a6bd2f`。GitHub publication 仍为 owner waiver；本次没有 push、force push 或 remote URL 修改。 |
| WP2 runtime | **PASS_CODE_LEVEL_ON_BOTH_NODES** | WP2 源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 的两机代码级验证保持有效：Release 26/26、ASan/UBSan 26/26、TSan 核心 2/2、本机 loopback/mapping/fail-closed gate 和 synthetic 12/12。它仍不是双机 RPC、pilot 或正式物理结果。 |
| WP3 host profile | **PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION_ON_BOTH_NODES** | node0/node1 topology validator、profile freeze、before capture、dry-run、atomic apply、89 项 effective verification、management SSH、独立 restore、第二次零修改 restore 和 restored semantic diff 均 PASS。两机均已恢复到 before 状态，未保持调优态。 |
| WP4 及以后 | **BLOCKED_NOT_AUTHORIZED_AND_S3_NOT_PASS** | 不允许双机 RPC、pilot、scheduler period calibration、arrival calibration、formal paired block 或正式性能结论；正式端口 `9000`、ring `4096` 条件和正式结果目录不得使用。 |

**当前允许事项：** 保存并复核 WP0.1/WP0.2/WP3 证据、同步同一文档提交、只读验证两机 identity/tag，以及补齐进入 WP4 前的非敏感 S3 输入。

**当前禁止事项：** WP4、双机 RPC、pilot、calibration、formal experiment、正式端口 `9000`、ring `4096` pilot 条件和正式结果目录。WP3 已完成恢复，不得把本次结果描述为性能实验。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
frozen_integration_commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
current_status_commit: SELF
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

## 3. WP0.1：精确租期 UTC freeze

2026-07-19 的两机实时采集结果：

| 节点 | hostname | manifest expiration | manifest SHA256 | live AM status raw expiration | live status SHA256 |
| --- | --- | --- | --- | --- | --- |
| node0 | `amd140.utah.cloudlab.us` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` | `2026-07-24 05:00:00` | `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12` |
| node1 | `amd136.utah.cloudlab.us` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` | `2026-07-24 05:00:00` | `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12` |

解释和冻结依据：

1. manifest 原始 XML、开始/结束 UTC、hostname、exit code、expiration 和 SHA 均重新实时采集，未使用旧缓存；manifest 子检查仍保持旧值事实。
2. 两机 `geni-get -n status` 返回相同 sliver expiration 原始字段 `2026-07-24 05:00:00`。
3. 实时 Utah CloudLab 官方 RSS 响应给出数字偏移 `-0600`；实时 `geni-get -n getversion` 对应部署源码提交 `0b1fdb15cd434591f3fab98799d78189698686fc`，保存的 exact source 和站点配置证明该字段按站点 `America/Denver` 本地时间解释。
4. 2026-07-24 位于 MDT，偏移 `-06:00`，所以 `2026-07-24 05:00:00 America/Denver = 2026-07-24T11:00:00Z`。旧 Portal `Jul 24, 2026 7:00 PM` 截图无时区，只作为保留的历史补充事实，不用于精确 UTC 计算。

冻结结果：

```text
decision_utc=2026-07-19T07:28:13Z
T_expire=2026-07-24T11:00:00Z
T_no_new_block=2026-07-23T23:00:00Z
remaining_at_decision=5d 03h 31m 47s
wp3_apply_restore_safety=PASS_SUFFICIENT_TIME
scope=WP3_HOST_PROFILE_APPLY_RESTORE_ONLY
```

证据根：

```text
physical-results/wp0-wp3-20260719T070542Z/wp0.1/
```

关键入口为 `GATE_STATUS.env`、`LEASE_DECISION.txt`、`authority-analysis/deployed-source-0b1fdb15/` 和 `authority-analysis/live-site-timezone/`。AM 直连 403、remote wrapper 和 SCP dot-path 等失败 attempts 全部保留。

## 4. WP0.2：S3 最小权限闭环

状态：`DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS`。

只检查了以下非敏感事实：

- node0/node1 的 `~/.config/rescuesched/rclone.conf` 均不存在；
- `rclone` binary 可用；
- expected remote name 缺失；
- private bucket name 缺失；
- project-dedicated prefix 缺失。

没有读取、打印、复制或提交任何 credential；没有创建测试对象，没有 upload/download/delete。该状态 `blocks_wp3=false`、`blocks_wp4_pilot_formal=true`。

证据根：

```text
physical-results/wp0-wp3-20260719T070542Z/wp0.2/
```

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

所有旧 BLOCKED、失败 attempt、TSan 诊断、coordinator bug 和意外 invocation 证据继续保留。新的唯一证据根：

```text
physical-results/wp0-wp3-20260719T070542Z/
```

继续未执行、未授权：

```text
WP4=NOT_RUN
two_node_RPC=NOT_RUN
pilot=NOT_RUN
scheduler_period_calibration=NOT_RUN
formal_experiment=NOT_RUN
formal_port_9000=NOT_USED
ring_4096=NOT_USED
GitHub_push=NOT_PERFORMED
```

进入 WP4/pilot/formal 前，至少必须完成 WP0.2 S3 最小权限闭环，并获得对应阶段的新授权。scheduler/poll period、handoff estimate、arrival scales、formal config SHA、formal archive/S3 object 和 16/12-worker 最终选择仍保持未冻结。
