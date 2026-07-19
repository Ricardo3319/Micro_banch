# RescueSched Evidence Index

> 索引版本：`evidence-index-v0.4`。最后更新：2026-07-19T08:33:00Z（UTC）。只登记真实存在的证据；`PENDING`/`null` 不代表 PASS。节点本地 `physical-results/` 被 Git 忽略，正式 archive 必须在 WP0 S3 门禁通过后登记 remote object。

## 1. 字段定义

| 字段 | 含义 |
| --- | --- |
| `evidence_id` | 不变的证据记录 ID |
| `claim_id` | 研究或工程主张 ID |
| `paper_location` | 论文位置；未分配为 `PENDING` |
| `figure_or_table` | 图表；未分配为 `PENDING` |
| `analysis_output` | 分析输出或 gate report |
| `block_id` / `attempt_id` / `method` | 正式矩阵定位；非正式证据为 `N/A` |
| `trace_sha256` | exact trace bytes SHA；无 trace 为 `N/A` |
| `config_sha256` | formal config snapshot SHA；未冻结为 `null` |
| `commit_sha` | 生成证据的 Git commit；`SELF` 表示包含本索引的提交 |
| `host_profile_sha256` | host profile SHA；未 apply 为 `null` |
| `archive_sha256` | immutable archive SHA；未归档为 `null` |
| `s3_object_key` | private remote object；未上传为 `null` |
| `validation_status` | `PASS`/`BLOCKED`/`PENDING`/`DEFERRED`/`WAIVED`/限定范围 PASS/正式分类 |

正式结果必须能够从 claim/figure/table 追溯到 CSV 行、block/attempt、trace/config/commit/profile/archive SHA 和 S3 object。失败 attempts 与有效 attempts 同等级索引。

## 2. 当前记录

