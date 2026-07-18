# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-18T16:59:01Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP0.1 租期证据 | **PASS_OWNER_ATTESTED_PORTAL_FOR_WP2** | 项目负责人于 2026-07-18T15:17:01Z 确认采用实时 Portal 实验页作为 WP2 前置租期依据。页面显示实验 `ready`、`Jul 24, 2026 7:00 PM`；两机 manifest 子检查仍显示旧值 `2026-07-18T03:00:00Z`，保持 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`，不得描述为 manifest 已证明 2026-07-25。Portal 未显示时区，精确 expiration UTC 仍未冻结。 |
| WP0.2 S3 闭环 | **DEFERRED_UNTIL_PILOT** | 负责人明确同意 WP2 前暂不执行 S3 闭环。rclone 配置、remote、private bucket/project prefix 仍缺失，upload、remote SHA、download、local SHA 和 delete 全部 `NOT_RUN`；进入 pilot 或 formal 前仍必须完成，不得写成 PASS。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUBLICATION_WAIVED_BY_OWNER** | 冻结基线 `0a88f03a21802be0eadc3065b93cb97876a6bd2f` 的两机 Release 24/24 CTest 历史证据保持有效；WP2 源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 已在两机 clean/same commit 上验证。annotated tag 未移动。GitHub 远端 branch/tag 实际仍为 `ABSENT`，没有 push；负责人同意该发布不阻塞 WP2。 |
| WP2 runtime | **PASS_CODE_LEVEL_ON_BOTH_NODES** | WP2.1–WP2.9 的代码路径已在源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 完成；node0/node1 各自 Release 26/26、ASan/UBSan 26/26、TSan 核心 2/2、本机四策略 UDP mapping/fail-closed gate 和 3×4 synthetic smoke 均 PASS。该状态只证明两机分别完成代码级验证，不是双机 RPC、pilot 或正式物理结果，也不自动授权 WP3。 |
| WP3 及以后 | **BLOCKED** | 不允许 CPU/IRQ/NIC tuning、host profile apply、两机 RPC smoke、pilot、calibration 或 formal experiment；不得使用正式端口或正式结果目录。 |

**当前允许事项：** 保存 WP2 最终身份/测试证据，以及处理后续阶段所需的精确 lease UTC 与 S3 最小权限门禁；任何 WP3 执行仍需新的明确放行。

