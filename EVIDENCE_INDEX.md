# RescueSched Evidence Index

> 索引版本：`evidence-index-v0.1`。最后更新：2026-07-18T03:55:59Z（UTC）。只登记真实存在的证据；`PENDING`/`null` 不代表 PASS。节点本地 `physical-results/` 被 Git 忽略，正式 archive 必须在 WP0 S3 门禁通过后登记 remote object。

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
| `validation_status` | `PASS`/`BLOCKED`/`PENDING`/正式分类 |

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

## 3. 租期 manifest 完整性

历史 2026-07-17 复核：

```text
node0 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

最新 2026-07-18T03:52:13Z 实时复核：

```text
node0 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 expiration=2026-07-18T03:00:00Z sha256=ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

完整原始 XML 与逐文件 SHA 清单：

```text
physical-results/wp0-wp1-gates-20260718T035213Z/wp0.1/
```

## 4. 正式证据模板（尚无记录）

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

当前没有 formal block、formal trace、formal config SHA、host-profile SHA、archive SHA 或 S3 object，因此不得在索引中填入推测值。
