# RescueSched CloudLab Quickstart

本文档给出从本地 Codex 连接 CloudLab 双节点、部署相同提交、执行当前可用
物理预检以及判断是否继续的最短路径。它不替代
`docs/CLOUDLAB_PREREGISTRATION.md`，也不把当前 synthetic runtime 描述为
真实 RPC 或 NIC/RSS 实验。

## 1. 当前可以和不可以执行的实验

当前仓库可以在 CloudLab 裸机上产生以下可审计证据：

- server/client 主机、CPU、NUMA、kernel、NIC 和工具链 inventory；
- Release build、CTest、deterministic smoke 和短锚点 schema gate；
- 同 core、同 NUMA、跨 NUMA 的 pinned descriptor handoff 微基准；
- pinned-worker synthetic runtime 的 trace replay、并发正确性和日志验证；
- 在物理主机上执行的模拟器锚点。

当前仓库尚无 networked RPC server/client、NIC/RSS receive path、client RTT
load generator 或 paired two-node runner。因此当前输出只能称为 CloudLab
host preflight、physical-host microbenchmark 或 synthetic runtime validation。
在这些组件实现并冻结前，不得启动预注册的最终 RPC matrix，也不得把输出称为
真实 RSS descriptor migration、end-to-end RPC result 或 production evidence。

## 2. 让 Codex 使用本地 SSH 凭据

先从 CloudLab portal 记录两个节点的公网 hostname 和登录用户名。在本地机器
确认实际使用的私钥路径；只向 Codex 提供路径，不要粘贴私钥内容。

建议在本地 `~/.ssh/config` 创建稳定别名：

```sshconfig
Host rescuesched-server
  HostName <server-hostname>
  User <cloudlab-user>
  IdentityFile ~/.ssh/<cloudlab-private-key>
  IdentitiesOnly yes
  ServerAliveInterval 30
  ServerAliveCountMax 6

Host rescuesched-client
  HostName <client-hostname>
  User <cloudlab-user>
  IdentityFile ~/.ssh/<cloudlab-private-key>
  IdentitiesOnly yes
  ServerAliveInterval 30
  ServerAliveCountMax 6
```

检查权限和非交互连接：

```bash
chmod 600 ~/.ssh/<cloudlab-private-key>
ssh -o BatchMode=yes rescuesched-server 'hostname; id -un'
ssh -o BatchMode=yes rescuesched-client 'hostname; id -un'
```

两个命令都成功后，Codex 才能通过这两个别名执行远程命令。首次出现 host-key
确认时应由用户核对 CloudLab portal 中的节点信息并接受；不要关闭 host-key
校验，也不要把私钥复制进项目仓库或 CloudLab 节点。

## 3. 本地冻结并上传实验提交

在本地项目根目录检查即将上传的内容：

```bash
cd /home/ricardo/projects/Micro_banch
git status --short
git diff --stat
git diff -- paper/
```

本轮上传不得包含新的 `paper/` 修改。提交并推送非论文文件后记录完整 SHA：

```bash
git push origin codex/rescuesched-baselines
EXPERIMENT_COMMIT="$(git rev-parse HEAD)"
printf '%s\n' "$EXPERIMENT_COMMIT"
git ls-remote origin refs/heads/codex/rescuesched-baselines
```

只有 `git ls-remote` 输出的 SHA 与 `EXPERIMENT_COMMIT` 完全一致时，才部署该
版本。此 SHA 是本轮不可变实验标识；分支之后移动也不能改变已开始的 run。

## 4. 在两个节点部署同一提交

分别登录 server 和 client，安装相同依赖：

```bash
sudo apt-get update
sudo apt-get install -y \
  git build-essential cmake ninja-build python3 \
  tmux numactl hwloc ethtool iproute2 sysstat chrony
```

然后在两个节点执行相同部署命令，把占位符替换为第 3 节冻结的完整 SHA：