| evidence_id | claim_id | paper_location | figure_or_table | analysis_output | block_id | attempt_id | method | trace_sha256 | config_sha256 | commit_sha | host_profile_sha256 | archive_sha256 | s3_object_key | validation_status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `E-WP0-LEASE-20260717-01` | `GATE-LEASE` | N/A | N/A | `physical-results/wp0-final-review-20260717T163447Z/SUMMARY.txt` | N/A | N/A | N/A | N/A | N/A | N/A | N/A | null | null | **BLOCKED**：两机 manifest 为 `2026-07-18T03:00:00Z` |
| `E-WP0-S3-01` | `GATE-REMOTE-RECOVERY` | N/A | N/A | null | N/A | N/A | N/A | N/A | N/A | N/A | N/A | null | null | **BLOCKED**：仓库外 rclone 配置和 bucket/prefix 缺失 |
| `E-WP1-INTEGRATION-01` | `GATE-INTEGRATION-ASSETS` | N/A | N/A | repository tree | N/A | N/A | N/A | N/A | N/A | `SELF` | null | null | null | PASS：simulator/paper/corrected artifacts 保留，物理实现按 allowlist 集成 |
| `E-WP1-BUILD-NODE0-01` | `GATE-RELEASE-CTEST` | N/A | N/A | `physical-results/wp1-final-v3-physical-integration-v1-node0/ctest.log` | N/A | N/A | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | PASS：最终 tag commit 上 Release 24/24 CTest |
| `E-WP1-BUILD-NODE1-01` | `GATE-RELEASE-CTEST` | N/A | N/A | `physical-results/wp1-final-v3-physical-integration-v1-node1/ctest.log` | N/A | N/A | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | PASS：最终 tag commit 上 Release 24/24 CTest |
| `E-WP1-IDENTITY-01` | `GATE-SAME-COMMIT-CLEAN` | N/A | N/A | `physical-results/wp1-final-v3-identity/IDENTITY_STATUS.txt` | N/A | N/A | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | PASS：两机 same full commit、clean、24/24 tests；使用 bundle 部署 |
| `E-WP1-FAIL-CLOSED-01` | `GATE-COORDINATOR-FAIL-CLOSED` | N/A | N/A | `physical-results/wp1-final-v3-identity/NEGATIVE_GATE_STATUS.txt` | N/A | N/A | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | PASS：load-generator dirty、server dirty、server commit mismatch 均在创建输出/RPC 工作前拒绝 |
| `E-WP0-LEASE-20260718-02` | `GATE-LEASE` | N/A | N/A | `physical-results/wp0-wp1-gates-20260718T035213Z/wp0.1/GATE_STATUS.txt` | N/A | `20260718T035213Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | **BLOCKED**：2026-07-18T03:52:13Z 两机 uncached manifest 仍一致为 `2026-07-18T03:00:00Z` |
| `E-WP0-S3-20260718-02` | `GATE-REMOTE-RECOVERY` | N/A | N/A | `physical-results/wp0-wp1-gates-20260718T035213Z/wp0.2/GATE_STATUS.txt` | N/A | `20260718T035213Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | **BLOCKED**：rclone 配置、remote、private bucket/project prefix 缺失；upload/SHA/download/delete 全部 `NOT_RUN` |
| `E-WP1-IDENTITY-20260718-02` | `GATE-SAME-COMMIT-CLEAN` | N/A | N/A | `physical-results/wp0-wp1-gates-20260718T035213Z/wp1/IDENTITY_STATUS.txt` | N/A | `20260718T035213Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | PASS：认证探测前两机 clean、same baseline commit，annotated tag 均指向冻结提交 |
| `E-WP1-REMOTE-PUBLICATION-20260718-01` | `GATE-REMOTE-PUBLICATION` | N/A | N/A | `physical-results/wp0-wp1-gates-20260718T035213Z/wp1/REMOTE_PUBLICATION_STATUS.txt` | N/A | `20260718T035213Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | **BLOCKED_CREDENTIALS**：node0/node1 HTTPS 认证不可用；远端 branch/tag 均 `ABSENT`；没有 push 或 force push |
| `E-WP0-LEASE-20260718-03` | `GATE-LEASE` | N/A | N/A | `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/GATE_STATUS.txt`；`physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/LEASE_SOURCE_DISCREPANCY.txt` | N/A | `20260718T144404Z` | N/A | N/A | N/A | `09de5b5ecca4987bd04ab44fe4a65461a24625fe` | null | null | null | **BLOCKED**：2026-07-18T14:44:32Z 两机实时 manifest 仍为 `2026-07-18T03:00:00Z`；AM status 为 `2026-07-24 05:00:00`，Portal 截图为 `Jul 24, 2026 7:00 PM`（时区未显示），来源不一致 |
| `E-WP0-S3-20260718-03` | `GATE-REMOTE-RECOVERY` | N/A | N/A | `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.2/GATE_STATUS.txt` | N/A | `20260718T144404Z` | N/A | N/A | N/A | `09de5b5ecca4987bd04ab44fe4a65461a24625fe` | null | null | null | **BLOCKED**：两机仓库外 rclone 配置、remote、private bucket/project prefix 缺失；对象创建/upload/SHA/download/delete 全部 `NOT_RUN` |
| `E-WP1-IDENTITY-20260718-03` | `GATE-SAME-COMMIT-CLEAN` | N/A | N/A | `physical-results/wp0-wp1-unblock-20260718T144404Z/wp1/PRECHECK_STATUS.txt` | N/A | `20260718T144404Z` | N/A | N/A | N/A | `09de5b5ecca4987bd04ab44fe4a65461a24625fe` | null | null | null | PASS：2026-07-18T14:44:05Z 两机 clean/same current status commit；annotated tag 均仍指向冻结 `0a88f03a21802be0eadc3065b93cb97876a6bd2f` |
| `E-WP1-REMOTE-PUBLICATION-20260718-02` | `GATE-REMOTE-PUBLICATION` | N/A | N/A | `physical-results/wp0-wp1-unblock-20260718T144404Z/wp1/REMOTE_PUBLICATION_STATUS.txt` | N/A | `20260718T144404Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | **BLOCKED_CREDENTIALS**：node0/node1 HTTPS 认证不可用；远端 branch/tag 均 `ABSENT`；push `NOT_RUN`，无 force push 或 remote URL 修改 |
| `E-GATE-POLICY-20260718-01` | `GATE-POLICY-REVISION` | N/A | N/A | `physical-results/gate-policy-revision-20260718T151701Z/OWNER_DECISION.txt`；`GATE_POLICY_STATUS.txt`；`validation/VALIDATION_STATUS.txt` | N/A | `20260718T151701Z` | N/A | N/A | N/A | `50362f346c7fcdbbc064760e5670841eb5ac5db4` | null | null | null | PASS：负责人于 2026-07-18T15:17:01Z 明确确认采用 WP2 前置门禁调整；范围和禁止事项已审计记录 |
| `E-WP0-LEASE-20260718-04` | `GATE-LEASE` | N/A | N/A | `physical-results/gate-policy-revision-20260718T151701Z/GATE_POLICY_STATUS.txt`；`owner-portal-expiration-screenshot.png` | N/A | `20260718T151701Z` | N/A | N/A | N/A | `50362f346c7fcdbbc064760e5670841eb5ac5db4` | null | null | null | **PASS_OWNER_ATTESTED_PORTAL_FOR_WP2**：Portal 显示 ready/`Jul 24, 2026 7:00 PM` 且负责人确认延期；manifest 子检查仍 BLOCKED，精确 UTC 未冻结 |
| `E-WP0-S3-20260718-04` | `GATE-REMOTE-RECOVERY` | N/A | N/A | `physical-results/gate-policy-revision-20260718T151701Z/GATE_POLICY_STATUS.txt` | N/A | `20260718T151701Z` | N/A | N/A | N/A | `50362f346c7fcdbbc064760e5670841eb5ac5db4` | null | null | null | **DEFERRED_UNTIL_PILOT**：不阻塞 WP2；配置仍缺失，对象操作和三段 SHA/delete 全部 `NOT_RUN`，pilot/formal 前必须完成 |
| `E-WP1-REMOTE-PUBLICATION-20260718-03` | `GATE-REMOTE-PUBLICATION` | N/A | N/A | `physical-results/gate-policy-revision-20260718T151701Z/GATE_POLICY_STATUS.txt` | N/A | `20260718T151701Z` | N/A | N/A | N/A | `0a88f03a21802be0eadc3065b93cb97876a6bd2f` | null | null | null | **WAIVED_BY_OWNER**：不阻塞 WP2；远端 branch/tag 实际仍 `ABSENT`，没有 push，冻结 tag 未移动 |
| `E-WP2-SOURCE-AUDIT-20260718-01` | `GATE-WP2-SOURCE-AND-FOCUSED-TESTS` | N/A | N/A | `physical-results/wp2-runtime-20260718T153855Z/tests/source-audit-20260718T163518Z/STATUS.txt`；`audit.log`；`manifest.env` | N/A | `20260718T163518Z` | N/A | N/A | N/A | `6d8acc7d85d446bc53244562ec46fdd3c182aad2` | null | null | null | **PASS_CODE_LEVEL_PRE_COMMIT_WORKTREE**：采集时 HEAD 为所列提交且 worktree 含 WP2 修改；`git diff --check`、三个 shell `bash -n`、focused CTest 3/3 PASS。该 exact worktree 随后提交为 `7db91095c4d0f84a5eb568b980748051f994c4cc`，并由两机 clean-commit 最终证据复验；`shellcheck=UNAVAILABLE`，正式端口/host tuning/双机 RPC 均未使用 |
| `E-WP2-NODE0-FINAL-20260718-01` | `GATE-WP2-CODE-LEVEL-NODE0` | N/A | N/A | `physical-results/wp2-final-node0-20260718T163835Z/FINAL_STATUS.txt`；`release/full.log`；`sanitizers/`；`rpc/`；`synthetic/` | N/A | `20260718T163835Z` | N/A | N/A | N/A | `7db91095c4d0f84a5eb568b980748051f994c4cc` | null | null | null | **PASS_CODE_LEVEL**：clean node0；Release 26/26、ASan/UBSan 26/26、TSan 2/2、loopback UDP/mapping/fail-closed PASS、synthetic 12/12；port 9000 未使用 |
| `E-WP2-NODE1-FINAL-20260718-01` | `GATE-WP2-CODE-LEVEL-NODE1` | N/A | N/A | `physical-results/wp2-final-node1-20260718T164147Z/FINAL_STATUS.txt`；`release/full.log`；`sanitizers/`；`rpc/`；`synthetic/` | N/A | `20260718T164147Z` | N/A | N/A | N/A | `7db91095c4d0f84a5eb568b980748051f994c4cc` | null | null | null | **PASS_CODE_LEVEL**：clean node1；Release 26/26、ASan/UBSan 26/26、TSan 2/2、loopback UDP/mapping/fail-closed PASS、synthetic 12/12；port 9000 未使用 |
| `E-WP2-TSAN-DIAGNOSTICS-20260718-01` | `GATE-WP2-TSAN-LIMITS-AND-REGRESSION` | N/A | N/A | `physical-results/wp2-runtime-20260718T153855Z/tests/tsan-multi-cv-probe-20260718T161731Z/`；`tsan-multi-cv-system-clock-probe-20260718T161755Z/`；`node0-tsan-full-hang-20260718T162606Z/`；`tsan-lifecycle-stress-20260718T162730Z/`；`node0-sanitizers-final-retry-20260718T162829Z.log` | N/A | `20260718T161731Z` | N/A | N/A | N/A | `7db91095c4d0f84a5eb568b980748051f994c4cc` | null | null | null | **PASS_WITH_PRESERVED_DIAGNOSTICS**：保留 `pthread_cond_clockwait` 假阳性复现和一次异常挂起；CLOCK_REALTIME 对照、20/20 生命周期压力、重试及最终两机 clean-commit TSan 均 PASS |
| `E-WP2-BUNDLE-SYNC-20260718-01` | `GATE-WP2-SAME-COMMIT-DEPLOYMENT` | N/A | N/A | `physical-results/wp2-node1-sync-20260718T164112Z/STATUS.txt`；`bundle-verify.log`；`node1-fast-forward.log` | N/A | `20260718T164112Z` | N/A | N/A | N/A | `7db91095c4d0f84a5eb568b980748051f994c4cc` | null | null | null | PASS：Git bundle over SCP，node1 fast-forward 到源码提交并 clean；GitHub 未使用、无 force push、冻结 tag 未移动 |
| `E-WP2-NODE1-EVIDENCE-TRANSFER-20260718-01` | `GATE-WP2-EVIDENCE-PRESERVATION` | N/A | N/A | `physical-results/wp2-node1-evidence-transfer-20260718T164352Z/STATUS.txt`；`copied-evidence-verify.log` | N/A | `20260718T164352Z` | N/A | N/A | N/A | `7db91095c4d0f84a5eb568b980748051f994c4cc` | null | `30eea36dc89fbc78e1d4dfb6305bd16e51d8f27590a9b8108ed94b34ebd4ec2c` | null | PASS：node1 证据 archive SHA 一致，复制后逐文件校验 PASS，node1 原始证据保留 |

| `E-WP0-LEASE-20260719-05` | `GATE-LEASE-EXACT-UTC-WP3` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/wp0.1/GATE_STATUS.env`；`LEASE_DECISION.txt`；`authority-analysis/` | N/A | `20260719T070542Z` | N/A | N/A | N/A | `08b12f60e94ed585e86744997a1077373c70b743` | N/A | null | null | **PASS_EXACT_UTC_FROZEN_FOR_WP3**：两机 manifest 子检查仍为 stale/BLOCKED；两机 live AM status 原始值一致，结合 deployed Utah site `America/Denver`/`-0600` 语义冻结 `T_expire=2026-07-24T11:00:00Z`、`T_no_new_block=2026-07-23T23:00:00Z`；仅授权 WP3 |
| `E-WP0-S3-20260719-05` | `GATE-REMOTE-RECOVERY` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/wp0.2/GATE_STATUS.env`；`MISSING_INPUTS.txt` | N/A | `20260719T070542Z` | N/A | N/A | N/A | `08b12f60e94ed585e86744997a1077373c70b743` | N/A | null | null | **DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS**：两机 repo-external config 不存在，remote/private bucket/project prefix 缺失；未读取 credential，全部对象操作 `NOT_RUN`；不阻塞 WP3，继续阻塞 WP4/pilot/formal |
| `E-WP3-TOOLING-20260719-01` | `GATE-WP3-RECOVERABLE-TOOLING` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/wp3/tooling-validation-precommit-20260719T075917Z/`；`tooling-sync-20260719T080032Z/`；`tooling-sync-dryrun-order-20260719T080145Z/` | N/A | `20260719T075917Z` | N/A | N/A | N/A | `90c5ff6d774cea0e06563e92681570727b079933` | node0=`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`; node1=`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e` | null | null | PASS：capture/validator/dry-run/restore-plan/apply/verify/independent restore 入口已提交；事务顺序修正为 dry-run 早于 final restore plan；bundle over management SCP 同步，GitHub 未使用；Release 26/26，shellcheck `UNAVAILABLE` |
| `E-WP3-NODE0-20260719-01` | `GATE-WP3-NODE0-PROFILE-RESTORE` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/wp3/node0/attempt-20260719T080242Z/RESULT.env`；`before/`；`apply/`；`restore-1/`；`restore-2/` | N/A | `20260719T080242Z` | N/A | N/A | N/A | `90c5ff6d774cea0e06563e92681570727b079933` | `50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b` | null | null | **PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION**：topology PASS；effective 89/89；management SSH PASS；restore PASS；repeat restore `PASS_ALREADY_RESTORED` mutation 0；before/restored、idempotency、control diff 均 0 bytes；workload/port 9000/ring 4096 未使用 |
| `E-WP3-NODE1-20260719-01` | `GATE-WP3-NODE1-PROFILE-RESTORE` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/wp3/node1/attempt-20260719T080344Z/RESULT.env`；`before/`；`apply/`；`restore-1/`；`restore-2/` | N/A | `20260719T080344Z` | N/A | N/A | N/A | `90c5ff6d774cea0e06563e92681570727b079933` | `fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e` | null | null | **PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION**：topology PASS，cpufreq policy absent 如实记录；effective 89/89；management SSH PASS；restore PASS；repeat restore `PASS_ALREADY_RESTORED` mutation 0；三类 restored diff 均 0 bytes；workload/port 9000/ring 4096 未使用 |
| `E-WP3-FINAL-VALIDATION-20260719-01` | `GATE-WP3-TRACKED-FILES-AND-RELEASE` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/final-validation-20260719T082516Z/STATUS.env`；`node0/release/`；`node1/release/`；`failed-attempt-*` | N/A | `20260719T082516Z` | N/A | N/A | N/A | `90c5ff6d774cea0e06563e92681570727b079933` | node0=`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`; node1=`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e` | null | null | PASS：git diff check、bash syntax、Python compile、YAML parse/frozen-field assertions、tracked credential scan；两机 Release 26/26；shellcheck `UNAVAILABLE`。外层 wrapper exit discrepancy 与一次 wrong-cwd SHA readback 均作为失败调用保留，正确 cwd 复核和 node1 证据复制 SHA PASS |
| `E-WP3-POSTDOCS-VALIDATION-20260719-01` | `GATE-WP3-FINAL-TRACKED-DOCS` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/final-validation-precommit-20260719T083324Z/STATUS.env`；`physical-results/wp0-wp3-20260719T070542Z/final-validation-postdocs-20260719T083051Z/FAILURE_CLASSIFICATION.txt` | N/A | `20260719T083147Z` | N/A | N/A | N/A | `SELF` | node0=`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`; node1=`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e` | null | null | **PASS_WITH_PRESERVED_FAILED_VALIDATOR_INVOCATION**：最终文档后 `git diff --check`、YAML exact lease/WP3/WP4-stop assertions、tracked credential-value scan PASS；首次只读 validator 使用错误顶层 key 而失败，未修改 tracked 文件，失败目录保留并在新目录校正复核 |
| `E-WP3-FINAL-IDENTITY-20260719-01` | `GATE-WP3-FINAL-SAME-COMMIT-CLEAN-TAG` | N/A | N/A | `physical-results/wp0-wp3-20260719T070542Z/final-identity/STATUS.env`；`node0/git_identity.raw`；`node1/git_identity.raw` | N/A | `20260719-final` | N/A | N/A | N/A | `SELF` | node0=`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`; node1=`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e` | null | null | PASS：最终两机 branch/commit 相同且 clean；annotated tag object/target 未变化；GitHub push 未执行；WP4/RPC/pilot/calibration/formal/9000 均未执行 |

