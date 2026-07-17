# RescueSched 项目状态

> 单一状态入口。实验合同见 `EXPERIMENT_CONTRACT.md`，操作入口见 `RUNBOOK.md`，证据映射见 `EVIDENCE_INDEX.md`。
> 最后更新：2026-07-17T16:35:31Z（UTC）

## 1. 当前结论

| 项目 | 状态 | 结论 |
| --- | --- | --- |
| WP0.1 租期证据 | **BLOCKED** | 两机实时 `geni-get -n manifest` 均显示 `2026-07-18T03:00:00Z`，没有显示负责人预期的 2026-07-25。 |
| WP0.2 S3 闭环 | **BLOCKED** | 两机均未发现仓库外 `rclone`/AWS 配置或凭据，不能执行 upload → remote SHA → download → local SHA → delete。 |
| WP1 集成基线 | **PASS_LOCAL / REMOTE_PUSH_BLOCKED** | integration、文档/config、两机 Release + 24/24 CTest、clean/same-commit 和本地 annotated tag 均完成；GitHub HTTPS 凭据在节点上不可用，branch/tag 远端发布待认证后补做。 |
| WP2 及以后 | **NOT_STARTED** | 未执行 host tuning、pilot、calibration 或 formal experiment。 |

**当前禁止事项：** WP0 未通过时，不得开始 host tuning、pilot 或 formal paired block；不得把 2026-07-25 写成已由 manifest 证明的租期。

## 2. Git 与集成范围

```yaml
repository: /users/Mingyang/Micro_banch
branch: codex/infocom2027-integration
integration_base: 5379f1af94a042814493a6329386b056738bfeaf
integration_commit: SELF  # 以包含本文件的提交为准：git rev-parse HEAD
release_tag: physical-integration-v1  # local annotated tag；remote push blocked by missing credentials
working_tree_required: clean
remote_publication: BLOCKED_CREDENTIALS
```

选择性集成保留了以下历史研究资产：

- `src/core/simulator.cpp`
- `paper/main.tex`
- `artifacts/step-21-corrected-full/manifest.md`
- `scripts/corrected_eval_analysis.py`

物理裁剪分支中会破坏完整 simulator 的公共 `constants.h` / `types.h` 简化版本未导入。物理 runtime、UDP RPC、trace generator、测试和辅助脚本按 allowlist 集成，公共构建文件手工合并。

## 3. 两机与租期证据

| 节点 | 角色 | hostname | 实验地址 | 实验 NIC | manifest expiration |
| --- | --- | --- | --- | --- | --- |
| node0 | server/main experiment | `amd140.utah.cloudlab.us` | `10.10.1.1` | `enp65s0f0np0` / `mlx5_core` | `2026-07-18T03:00:00Z` |
| node1 | load generator/coordinator | `amd136.utah.cloudlab.us` | `10.10.1.2` | `enp65s0f0np0` / `mlx5_core` | `2026-07-18T03:00:00Z` |

最终复核采集：node0 `2026-07-17T16:35:12Z`、node1 `2026-07-17T16:35:31Z`，命令为 `geni-get -n manifest`（绕过缓存）。两份 manifest 的 SHA256 相同：

```text
ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8
```

节点本地、被 Git 忽略的原始证据目录：

```text
physical-results/wp0-final-review-20260717T163447Z/
```

按当前 manifest 计算：

```text
T_expire       = 2026-07-18T03:00:00Z
T_no_new_block = 2026-07-17T15:00:00Z  # 已经过期
T_restore      = 2026-07-17T21:00:00Z
```

因此当前不得启动新的正式 paired block。负责人需在 CloudLab Portal 核对延期是否真正提交到当前 experiment；Portal 与 manifest 一致前，租期门禁保持 `BLOCKED`。

## 4. 主机事实与计划布局

两机当前均为 Ubuntu 22.04.2、kernel `5.15.0-177-generic`、AMD EPYC 7302P；每机 16 个 physical cores / 32 个 logical CPUs，CPU `16-31` 分别是 CPU `0-15` 的 SMT siblings。实验 NIC 为 Mellanox `mlx5_core`。

WP1 只记录**计划布局**，没有 apply host profile：

- node0 worker CPUs：`0-15`，每个 physical core 只选一个 worker；
- node0 control/IRQ logical CPUs：`16-31`，它们是 worker cores 的 SMT siblings；
- node1 client/coordinator：CPU `0-7`；实验 IRQ：CPU `8-15`；CPU `16-31` 不运行实验线程；
- 旧文档中的 `0,2,4,...,30` worker 示例无效，因为会重复使用同一 physical core。

正式使用前必须由 topology validator 重新验证，并在 WP3 冻结实际 profile/hash。

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

计划 region 为 `us-west-2`，但以下值均未获得，不能虚构：

```yaml
rclone_config: null
remote_name: null
bucket: null
prefix: null
test_object: null
last_remote_verified_object: null
```

已检查且不存在：

- `~/.config/rescuesched/rclone.conf`
- `~/.config/rclone/rclone.conf`
- `~/.aws/credentials`
- `~/.aws/config`

解除阻断所需输入：仓库外、权限 `0600` 的最小权限 rclone 配置，以及 private bucket/prefix 标识。secret 不得进入 Git、命令日志或聊天。

## 7. 已完成与下一步

最近完成：

1. 从冻结 baseline 建立 `codex/infocom2027-integration`。
2. 选择性导入物理 runtime/RPC/trace/tests/scripts，并保留 simulator、paper 和 corrected artifacts。
3. node0/node1 已在同一 clean 最终提交上分别完成 Release build 和 24/24 CTest，固定 final evidence 目录中的日志与 SHA 清单已验证。
4. 两机重新采集 uncached manifest，并确认当前仍为 2026-07-18 03:00 UTC。
5. HTTPS `git push` 因节点无 GitHub credential 失败；使用 `git bundle` 完成 node1 同提交部署，没有伪造远端发布。
6. coordinator 负向验收发现远端 commit `test` 的失败状态会被后续 shell 命令掩盖；已改为显式读取/比较 server commit 和 dirty status，并通过 load-generator dirty、server dirty、server commit mismatch 三项 fail-closed 验收。
7. 首轮 commit-mismatch 负向验收因上述缺陷意外启动了一个失败的非正式 short smoke；失败目录保留在 `physical-results/wp1-mismatch-should-not-exist/`、`physical-results/wp1-final-failclosed/` 及 node0 的 `physical-results/two-node-active/L0_RandomCore-20260717T162510Z-7192/`，不作为正式结果或 PASS 证据。

下一步（最多三项）：

1. 在有 GitHub 认证的环境推送 `codex/infocom2027-integration` 和 `physical-integration-v1`；不得改写 tag。
2. 由负责人修复 Portal/manifest 租期不一致。
3. 提供仓库外最小权限 rclone 配置后重做 WP0 S3 闭环。

## 8. 未冻结合同值

以下字段保持 `PENDING`/`null`，不得从 smoke 或仿真结果臆测：

- scheduler/poll period；
- handoff estimate；
- 四个 anchor 的 physical arrival scale；
- decision logging mode/cap/bucket；
- formal host-profile SHA；
- formal config SHA；
- S3 remote/bucket/prefix/object；
- 16-worker 与 12-worker fallback 的最终选择。
