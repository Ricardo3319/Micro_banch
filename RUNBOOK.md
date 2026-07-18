# RescueSched WP0/WP1/WP2 操作手册

> 本文覆盖租期/S3 门禁、integration baseline、2026-07-18T15:17:01Z 的 WP2 前置门禁调整，以及源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 的 WP2 本机代码级验证入口。WP2 已在两台节点分别通过代码级 gate，但 host tuning、两机 RPC smoke、pilot、calibration、formal 入口全部保持停止。所有时间使用 UTC；所有 secret 必须位于仓库外。

## 0. 主机约定与 fail-closed 规则

| 名称 | 角色 | control path | experiment path | repo |
| --- | --- | --- | --- | --- |
| node0 | server | 本机 / `amd140.utah.cloudlab.us` | `10.10.1.1` | `/users/Mingyang/Micro_banch` |
| node1 | coordinator/client | SSH alias `node1` / `amd136.utah.cloudlab.us` | `10.10.1.2` | `/users/Mingyang/Micro_banch` |

全局成功条件：命令 exit code 0、要求的 status/限定范围 owner waiver 明确、identity 一致且输出被保留。恢复入口：任何一步失败即停止后续 gate，保存 stdout/stderr 和目录；不删除失败证据，不执行主机调优。`DEFERRED`/`WAIVED` 不得写成数据面或远端发布 PASS。

## 1. WP0.1：采集 uncached lease manifest

**Host：node0。Input：** node1 SSH 可用。**标准成功条件：** 两份原始 XML、采集时间和 SHA256 存在，且 expiration 明确显示 2026-07-25，才能判 `PASS_MANIFEST`。**恢复入口：** 若不满足，保存差异并停止受影响阶段；禁止覆盖旧目录。当前负责人调整只允许以 Portal+负责人确认形成 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2`，不得冒充 `PASS_MANIFEST`。

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="physical-results/wp0-lease-$STAMP"
mkdir -p "$OUT/node0" "$OUT/node1"
date -u +%Y-%m-%dT%H:%M:%SZ > "$OUT/collected_at_utc.txt"
hostname -f > "$OUT/node0/hostname.txt"
geni-get -n manifest > "$OUT/node0/manifest.xml"
ssh -o BatchMode=yes node1 'hostname -f' > "$OUT/node1/hostname.txt"
ssh -o BatchMode=yes node1 'geni-get -n manifest' > "$OUT/node1/manifest.xml"
sha256sum "$OUT/node0/manifest.xml" "$OUT/node1/manifest.xml" \
  > "$OUT/MANIFEST_SHA256SUMS"
grep -oE 'expires="[^"]+"' "$OUT"/node*/manifest.xml
# 仅在 manifest 与 Portal 认知冲突时补采诊断；这些输出不自动替代 manifest 门禁。
geni-get -n status > "$OUT/node0/status.raw"
geni-get -n portalmanifest > "$OUT/node0/portalmanifest.xml"
ssh -o BatchMode=yes node1 'geni-get -n status' > "$OUT/node1/status.raw"
ssh -o BatchMode=yes node1 'geni-get -n portalmanifest' > "$OUT/node1/portalmanifest.xml"
```

最新 manifest 证据 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/` 显示：2026-07-18T14:44:32Z 两机实时执行 `geni-get -n manifest` 后仍均为 `2026-07-18T03:00:00Z`。安装的客户端源码表明 `-n` 不读取本地缓存（该版本客户端本身不缓存）；补采 AM v3 `status` 为 `2026-07-24 05:00:00`，Portal 截图为 `Jul 24, 2026 7:00 PM` 且不含时区。

项目负责人于 2026-07-18T15:17:01Z 正式确认采用调整，证据位于 `physical-results/gate-policy-revision-20260718T151701Z/`：对 WP2 runtime 工作，以实时 Portal 实验页加负责人确认判为 `PASS_OWNER_ATTESTED_PORTAL_FOR_WP2`；manifest 子检查继续登记为 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`，精确 expiration UTC 继续为 `UNRESOLVED`。因此当前可进入 WP2 代码工作，但不得进入 host tuning、两机 RPC smoke、pilot、calibration 或 formal。历史证据目录继续保留，不覆盖、不删除。

