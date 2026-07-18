# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-18T03:55:59Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP0.1 租期证据 | **BLOCKED** | 2026-07-18T03:52:13Z 在两机实时执行 `geni-get -n manifest`；两份 manifest 仍一致显示 `2026-07-18T03:00:00Z`，没有机器侧证明租期已延期到 2026-07-25。Portal 截图/负责人确认不能替代此门禁。 |
| WP0.2 S3 闭环 | **BLOCKED** | 首选及默认仓库外 `rclone.conf` 均不存在，未发现 configured remote，private bucket/项目专用 prefix 也未提供；upload、remote SHA、download、local SHA 和 delete 均未执行。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUSH_BLOCKED_CREDENTIALS** | 冻结基线 `0a88f03a21802be0eadc3065b93cb97876a6bd2f`、两机 Release + 24/24 CTest、clean/same-commit 和本地 annotated tag 均保持有效；node0/node1 的 GitHub HTTPS 认证均不可用，远端 branch/tag 实际均为 `ABSENT`，没有执行 push。 |
| WP2 及以后 | **NOT_STARTED** | 本次未执行 WP2、CPU/IRQ/NIC tuning、RPC smoke、pilot、calibration 或 formal experiment。 |

**当前禁止事项：** WP0 未通过时，不得开始 WP2、host tuning、pilot、calibration 或 formal paired block；不得把 2026-07-25 写成已由 manifest 证明的租期。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
frozen_integration_commit: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
current_status_commit: SELF  # 状态文档若形成后续提交，不改变冻结 tag 的含义
release_tag: physical-integration-v1
release_tag_target: 0a88f03a21802be0eadc3065b93cb97876a6bd2f
release_tag_kind: annotated
remote_branch_actual: ABSENT
remote_tag_actual: ABSENT
remote_publication: BLOCKED_CREDENTIALS
```

选择性集成保留了以下历史研究资产：

- `src/core/simulator.cpp`
- `paper/main.tex`
- `artifacts/step-21-corrected-full/manifest.md`
- `scripts/corrected_eval_analysis.py`

物理裁剪分支中会破坏完整 simulator 的公共 `constants.h` / `types.h` 简化版本未导入。物理 runtime、UDP RPC、trace generator、测试和辅助脚本按 allowlist 集成，公共构建文件手工合并。

## 3. 两机与最新租期证据

| 节点 | 角色 | hostname | 实验地址 | 实验 NIC | 实时采集 UTC | manifest expiration | manifest SHA256 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| node0 | server/main experiment | `amd140.utah.cloudlab.us` | `10.10.1.1` | `enp65s0f0np0` / `mlx5_core` | `2026-07-18T03:52:13Z` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` |
| node1 | load generator/coordinator | `amd136.utah.cloudlab.us` | `10.10.1.2` | `enp65s0f0np0` / `mlx5_core` | `2026-07-18T03:52:13Z` | `2026-07-18T03:00:00Z` | `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8` |

本轮命令均为实时 `geni-get -n manifest`，没有用旧缓存或旧证据推断当前租期。完整原始 XML、命令、exit code、UTC 时间和 SHA 清单位于：

```text
physical-results/wp0-wp1-gates-20260718T035213Z/wp0.1/
```

按 manifest 中较早值计算：

```text
T_expire       = 2026-07-18T03:00:00Z  # 在本轮采集时已经过去
T_no_new_block = 2026-07-17T15:00:00Z
T_restore      = 2026-07-17T21:00:00Z
```

机器当前仍可访问不等于租期延期已由 manifest 证明。只有两机新的 uncached manifest 一致明确显示 2026-07-25，WP0.1 才能改为 `PASS`。

## 4. 主机事实与计划布局

两机当前均为 Ubuntu 22.04.2、kernel `5.15.0-177-generic`、AMD EPYC 7302P；每机 16 个 physical cores / 32 个 logical CPUs，CPU `16-31` 分别是 CPU `0-15` 的 SMT siblings。实验 NIC 为 Mellanox `mlx5_core`。

WP1 只记录**计划布局**，没有 apply host profile：

- node0 worker CPUs：`0-15`，每个 physical core 只选一个 worker；
- node0 control/IRQ logical CPUs：`16-31`，它们是 worker cores 的 SMT siblings；
- node1 client/coordinator：CPU `0-7`；实验 IRQ：CPU `8-15`；CPU `16-31` 不运行实验线程；
- 旧文档中的 `0,2,4,...,30` worker 示例无效，因为会重复使用同一 physical core。