**当前禁止事项：** WP3、host tuning、host profile apply、两机 RPC smoke、pilot、calibration 和 formal paired block。进入这些阶段前必须冻结可审计的精确 lease UTC，并按相应门禁完成 S3 最小权限恢复闭环。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
frozen_integration_commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
current_status_commit: SELF
wp2_source_commit: 7db91095c4d0f84a5eb568b980748051f994c4cc
release_tag: physical-integration-v1
release_tag_object: 2fb914080bc63d357e38b8f4e21a6d5add34dc8e
release_tag_target: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
release_tag_kind: annotated
remote_branch_actual: ABSENT
remote_tag_actual: ABSENT
remote_publication: WAIVED_BY_OWNER
```

选择性集成保留了以下历史研究资产：

- `src/core/simulator.cpp`
- `paper/main.tex`
- `artifacts/step-21-corrected-full/manifest.md`
- `scripts/corrected_eval_analysis.py`

物理裁剪分支中会破坏完整 simulator 的公共 `constants.h` / `types.h` 简化版本未导入。物理 runtime、UDP RPC、trace generator、测试和辅助脚本按 allowlist 集成，公共构建文件手工合并。

## 3. 租期事实、来源差异与负责人调整

| 节点 | hostname | 实时采集 UTC | manifest expiration | manifest SHA256 |
| --- | --- | --- | --- | --- |
| node0 | `amd140.utah.cloudlab.us` | `2026-07-18T14:44:32Z` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` |
| node1 | `amd136.utah.cloudlab.us` | `2026-07-18T14:44:32Z` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` |

实时 manifest、AM API v3 `status` 和 Portal 展示仍不一致：

```text
node0/node1 manifest: 2026-07-18T03:00:00Z
AM API v3 status:    2026-07-24 05:00:00
Portal experiment:   Jul 24, 2026 7:00 PM (timezone not shown)
```

manifest 原始 XML、采集时间、SHA 和来源差异诊断位于：

```text
physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/
```

负责人于 2026-07-18T15:17:01Z 明确确认实验已延期并采用以下政策：

- 对 **WP2 runtime 工作**，以实时 Portal 实验页和负责人确认作为有效租期依据；
- WP0.1 生效状态为 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2`；
- manifest 子检查仍为 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`，旧值和旧证据不得删除或改写；
- 精确 expiration UTC 仍为 `UNRESOLVED`，所以当前不计算或冻结 formal `T_expire`、`T_no_new_block`、`T_restore`；
- host tuning、RPC smoke、pilot、calibration 和 formal experiment 继续停止。

负责人确认和 Portal 截图证据位于：

```text
physical-results/gate-policy-revision-20260718T151701Z/
SHA256SUMS SHA256: 29e6f6fece0463c10bd97153cacb1f529c9d95f9e82e0d676684375e5cd8d918
```

## 4. 主机事实与计划布局

两机当前均为 Ubuntu 22.04.2、kernel `5.15.0-177-generic`、AMD EPYC 7302P；每机 16 个 physical cores / 32 个 logical CPUs，CPU `16-31` 分别是 CPU `0-15` 的 SMT siblings。实验 NIC 为 Mellanox `mlx5_core`。

当前只保留计划布局，没有 apply host profile：

- node0 worker CPUs：`0-15`，每个 physical core 只选一个 worker；
- node0 control/IRQ logical CPUs：`16-31`，它们是 worker cores 的 SMT siblings；
- node1 client/coordinator：CPU `0-7`；实验 IRQ：CPU `8-15`；CPU `16-31` 不运行实验线程；
- 旧文档中的 `0,2,4,...,30` worker 示例无效，因为会重复使用同一 physical core。

正式使用前必须由 topology validator 重新验证，并在 WP3 冻结实际 profile/hash。当前没有执行 topology freeze、sysctl/governor 变更或任何 host tuning。

## 5. S3 状态

2026-07-18T14:54:03Z 的非敏感 preflight 事实保持不变：

```yaml
rclone_config_preferred: ~/.config/rescuesched/rclone.conf
rclone_config_preferred_exists: false
rclone_config_default: ~/.config/rclone/rclone.conf
rclone_config_default_exists: false
discovered_rclone_configs: []
remote_name: null
bucket: null
prefix: null
test_object: null
local_source_sha256: null
remote_stream_sha256: null
downloaded_local_sha256: null
delete_confirmation: NOT_RUN
s3_gate_status: DEFERRED_UNTIL_PILOT
```

没有读取、打印、复制或提交 access key、secret key、session token；没有创建、下载或删除任何 S3 对象。原始 preflight 证据位于：

```text
physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.2/
```

本次调整只把 S3 从“WP2 前阻塞”改为“pilot/formal 前必须完成”。实际数据面闭环仍未通过。

## 6. WP1 本地基线与远端发布

2026-07-18T15:17:01Z 的负责人政策证据采集时：

```text
node0 HEAD: 50362f346c7fcdbbc064760e5670841eb5ac5db4, clean, branch codex/infocom2027-integration
node1 HEAD: 50362f346c7fcdbbc064760e5670841eb5ac5db4, clean, branch codex/infocom2027-integration
node0 tag object: 2fb914080bc63d357e38b8f4e21a6d5add34dc8e -> 0a88f03a21802be0eadc3065b93cb97876a6bd2f
node1 tag object: 2fb914080bc63d357e38b8f4e21a6d5add34dc8e -> 0a88f03a21802be0eadc3065b93cb97876a6bd2f
```

最近一次远端确认时间为 2026-07-18T15:03:28Z：

```text
refs/heads/codex/infocom2027-integration: ABSENT
refs/tags/physical-integration-v1: ABSENT
push performed: no
force push: no
remote URL modified: no
```

远端发布状态现为 `WAIVED_BY_OWNER`，含义是“不要求于 WP2 前完成”，而不是已经 push。若以后发布，仍只能普通非 force push，并验证远端 branch/tag；冻结 tag 不得移动、删除或重建。

## 7. WP2 代码级双机验证

WP2 源码提交：

```text
7db91095c4d0f84a5eb568b980748051f994c4cc
```

两台节点是在同一 clean 源码提交上**分别**完成代码级验证；没有运行 node0↔node1 RPC：

| 验证项 | node0 | node1 | 范围说明 |
| --- | --- | --- | --- |
| Release CTest | PASS 26/26 | PASS 26/26 | 全量当前测试集；冻结 WP1 tag 的历史值仍是 24/24 |
| ASan/UBSan | PASS 26/26 | PASS 26/26 | Debug sanitizer 全量测试 |
| TSan 核心 | PASS 2/2 | PASS 2/2 | `physical_runtime_validity`、`wp2_runtime_concurrency` |
| 本机 UDP gate | PASS | PASS | loopback `127.0.0.1:19184`；正式端口 `9000` 未使用 |
| 四方法 ingress mapping | PASS | PASS | 各节点内部 L0/L1/M0/M1 projection 完全一致；不要求跨节点 SHA 相同 |
| response queue failure injection | PASS_EXPECTED_FAIL_CLOSED | PASS_EXPECTED_FAIL_CLOSED | 212 次 enqueue failure 被显式报告，无 silent drop |
| synthetic stress | PASS 12/12 | PASS 12/12 | 3 repetitions × 4 policies；仅 in-process implementation smoke |

关键证据根：

```text
physical-results/wp2-runtime-20260718T153855Z/
physical-results/wp2-final-node0-20260718T163835Z/
physical-results/wp2-final-node1-20260718T164147Z/
physical-results/wp2-node1-sync-20260718T164112Z/
physical-results/wp2-node1-evidence-transfer-20260718T164352Z/
```

TSan 诊断历史没有删除：GCC 11/glibc 2.35 `pthread_cond_clockwait` 假阳性复现、一次异常生命周期测试挂起、中断快照、20/20 生命周期压力 PASS、sanitizer 重试 PASS 和最终两机 clean-commit PASS 均保留。误将 `--help` 作为 sanitizer build-dir 参数而产生的仓库根 `--help/` 目录也保留，并仅通过本地 `.git/info/exclude` 排除。

## 8. 已完成、证据保留与下一步

本次完成：

1. 在源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 完成 WP2.1–WP2.9 代码路径和新增单元/并发测试；
2. node0/node1 均在 clean/same commit 上完成 Release 26/26、ASan/UBSan 26/26、TSan 核心 2/2；
3. 两机分别完成本机四策略 UDP/mapping gate、response queue fail-closed 注入和 3×4 synthetic stress；
4. 使用 Git bundle over SCP 将 WP2 源码 fast-forward 同步到 node1，未使用 GitHub、未 force push；
5. 安全复制 node1 WP2 原始证据到 node0，并验证 archive SHA；node1 原始目录继续保留；
6. 未改写、移动、删除或重建 `physical-integration-v1`；所有旧 BLOCKED、失败 attempt、TSan 诊断和 coordinator bug 证据继续保留；
7. 没有执行 WP3、CPU/IRQ/NIC tuning、host profile apply、双机 RPC、pilot、calibration 或 formal experiment，正式端口 `9000` 未使用。

下一步：

1. 精确 lease expiration UTC 仍需以可审计来源冻结；manifest 子检查仍为 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`；
2. 进入 pilot/formal 前必须完成 S3 最小权限 upload/remote-SHA/download/local-SHA/delete 闭环；
3. WP3 及两机 RPC 需在外部门禁完成后另行明确授权，WP2 的代码级 PASS 不构成自动放行；
4. GitHub publication 保持 `WAIVED_BY_OWNER`；远端 refs 仍 `ABSENT`，没有 push。

## 9. 未冻结合同值

以下字段保持 `PENDING`/`null`，不得从 smoke 或仿真结果臆测：

- 精确 lease expiration UTC、`T_expire`、`T_no_new_block`、`T_restore`；
- scheduler/poll period；
- handoff estimate；
- 四个 anchor 的 physical arrival scale；
- decision logging 的代码默认已固定为 bounded / 100000 / 1000 µs，但包含这些值的 formal config SHA 尚未冻结；
- formal host-profile SHA；
- formal config SHA；
- S3 remote/bucket/prefix/object；
- 16-worker 与 12-worker fallback 的最终选择。
