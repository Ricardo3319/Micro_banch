# RescueSched Evidence Index

> 索引版本：`evidence-index-v0.1`。只登记真实存在的证据；`PENDING`/`null` 不代表 PASS。节点本地 `physical-results/` 被 Git 忽略，正式 archive 必须在 WP0 S3 门禁通过后登记 remote object。

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
| `E-WP0-LEASE-20260717-01` | `GATE-LEASE` | N/A | N/A | `physical-results/wp0-lease-20260717T161011Z/SUMMARY.txt` | N/A | N/A | N/A | N/A | N/A | N/A | N/A | null | null | **BLOCKED**：两机 manifest 为 `2026-07-18T03:00:00Z` |
| `E-WP0-S3-01` | `GATE-REMOTE-RECOVERY` | N/A | N/A | null | N/A | N/A | N/A | N/A | N/A | N/A | N/A | null | null | **BLOCKED**：仓库外 rclone 配置和 bucket/prefix 缺失 |
| `E-WP1-INTEGRATION-01` | `GATE-INTEGRATION-ASSETS` | N/A | N/A | repository tree | N/A | N/A | N/A | N/A | N/A | `SELF` | null | null | null | PASS：simulator/paper/corrected artifacts 保留，物理实现按 allowlist 集成 |
| `E-WP1-BUILD-NODE0-01` | `GATE-RELEASE-CTEST` | N/A | N/A | `physical-results/wp1-build-<UTC>-amd140/ctest.log` | N/A | N/A | N/A | N/A | N/A | `SELF` | null | null | null | PENDING：最终 tagged commit 上重跑 |
| `E-WP1-BUILD-NODE1-01` | `GATE-RELEASE-CTEST` | N/A | N/A | `physical-results/wp1-build-<UTC>-amd136/ctest.log` | N/A | N/A | N/A | N/A | N/A | `SELF` | null | null | null | PENDING：最终 tagged commit 上重跑 |
| `E-WP1-IDENTITY-01` | `GATE-SAME-COMMIT-CLEAN` | N/A | N/A | `physical-results/wp1-identity-<UTC>/IDENTITY_STATUS.txt` | N/A | N/A | N/A | N/A | N/A | `SELF` | null | null | null | PENDING |

租期原始 XML 的 SHA256：

```text
node0 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
node1 ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

## 3. 正式证据模板（尚无记录）

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