## 2. WP0.2：S3 最小权限闭环

### 2.1 配置前检查

**Host：node0。Input：** 负责人提供仓库外 rclone 配置路径、remote 名称和 private prefix；不要把配置内容打印到终端。**成功条件：** 文件 owner 为当前用户、mode `0600`，bucket/prefix 明确。**恢复入口：** mode 或权限不满足时停止并由负责人修复 IAM/config；不执行上传。

```bash
export RCLONE_CONFIG="$HOME/.config/rescuesched/rclone.conf"
export S3_REMOTE='<remote-name>'
export S3_PREFIX='<private-bucket>/<project-prefix>/wp0-gate'
test -f "$RCLONE_CONFIG"
test "$(stat -c '%a' "$RCLONE_CONFIG")" = 600
rclone --config "$RCLONE_CONFIG" lsd "$S3_REMOTE:" >/dev/null
```

不运行 `rclone config show`，不把 access key、secret key、session token 或配置文件加入 Git。

### 2.2 upload → remote SHA → download → delete

**Host：node0。Input：** 上节三个环境变量。**成功条件：** local、remote stream、downloaded 三个 SHA 完全一致；只删除本次唯一 test object；删除后对象不存在。**恢复入口：** 任一命令或 SHA 失败，保留本地文件和日志、停止删除其他对象；只允许人工核对本次 test key。

```bash
set -euo pipefail
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="physical-results/wp0-s3-$STAMP"
mkdir -p "$OUT"
TEST_FILE="/tmp/rescuesched-s3-gate-$STAMP.bin"
DOWNLOADED="/tmp/rescuesched-s3-gate-$STAMP.downloaded.bin"
OBJECT="$S3_REMOTE:$S3_PREFIX/$(basename "$TEST_FILE")"
head -c 1048576 /dev/urandom > "$TEST_FILE"
LOCAL_SHA="$(sha256sum "$TEST_FILE" | awk '{print $1}')"
rclone --config "$RCLONE_CONFIG" copyto "$TEST_FILE" "$OBJECT"
REMOTE_SHA="$(rclone --config "$RCLONE_CONFIG" cat "$OBJECT" | sha256sum | awk '{print $1}')"
rclone --config "$RCLONE_CONFIG" copyto "$OBJECT" "$DOWNLOADED"
DOWNLOADED_SHA="$(sha256sum "$DOWNLOADED" | awk '{print $1}')"
test "$LOCAL_SHA" = "$REMOTE_SHA"
test "$LOCAL_SHA" = "$DOWNLOADED_SHA"
rclone --config "$RCLONE_CONFIG" deletefile "$OBJECT"
if rclone --config "$RCLONE_CONFIG" lsf "$OBJECT" | grep -q .; then
  echo 'test object still exists' >&2
  exit 1
fi
printf 'status=PASS\nobject=%s\nlocal_sha256=%s\nremote_sha256=%s\ndownloaded_sha256=%s\n' \
  "$OBJECT" "$LOCAL_SHA" "$REMOTE_SHA" "$DOWNLOADED_SHA" > "$OUT/S3_GATE_STATUS.txt"
rm -f "$TEST_FILE" "$DOWNLOADED"
```

当前 2026-07-18T14:54:03Z preflight 证据位于 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.2/`：两机首选/默认 rclone 配置、remote 和 private bucket/project prefix 均缺失，故所有数据面步骤为 `NOT_RUN`。负责人调整后状态为 `DEFERRED_UNTIL_PILOT`：它不再阻塞 WP2，但进入 pilot/formal 前仍必须按本节完成全部闭环。

`S3_GATE_STATUS.txt` 只能包含无 secret 的 remote/object 标识和 hashes。当前没有配置输入，本节尚未执行，不能写成 PASS。

## 3. 安装 WP1 依赖

**Host：node0 和 node1，分别执行。Input：** Ubuntu apt 网络。**成功条件：** 所有包安装成功且关键工具可运行。**恢复入口：** apt 失败时停止构建，保存 apt 错误，修复源后重试；不要跳过缺失工具。

```bash
sudo apt-get update
sudo apt-get install -y \
  git build-essential cmake ninja-build python3 python3-venv python3-pip \
  numactl hwloc ethtool iproute2 \
  linux-tools-common "linux-tools-$(uname -r)" \
  jq zstd rclone tmux