```bash
cd ~
if [[ ! -d Micro_banch/.git ]]; then
  git clone --branch codex/rescuesched-baselines \
    https://github.com/Ricardo3319/Micro_banch.git
fi
cd ~/Micro_banch
git fetch origin codex/rescuesched-baselines
EXPERIMENT_COMMIT=<full-frozen-commit-sha>
git checkout --detach "$EXPERIMENT_COMMIT"
test "$(git rev-parse HEAD)" = "$EXPERIMENT_COMMIT"
test -z "$(git status --porcelain)"
```

任何 `test` 失败都必须停止。不要执行 `git pull` 代替 detached checkout。

## 5. 两端先记录机器身份

在 server 和 client 分别执行，并保留输出：

```bash
cd ~/Micro_banch
ROLE=server  # client 节点改为 ROLE=client
OUT="physical-results/inventory-$(date -u +%Y%m%dT%H%M%SZ)-${ROLE}-$(hostname -s)"
mkdir -p "$OUT"
{
  date --iso-8601=seconds
  hostnamectl || hostname
  uname -a
  lscpu
  numactl --hardware || true
  free -h
  ip -br link
  ip -br addr
  ethtool -i "$(ip route show default | awk 'NR==1 {print $5}')" || true
  git rev-parse HEAD
  git status --short --branch
} 2>&1 | tee "$OUT/host-inventory.txt"
sha256sum "$OUT/host-inventory.txt" > "$OUT/SHA256SUMS"
```

记录 portal 中的 node type、experiment ID 和 server/client 角色映射。不要仅凭
hostname 猜测角色。

## 6. Server 节点执行正式预检 gate

在 server 上用 `tmux` 执行，避免 SSH 断开终止进程：

```bash
cd ~/Micro_banch
tmux new -s rescuesched-preflight

EXPECTED_COMMIT="$(git rev-parse HEAD)"
bash scripts/run_physical_preflight.sh \
  --expected-commit "$EXPECTED_COMMIT" \
  --require-clean \
  2>&1 | tee physical-preflight-console.log
```

按 `Ctrl+B`、再按 `D` 可退出会话；恢复时运行：

```bash
tmux attach -t rescuesched-preflight
```

命令结束后检查最新结果：

```bash
cd ~/Micro_banch
RESULT_DIR="$(find physical-results -maxdepth 1 -type d \
  -name 'preflight-*' | sort | tail -n 1)"
printf 'RESULT_DIR=%s\n' "$RESULT_DIR"
cat "$RESULT_DIR/PREFLIGHT_STATUS.txt"
cat "$RESULT_DIR/MICROBENCH_STATUS.txt"
cat "$RESULT_DIR/pinned-handoff/HANDOFF_STATUS.txt"
tail -n 30 "$RESULT_DIR/logs/ctest.log"
tail -n 20 "$RESULT_DIR/logs/rescue-smoke.log"
tail -n 20 "$RESULT_DIR/logs/short-anchor-schema.log"
cat "$RESULT_DIR/metadata/manifest.env"
cat "$RESULT_DIR/metadata/git-status.txt"
```

只有以下条件全部满足才通过 gate：

- 三个状态文件都包含 `status=PASS`；
- CTest 全部通过；
- smoke 包含 `RescueSched smoke status: PASS`；
- schema 校验成功；
- manifest 中 commit 等于冻结 SHA；
- git status 证明正式运行前工作区干净；
- 不可用的跨 NUMA 场景明确记录 `SKIPPED_UNAVAILABLE_TOPOLOGY`，而不是数字。

任一条件失败时保留整个失败目录，阅读相应 `logs/*.log` 修复后生成新的 run；
不得删除失败 run，也不得从失败输出中选择有利数字。

## 7. Client 节点当前的 gate

由于尚无网络 load generator，client 当前只执行相同提交校验、host inventory、
时间同步和 NIC 记录：

```bash
cd ~/Micro_banch
test -z "$(git status --porcelain)"
timedatectl status || true
chronyc tracking || true
ip route
ip -s link
```

当前不存在可合法启动的 `loadgen`、`client` 或 `rpc_client` 命令。看到 server
预检 PASS 后也不能自行用 synthetic runtime 充当 client traffic。

## 8. 可选的 synthetic runtime 验证

