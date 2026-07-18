# RescueSched WP0/WP1 操作手册

> 本文只覆盖租期/S3 门禁和 integration baseline。WP0 当前 `BLOCKED`，所以本手册中的 host tuning、pilot、calibration、formal 入口全部保持停止。所有时间使用 UTC；所有 secret 必须位于仓库外。

## 0. 主机约定与 fail-closed 规则

| 名称 | 角色 | control path | experiment path | repo |
| --- | --- | --- | --- | --- |
| node0 | server | 本机 / `amd140.utah.cloudlab.us` | `10.10.1.1` | `/users/Mingyang/Micro_banch` |
| node1 | coordinator/client | SSH alias `node1` / `amd136.utah.cloudlab.us` | `10.10.1.2` | `/users/Mingyang/Micro_banch` |

全局成功条件：命令 exit code 0、要求的 status 文件为 `PASS`、identity 一致且输出被保留。恢复入口：任何一步失败即停止后续 gate，保存 stdout/stderr 和目录；不删除失败证据，不执行主机调优。

## 1. WP0.1：采集 uncached lease manifest

**Host：node0。Input：** node1 SSH 可用。**成功条件：** 两份原始 XML、采集时间和 SHA256 存在，且 expiration 明确显示 2026-07-25 才能判租期 PASS。**恢复入口：** 若不是 7 月 25 日，停止新 formal block，在 Portal 修正延期后创建新证据目录重跑；禁止覆盖旧目录。

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

最新证据 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.1/` 显示：2026-07-18T14:44:32Z 两机实时执行 `geni-get -n manifest` 后仍均为 `2026-07-18T03:00:00Z`。安装的客户端源码表明 `-n` 不读取本地缓存（该版本客户端本身不缓存）；补采 AM v3 `status` 为 `2026-07-24 05:00:00`，Portal 截图为 `Jul 24, 2026 7:00 PM` 且不含时区。来源不一致时保持 gate `BLOCKED`，不得自行选择较晚值；若要改用其他权威源，必须先正式修订合同、记录精确 UTC 语义并重新验收两机。历史证据目录继续保留，不覆盖、不删除。

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

当前 2026-07-18T14:54:03Z preflight 证据位于 `physical-results/wp0-wp1-unblock-20260718T144404Z/wp0.2/`：两机首选/默认 rclone 配置、remote 和 private bucket/project prefix 均缺失，故所有数据面步骤为 `NOT_RUN`，gate 为 `BLOCKED`。

`S3_GATE_STATUS.txt` 只能包含无 secret 的 remote/object 标识和 hashes。当前没有配置输入，本节尚未执行。

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

远端 branch/tag 仍须之后在有 GitHub 认证的环境执行普通、非 force push。

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

**Host：node0 和 node1，分别执行。Input：** 同一 clean integration commit。**成功条件：** configure/build exit 0，CTest `100% tests passed, 0 tests failed out of 24`。**恢复入口：** 保留失败 build directory/log；修复代码后产生新提交，两机全部重跑，禁止在不同 commit 拼接 PASS。

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

## 8. 创建 integration tag

**Host：node0。Input：** 两机同 full commit、clean、Release/24 CTest PASS。**成功条件：** annotated tag 指向当前 HEAD；有认证时 push 成功。**恢复入口：** 任一验收未满足时不创建 tag；若远端同名 tag 已存在且目标不同，停止并人工审计，禁止强推覆盖。节点没有 GitHub credential 时保留本地 tag 并把 remote publication 标为 `BLOCKED_CREDENTIALS`，不得冒充已 push。

```bash
cd /users/Mingyang/Micro_banch
TAG=physical-integration-v1
test -z "$(git status --porcelain)"
test -z "$(git tag -l "$TAG")"
git tag -a "$TAG" -m 'WP1 dual-node physical integration baseline'
git push origin "$TAG"
test "$(git rev-list -n1 "$TAG")" = "$(git rev-parse HEAD)"
```

## 9. WP1 完成后的硬停止线

WP1 tag 只证明 integration/build/test baseline，不证明网络实验、性能或论文主张。只有以下两项都 PASS 才允许进入 WP2/WP3：

1. 两机 manifest 明确显示 2026-07-25 的精确 expiration；
2. S3 upload/remote-stream-SHA/download/local-SHA/delete 闭环 PASS。