cmake --version
ninja --version
python3 --version
jq --version
rclone version
perf --version
lstopo --version
```

## 4. 建立和部署 integration branch

### 4.1 node0 建立基线

**Host：node0。Input：** clean baseline `5379f1af94a042814493a6329386b056738bfeaf`。**成功条件：** 当前分支为 `codex/infocom2027-integration`，历史资产存在，无意外删除。**恢复入口：** 删除审计异常时停止并回到 baseline，按 allowlist 重新集成；禁止整体合并裁剪分支。

```bash
cd /users/Mingyang/Micro_banch
git fetch --prune origin
git switch -c codex/infocom2027-integration \
  5379f1af94a042814493a6329386b056738bfeaf
for path in \
  src/core/simulator.cpp \
  paper/main.tex \
  artifacts/step-21-corrected-full/manifest.md \
  scripts/corrected_eval_analysis.py; do
  test -f "$path"
done
git diff --name-status 5379f1af94a042814493a6329386b056738bfeaf --
```

### 4.2 node1 部署已推送提交

**Host：node1。Input：** node0 已 push integration branch。**成功条件：** `HEAD` 等于给定 full SHA，working tree clean。**恢复入口：** 本地有修改时先保存到独立 patch/branch，不用 `reset --hard` 覆盖未知工作；identity 不匹配时停止构建。

```bash
cd /users/Mingyang/Micro_banch
EXPECTED_COMMIT='<full-integration-sha>'
git fetch --prune origin
git switch --force-create codex/infocom2027-integration \
  origin/codex/infocom2027-integration
test "$(git rev-parse HEAD)" = "$EXPECTED_COMMIT"
test -z "$(git status --porcelain)"
```

### 4.3 无 GitHub 凭据时的审计型 bundle 部署

**Host：node0 发起、node1 接收。Input：** node0 clean commit，node1 clean worktree。**成功条件：** bundle 验证/fetch 成功，node1 full SHA 完全相同。**恢复入口：** 任一 checkout 不 clean 时停止并人工保存本地修改；bundle 只解决节点间部署，不冒充 GitHub push。

```bash
# node0
cd /users/Mingyang/Micro_banch
COMMIT="$(git rev-parse HEAD)"
BUNDLE="/tmp/rescuesched-wp1-$COMMIT.bundle"
git bundle create "$BUNDLE" codex/infocom2027-integration
git bundle verify "$BUNDLE"
scp "$BUNDLE" node1:/tmp/

# node1
cd /users/Mingyang/Micro_banch
test -z "$(git status --porcelain)"
git fetch "/tmp/rescuesched-wp1-$COMMIT.bundle" \
  codex/infocom2027-integration
git switch --force-create codex/infocom2027-integration FETCH_HEAD
test "$(git rev-parse HEAD)" = "$COMMIT"
test -z "$(git status --porcelain)"
```

负责人已将 GitHub publication 设为 `WAIVED_BY_OWNER`，所以它不阻塞 WP2。远端 branch/tag 实际仍为 `ABSENT`；若以后选择发布，必须在有认证的环境执行普通、非 force push，并验证精确 refs。

## 5. 分析 Python 环境

**Host：node0；node1 仅在需要离线分析时重复。Input：** `requirements/analysis-lock.txt`。**成功条件：** venv 创建成功，`pip check` 和 imports PASS。**恢复入口：** 删除仅被忽略的 `.venv-analysis/` 后按锁文件重建；不要修改系统 Python。

```bash
cd /users/Mingyang/Micro_banch
python3 -m venv .venv-analysis
.venv-analysis/bin/python -m pip install --upgrade pip
.venv-analysis/bin/python -m pip install -r requirements/analysis-lock.txt
.venv-analysis/bin/python -m pip check
.venv-analysis/bin/python - <<'PY'
import matplotlib
import matplotlib.pyplot
print(matplotlib.__version__)
PY
```

## 6. Release build 与 CTest

**Host：node0 和 node1，分别执行。Input：** 同一 clean integration commit。**当前成功条件：** configure/build exit 0，CTest `100% tests passed, 0 tests failed out of 26`。**恢复入口：** 保留失败 build directory/log；修复代码后产生新提交，两机全部重跑，禁止在不同 commit 拼接 PASS。冻结 tag `physical-integration-v1` 的历史验收仍为 24/24，不得把历史日志改写成 26/26。

```bash
cd /users/Mingyang/Micro_banch
test -z "$(git status --porcelain)"
COMMIT="$(git rev-parse HEAD)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BUILD="build-integration-$STAMP"
EVIDENCE="physical-results/wp1-build-$STAMP-$(hostname -s)"
mkdir -p "$EVIDENCE"
printf '%s\n' "$COMMIT" > "$EVIDENCE/commit.txt"
git status --short --branch > "$EVIDENCE/git-status.txt"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  2>&1 | tee "$EVIDENCE/cmake-configure.log"
cmake --build "$BUILD" --parallel \
  2>&1 | tee "$EVIDENCE/cmake-build.log"