server 预检 PASS 后，可运行本地多 worker 实现验证：

```bash
cd ~/Micro_banch
bash scripts/run_local_physical_runtime_smoke.sh \
  --requests 1000 \
  --warmup 100 \
  --stress-repetitions 3
```

该步骤检查 queued-only migration、descriptor ownership、EWMA 更新、日志 schema
和 affinity 等实现约束。其输出仍不是网络实验，也不能替代 client RTT。

## 9. 冻结未来最终实验的运行顺序

在开始任何最终 repetition 前，只生成一次预注册顺序：

```bash
cd ~/Micro_banch
mkdir -p physical-results/cloudlab-final
python3 scripts/generate_cloudlab_run_order.py \
  --output physical-results/cloudlab-final/run-order.csv
python3 scripts/generate_cloudlab_run_order.py \
  --verify physical-results/cloudlab-final/run-order.csv
sha256sum physical-results/cloudlab-final/run-order.csv
```

预期 CSV 为 160 行数据加一个 header，预期 sidecar SHA-256 为：

```text
e4511a32144f7535d91900d1580dd9a3ec312f18862d41be74a8347d5db88776
```

若校验值不同，停止并检查脚本版本和换行，不要手工编辑 schedule。

## 10. 最终双节点实验开始前的停止条件

只有下列组件全部以已提交代码存在并通过测试后，才能执行
`docs/CLOUDLAB_PREREGISTRATION.md` 的 4 anchors x 10 seeds x 4 methods：

1. networked RPC server 和 open-loop client/load generator；
2. NIC/RSS receive path 与 initial placement 记录；
3. server-side completion 和 client-observed RTT 的独立日志；
4. shared trace identity、双节点 manifest 和 paired runner；
5. load calibration manifest、timeout/drop 规则和自动归档；
6. 四方法共享 runtime、资源预算、handoff primitive 和时间源的验证。

缺少任一项时标记 `[IMPLEMENTATION DETAIL REQUIRED]` 或
`[PHYSICAL RESULT REQUIRED]`，不得用 host-local synthetic 数据填充论文物理结果。

## 11. 必须按顺序阅读的项目材料

正式操作前按以下顺序阅读：

1. `docs/CLOUDLAB_QUICKSTART.md`：连接、部署和立即可执行的 gate；
2. `docs/PHYSICAL_MACHINE_RUNBOOK.md`：预检细节、归档和故障处理；
3. `docs/PHYSICAL_RUNTIME_CONTRACT.md`：descriptor 状态、所有权和证据边界；
4. `docs/CLOUDLAB_PREREGISTRATION.md`：最终矩阵、统计和失败重跑规则；
5. `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md`：物理复现实验缺口；
6. `docs/ARTIFACT_PROVENANCE.md`：哪些 artifact 可以进入论文证据链。

操作时主要根据以下信号决定继续或停止：

- 版本信号：冻结 commit 完全一致，worktree clean；
- 构建信号：Release build 和 CTest PASS；
- 实现信号：smoke、schema、runtime invariant PASS；
- 环境信号：handoff stability PASS，拓扑缺失明确 SKIP；
- 证据信号：manifest、原始日志和 SHA-256 完整；
- 停止信号：任何 FAIL、trace SHA mismatch、affinity failure、节点/网络故障，
  或最终 RPC 所需组件仍不存在。

## 12. 归档并下载预检证据

在 server 上：

```bash
cd ~/Micro_banch
RESULT_DIR="$(find physical-results -maxdepth 1 -type d \
  -name 'preflight-*' | sort | tail -n 1)"
ARCHIVE="$(basename "$RESULT_DIR").tar.gz"
tar -czf "$ARCHIVE" "$RESULT_DIR" physical-preflight-console.log
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
```

在本地机器：

```bash
scp rescuesched-server:~/Micro_banch/preflight-*.tar.gz .
scp rescuesched-server:~/Micro_banch/preflight-*.tar.gz.sha256 .
sha256sum -c preflight-*.tar.gz.sha256
```

下载后保持原始 archive 不变；分析输出写入新的派生目录并记录输入 checksum。