正式使用前必须由 topology validator 重新验证，并在 WP3 冻结实际 profile/hash。本轮没有执行 topology freeze 或任何 host tuning。

## 5. 依赖与工具

两机已安装：

```text
git build-essential cmake ninja-build python3 python3-venv python3-pip
numactl hwloc ethtool iproute2 linux-tools-common linux-tools-5.15.0-177-generic
jq zstd rclone tmux
```

关键版本：CMake 3.22.1、Ninja 1.10.1、Python 3.10.12、jq 1.6、rclone 1.53.3、perf 5.15.199、hwloc 2.7.0。

分析环境入口：`.venv-analysis/`（Git 忽略）；锁文件：`requirements/analysis-lock.txt`。

## 6. S3 状态

2026-07-18T03:52:55Z 至 03:53:15Z 的非敏感 preflight 结果：

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
s3_gate_status: BLOCKED
```

缺失的非敏感输入：

1. 仓库外、owner 为当前用户且权限为 `0600` 的 rclone 配置；
2. configured rclone remote 名称；
3. private bucket 与项目专用 prefix。

没有读取、打印、复制或提交 access key、secret key、session token；没有创建或删除任何 S3 对象。脱敏证据位于：

```text
physical-results/wp0-wp1-gates-20260718T035213Z/wp0.2/
```

## 7. WP1 远端发布状态

2026-07-18T03:53:53Z 在 push 认证探测前复核：

```text
node0 HEAD: 0a88f03a21802be0eadc3065b93cb97876a6bd2f, clean, branch codex/infocom2027-integration
node1 HEAD: 0a88f03a21802be0eadc3065b93cb97876a6bd2f, clean, branch codex/infocom2027-integration
node0 physical-integration-v1: annotated tag -> 0a88f03a21802be0eadc3065b93cb97876a6bd2f
node1 physical-integration-v1: annotated tag -> 0a88f03a21802be0eadc3065b93cb97876a6bd2f
```

node0 与 node1 的非交互 `git push --dry-run` 均在没有输出 credential 的情况下以 `could not read Username ... terminal prompts disabled` 失败。2026-07-18T03:55:59Z 再次 `git ls-remote` 的实际结果：

```text
refs/heads/codex/infocom2027-integration: ABSENT
refs/tags/physical-integration-v1: ABSENT
```

因此远端发布为 `BLOCKED_CREDENTIALS`：没有 push、没有 force push、没有修改 remote URL。证据位于：

```text
physical-results/wp0-wp1-gates-20260718T035213Z/wp1/
```

## 8. 已完成、保留证据与下一步

最近完成：

1. 再次实时采集两机 uncached manifest，并按机器侧事实保持 WP0.1 `BLOCKED`。
2. 在不访问 secret 的前提下完成 S3 preflight；因配置与非敏感 routing inputs 缺失保持 WP0.2 `BLOCKED`。
3. 再次证明冻结基线时两机 same full commit、clean，且 annotated tag 未改写。
4. 对两台机器分别执行安全的 GitHub 认证 dry-run；认证不可用，远端 refs 为空，没有冒充 push 成功。
5. 本轮新证据统一保存在 `physical-results/wp0-wp1-gates-20260718T035213Z/`；旧 BLOCKED、失败 attempt 和 coordinator bug 证据均保留。

下一步（外部前置条件解除后）：

1. 在 CloudLab Portal 确认当前 experiment 的延期真正传播到两机 manifest，再创建新的唯一证据目录重跑 WP0.1。
2. 提供仓库外 `0600` 最小权限 rclone 配置、remote、private bucket/project prefix 后重跑 WP0.2 完整闭环。
3. 在 GitHub 认证可用时普通、非 force 推送冻结基线 branch/tag，并验证远端 refs 精确指向 `0a88f03a21802be0eadc3065b93cb97876a6bd2f`。

## 9. 未冻结合同值

以下字段保持 `PENDING`/`null`，不得从 smoke 或仿真结果臆测：

- scheduler/poll period；
- handoff estimate；
- 四个 anchor 的 physical arrival scale；
- decision logging mode/cap/bucket；
- formal host-profile SHA；
- formal config SHA；
- S3 remote/bucket/prefix/object；
- 16-worker 与 12-worker fallback 的最终选择。
