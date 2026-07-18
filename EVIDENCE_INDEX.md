# RescueSched Evidence Index

> 索引版本：`evidence-index-v0.3`。最后更新：2026-07-18T16:59:01Z（UTC）。只登记真实存在的证据；`PENDING`/`null` 不代表 PASS。节点本地 `physical-results/` 被 Git 忽略，正式 archive 必须在 WP0 S3 门禁通过后登记 remote object。

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

## 3. 租期 manifest 完整性

历史 2026-07-17 复核：

```text
node0 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

最新 2026-07-18T14:44:32Z 实时复核：

```text
node0 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

完整原始 XML、补充状态源、Portal 截图与逐文件 SHA 清单：

```text
physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/
```

补充诊断：两机 AM API v3 `status` 均列出两个 ready/provisioned sliver，`geni_expires=2026-07-24 05:00:00`；用户 Portal 截图显示 `Jul 24, 2026 7:00 PM`，但截图不含时区。manifest、AM status 与 Portal 展示互不一致，详见 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/LEASE_SOURCE_DISCREPANCY.txt`。

2026-07-18T15:17:01Z，项目负责人正式确认采用实时 Portal 页面加负责人确认作为 **WP2-only** 的替代租期依据，证据见 `physical-results/gate-policy-revision-20260718T151701Z/`（`SHA256SUMS` SHA256 `29e6f6fece0463c10bd97153cacb1f529c9d95f9e82e0d676684375e5cd8d918`）。因此有效状态为 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2`；这不改变 manifest 子检查的 BLOCKED 事实，也没有把 2026-07-25 或精确 UTC 登记为已由 manifest 证明。

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

当前没有 formal block、formal trace、formal config SHA、host-profile SHA、archive SHA 或 S3 object，因此不得在索引中填入推测值。S3 的 `DEFERRED_UNTIL_PILOT` 不是数据面 PASS。