ctest --test-dir "$BUILD" --output-on-failure \
  2>&1 | tee "$EVIDENCE/ctest.log"
git status --short --branch > "$EVIDENCE/git-status-after.txt"
test -z "$(git status --porcelain)"
sha256sum "$EVIDENCE"/*.txt "$EVIDENCE"/*.log > "$EVIDENCE/SHA256SUMS"
```

## 7. 两机 identity 和 coordinator fail-closed 检查

**Host：node1。Input：** node0/node1 repo 都已部署。**成功条件：** full commit 完全相同、两边 clean；coordinator 源码/帮助入口存在且脚本包含 dirty/mismatch 拒绝逻辑。此 WP1 gate 不启动 RPC smoke。**恢复入口：** 任一不一致即停止；重新 fetch/checkout/build，不绕过检查。

```bash
cd /users/Mingyang/Micro_banch
LOCAL_COMMIT="$(git rev-parse HEAD)"
REMOTE_COMMIT="$(ssh -o BatchMode=yes node0 \
  'cd /users/Mingyang/Micro_banch && git rev-parse HEAD')"
test "$LOCAL_COMMIT" = "$REMOTE_COMMIT"
test -z "$(git status --porcelain)"
ssh -o BatchMode=yes node0 \
  'cd /users/Mingyang/Micro_banch && test -z "$(git status --porcelain)"'
grep -q 'status --porcelain' scripts/run_two_node_rpc_smoke.sh
grep -q 'server_commit' scripts/run_two_node_rpc_smoke.sh
bash scripts/run_two_node_rpc_smoke.sh --help >/dev/null
```

在当前 CloudLab 配置中，从 node1 到 node0 的可用 SSH alias 可能是 `node0` 或 control hostname；按实际 SSH config 使用，但 RPC server IP 必须是 `10.10.1.1`。

## 8. 验证冻结 integration tag（已完成，禁止重建）

`physical-integration-v1` 已是冻结 annotated tag。任何后续状态或文档提交都不得移动、删除或重新创建它。这里只允许只读验证：

```bash
cd /users/Mingyang/Micro_banch
TAG=physical-integration-v1
test "$(git cat-file -t "$TAG")" = tag
test "$(git rev-parse "$TAG")" = 2fb914080bc63d357e38b8f4e21a6d5add34dc8e
test "$(git rev-list -n1 "$TAG")" = 0a88f03a21802be0eadc3065b93cb97876a6bd2f
```

禁止运行 `git tag -f`、`git tag -d physical-integration-v1`、重新 `git tag -a` 或 force push。GitHub publication 为 `WAIVED_BY_OWNER`；这不改变本地冻结 tag，也不表示远端 tag 已存在。

## 9. WP2 本机代码级验证入口

以下命令必须在 node0 和 node1 **分别**运行，使用各自新的唯一 `physical-results/` 目录。它们不修改 host profile，也不建立 node0↔node1 数据面。

### 9.1 Release 26/26

```bash
cd /users/Mingyang/Micro_banch
test -z "$(git status --porcelain)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BUILD="build-wp2-release-$STAMP"
OUT="physical-results/wp2-release-$STAMP-$(hostname -s)"
mkdir -p "$OUT"
printf 'start_utc=%s\ncommit=%s\n' \
  "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(git rev-parse HEAD)" > "$OUT/manifest.env"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  2>&1 | tee "$OUT/configure.log"
cmake --build "$BUILD" --parallel 2>&1 | tee "$OUT/build.log"
ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tee "$OUT/ctest.log"
grep -F '100% tests passed, 0 tests failed out of 26' "$OUT/ctest.log"
test -z "$(git status --porcelain)"
```

### 9.2 ASan/UBSan 与 TSan 核心

`scripts/run_sanitizers.sh` 的前两个位置参数是 build directory；该脚本没有 `--help` 入口，不得把 `--help` 作为参数。

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="physical-results/wp2-sanitizers-$STAMP-$(hostname -s)"
mkdir -p "$OUT"
scripts/run_sanitizers.sh \
  "build-wp2-asan-$STAMP" "build-wp2-tsan-$STAMP" \
  2>&1 | tee "$OUT/full.log"
```

成功条件：ASan/UBSan 26/26；TSan 正常执行 `physical_runtime_validity` 和 `wp2_runtime_concurrency` 且 2/2 PASS。若环境在测试执行前报告 TSan shadow-memory mapping 不支持，只能登记 `UNSUPPORTED`，不能写成 PASS。

### 9.3 本机四策略 UDP/mapping/fail-closed gate

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
scripts/run_local_wp2_rpc_gate.sh \
  --build-dir "build-wp2-rpc-$STAMP" \
  --out-dir "physical-results/wp2-rpc-$STAMP-$(hostname -s)" \
  --port 19184
```

该入口只允许 loopback 和非正式端口。脚本显式拒绝正式端口 `9000`。成功条件包括四方法 `flow_id/source_port/kernel_reuseport_ingress_shard` projection 在同一节点完全一致，以及 response queue 注入产生预期 fail-closed、无 silent drop。不同节点的 kernel mapping SHA 不要求相同。

### 9.4 三次四策略 synthetic stress

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
scripts/run_local_physical_runtime_smoke.sh \
  --build-dir "build-wp2-synthetic-$STAMP" \
  --out-dir "physical-results/wp2-synthetic-$STAMP-$(hostname -s)" \
  --stress-repetitions 3
```

成功条件：3 repetitions × L0/L1/M0/M1，共 12/12 implementation runs PASS。输出是 in-process synthetic smoke，不是 RPC、双机 CloudLab、pilot 或论文物理证据。

### 9.5 证据完整性

脚本生成的嵌套 `SHA256SUMS` 使用相对路径，应在对应证据目录中验证；WP2 最终聚合根的 `SHA256SUMS` 使用仓库相对路径，应从仓库根验证。失败 attempt、TSan 诊断和意外 invocation 目录一律保留。

已完成证据：

```text
physical-results/wp2-final-node0-20260718T163835Z/
physical-results/wp2-final-node1-20260718T164147Z/
```

这两个目录记录源码提交 `7db91095c4d0f84a5eb568b980748051f994c4cc` 上两机分别完成 Release 26/26、ASan/UBSan 26/26、TSan 2/2、loopback gate 和 synthetic 12/12。

## 10. WP2 完成后的硬停止线

WP2 的 `PASS_CODE_LEVEL_ON_BOTH_NODES` 只证明同一源码在两台节点分别完成代码级测试，不证明双机网络实验、性能或论文主张，也不自动授权 WP3。

仍然禁止：

1. WP3、CPU/IRQ/NIC tuning、sysctl/governor 变更或 host profile apply；
2. 两机 RPC smoke、pilot、calibration 和 formal experiment；
3. 使用正式实验端口 `9000` 或正式结果目录；
4. 把 S3 `DEFERRED` 写成 PASS，或把 GitHub `WAIVED` 写成已 push；
5. 把旧 manifest 值改写成已证明 2026-07-25；
6. 把本机 loopback 写成双机 RPC，或把 synthetic smoke 写成正式物理结果。

进入 WP3/pilot/formal 前必须先冻结可审计的精确 lease expiration UTC；进入 pilot/formal 前还必须完成 S3 upload/remote-stream-SHA/download/local-SHA/delete 闭环，并获得相应阶段的明确授权。
