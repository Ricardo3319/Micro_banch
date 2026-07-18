# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-18T15:25:50Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP0.1 租期证据 | **PASS_OWNER_ATTESTED_PORTAL_FOR_WP2** | 项目负责人于 2026-07-18T15:17:01Z 确认采用实时 Portal 实验页作为 WP2 前置租期依据。页面显示实验 `ready`、`Jul 24, 2026 7:00 PM`；两机 manifest 子检查仍显示旧值 `2026-07-18T03:00:00Z`，保持 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`，不得描述为 manifest 已证明 2026-07-25。Portal 未显示时区，精确 expiration UTC 仍未冻结。 |
| WP0.2 S3 闭环 | **DEFERRED_UNTIL_PILOT** | 负责人明确同意 WP2 前暂不执行 S3 闭环。rclone 配置、remote、private bucket/project prefix 仍缺失，upload、remote SHA、download、local SHA 和 delete 全部 `NOT_RUN`；进入 pilot 或 formal 前仍必须完成，不得写成 PASS。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUBLICATION_WAIVED_BY_OWNER** | 冻结基线 `0a88f03a21802be0eadc3065b93cb97876a6bd2f` 的两机 Release 24/24 CTest 证据保持有效；2026-07-18T15:17:01Z 两机均 clean/same commit `50362f346c7fcdbbc064760e5670841eb5ac5db4`，annotated tag 未移动。GitHub 远端 branch/tag 实际仍为 `ABSENT`，没有 push；负责人同意该发布不再阻塞 WP2。 |
| WP2 runtime | **READY_TO_START / NOT_STARTED** | 门禁调整只解除 WP2 runtime 源码修复和代码级测试的前置阻塞；截至本状态时间尚未执行 WP2 修改或测试。 |
| WP3 及以后 | **BLOCKED** | 不允许 CPU/IRQ/NIC tuning、host profile apply、两机 RPC smoke、pilot、calibration 或 formal experiment；不得使用正式端口或正式结果目录。 |

**当前允许事项：** 开始 WP2 runtime 正确性修复和代码级测试。

**当前禁止事项：** host tuning、两机 RPC smoke、pilot、calibration 和 formal paired block。进入这些阶段前必须冻结可审计的精确 lease UTC，并按相应门禁完成 S3 最小权限恢复闭环。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
frozen_integration_commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
current_status_commit: SELF
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

## 7. 已完成、证据保留与下一步

本次完成：

1. 记录项目负责人在 2026-07-18T15:17:01Z 对 WP0.1/WP0.2/WP1 门禁调整的明确确认；
2. 保存新的唯一证据目录 `physical-results/gate-policy-revision-20260718T151701Z/`，包含 Portal 截图、负责人决定、门禁范围、两机 Git identity 和 SHA256 清单；
3. 将 WP0.1 调整为仅对 WP2 生效的 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2`，同时保留 manifest 子检查失败事实；
4. 将 WP0.2 调整为 `DEFERRED_UNTIL_PILOT`，没有虚构任何 S3 数据面 PASS；
5. 将 WP1 GitHub publication 调整为 `WAIVED_BY_OWNER`，保留远端 refs `ABSENT` 和未 push 的事实；
6. 2026-07-18T15:25:03Z 至 2026-07-18T15:25:04Z，两机现有 Release build 各自再次完成 24/24 CTest；
7. 未改写、移动、删除或重建 `physical-integration-v1`；所有旧 BLOCKED、失败 attempt 和 coordinator bug 证据继续保留。

下一步：

1. 可以开始 WP2 runtime 正确性代码工作和代码级测试；
2. WP2 完成前后均不得顺带执行 host tuning 或两机 RPC smoke；
3. 进入 pilot/formal 前，必须完成 S3 最小权限闭环并冻结可审计的精确 lease UTC；
4. GitHub publication 保持可选，若执行必须有认证且禁止 force push。

## 8. 未冻结合同值

以下字段保持 `PENDING`/`null`，不得从 smoke 或仿真结果臆测：

- 精确 lease expiration UTC、`T_expire`、`T_no_new_block`、`T_restore`；
- scheduler/poll period；
- handoff estimate；
- 四个 anchor 的 physical arrival scale；
- decision logging mode/cap/bucket；
- formal host-profile SHA；
- formal config SHA；
- S3 remote/bucket/prefix/object；
- 16-worker 与 12-worker fallback 的最终选择。