## 3. 租期 manifest 完整性

历史 2026-07-17、2026-07-18 的失败/BLOCKED 记录继续保留，不被新判定覆盖。它们的两机 manifest SHA 与 2026-07-19 实时复核相同：

```text
node0 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
manifest_subcheck=BLOCKED_STALE_OR_NON_PROPAGATED_VALUE
```

2026-07-19 本轮没有使用旧缓存推断租期。两机分别实时执行的 `geni-get -n status` 均给出两个 sliver 的原始 `geni_expires=2026-07-24 05:00:00`，状态输出 SHA256 均为：

```text
1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12
```

实时 Utah CloudLab 官方站点响应给出 `-0600`；保存的 exact deployed source `0b1fdb15cd434591f3fab98799d78189698686fc` 与站点配置证明原始字段使用 `America/Denver` 站点时间。因此本轮冻结：

```text
raw=2026-07-24 05:00:00 America/Denver (MDT, -0600)
T_expire=2026-07-24T11:00:00Z
T_no_new_block=2026-07-23T23:00:00Z
status=PASS_EXACT_UTC_FROZEN_FOR_WP3
```

完整原始 XML、开始/结束 UTC、hostname、exit code、补充 status、部署源码、时区解释、逐文件 SHA 和所有失败 attempts：

```text
physical-results/wp0-wp3-20260719T070542Z/wp0.1/
```

该 PASS 只解除 WP3 host profile apply/restore。旧 Portal 截图不含时区，仍作为历史补充事实保留；它没有被改写成精确 UTC authority。

## 4. 正式证据模板（WP2 代码级证据不属于 formal 记录）

新增正式 row 时使用：

```text
evidence_id:
claim_id:
paper_location:
figure_or_table:
analysis_output:
block_id:
attempt_id:
method:
trace_sha256:
config_sha256:
commit_sha:
host_profile_sha256:
archive_sha256:
s3_object_key:
validation_status:
```

当前没有 formal block、formal trace、formal config SHA、formal archive SHA 或 S3 object。WP3 的两机 host-profile SHA 已真实冻结并用于 apply/restore regression，但尚不是 WP4/formal config SHA。S3 的 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS` 不是数据面 PASS。
