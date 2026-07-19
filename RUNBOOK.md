# RescueSched WP0/WP1/WP2/WP3 操作手册

> 本文覆盖租期/S3 门禁、integration baseline、WP2 代码级验证，以及 WP3 可恢复 host profile 的 capture/validate/dry-run/apply/verify/restore 入口。2026-07-19 已冻结 `T_expire=2026-07-24T11:00:00Z` 并在两机完成 WP3 apply/restore 回归；主机最终均恢复到 before 状态。WP4、双机 RPC、pilot、calibration、formal、正式端口 `9000` 和 ring `4096` 条件继续停止。所有时间使用 UTC；所有 secret 必须位于仓库外。

## 0. 主机约定与 fail-closed 规则

| 名称 | 角色 | control path | experiment path | repo |
| --- | --- | --- | --- | --- |
| node0 | server | 本机 / `amd140.utah.cloudlab.us` | `10.10.1.1` | `/users/Mingyang/Micro_banch` |
| node1 | coordinator/client | management SSH alias（本轮为 `node1-control`）/ `amd136.utah.cloudlab.us` | `10.10.1.2` | `/users/Mingyang/Micro_banch` |

全局成功条件：命令 exit code 0、要求的 gate status 明确、两机 identity 一致且输出被保留。恢复入口：任何一步失败即停止后续 gate，保存 stdout/stderr 和唯一 attempt 目录；不删除旧失败证据，不使用 `reset --hard` 掩盖未知修改。`DEFERRED`/`WAIVED` 不得写成数据面或远端发布 PASS。WP3 只允许修改 experiment NIC `enp65s0f0np0`；control NIC `eno33np0` 只能做只读连通性核验。

## 1. WP0.1：实时租期采集与精确 UTC freeze

**Host：node0 和 node1，分别实时执行。Input：** management SSH 可用。**成功条件：** 每机完整原始 manifest、开始/结束 UTC、hostname、exit code、manifest SHA256 和 expiration 均落盘；若 manifest 与 live status 冲突，必须保留冲突，并以明确时区语义的权威 live source 冻结唯一 UTC。**停止条件：** 两机 sliver 不一致、来源时区仍不明确、节点不可达或剩余窗口不足时，WP3 保持 `BLOCKED_FOR_WP3`，不得执行 host tuning。

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="physical-results/wp0-lease-$STAMP"
mkdir -p "$OUT/node0" "$OUT/node1"

# 每台机器都要把开始/结束 UTC、stdout、stderr 和 exit code 分开保存；
# 不得复用旧 manifest。以下为节点本地采集核心命令。
date -u +%Y-%m-%dT%H:%M:%SZ > "$OUT/node0/manifest.started_at_utc.txt"
set +e
geni-get -n manifest > "$OUT/node0/manifest.xml" 2> "$OUT/node0/manifest.stderr.txt"
rc=$?
set -e
printf '%s\n' "$rc" > "$OUT/node0/manifest.exit_code.txt"
date -u +%Y-%m-%dT%H:%M:%SZ > "$OUT/node0/manifest.completed_at_utc.txt"
hostname -f > "$OUT/node0/hostname.txt"
sha256sum "$OUT/node0/manifest.xml" > "$OUT/node0/manifest.sha256"

# 补充来源同样必须实时采集并保存原始输出；其时区语义必须另有权威依据。
geni-get -n status > "$OUT/node0/status.raw" 2> "$OUT/node0/status.stderr.txt"
sha256sum "$OUT/node0/status.raw" > "$OUT/node0/status.sha256"
```

node1 使用管理面 SSH 在远端执行相同采集；不要把 SSH 私钥或配置内容写入证据。若使用临时 SSH config，日志只记录别名、hostname、exit code 和 route，不记录 `IdentityFile`。

2026-07-19 的实际结果位于：

```text
physical-results/wp0-wp3-20260719T070542Z/wp0.1/
```

两机实时 manifest 仍为 `2026-07-18T03:00:00Z`，SHA256 均为 `ca30afc0937b830d2dbaf6594ea7af878ed368e150df54901256b5ece72a27a8`，因此 manifest 子检查保留为 `BLOCKED_STALE_OR_NON_PROPAGATED_VALUE`。两机实时 `geni-get -n status` 原始 `geni_expires` 均为 `2026-07-24 05:00:00`，status SHA256 均为 `1743b02feed164f002441a93ce6461991046b3eb7bf0442aab5a89d0622e8a12`。实时 Utah CloudLab 官方站点给出 `-0600`，保存的 exact deployed source `0b1fdb15cd434591f3fab98799d78189698686fc` 与站点配置证明该字段使用 `America/Denver` 本地时间，因此冻结：

```text
raw live status = 2026-07-24 05:00:00 America/Denver (MDT, -0600)
T_expire = 2026-07-24T11:00:00Z
T_no_new_block = 2026-07-23T23:00:00Z
decision_at_utc = 2026-07-19T07:28:13Z
remaining_at_decision = 5d 03h 31m 47s
status = PASS_EXACT_UTC_FROZEN_FOR_WP3
```

旧 Portal `Jul 24, 2026 7:00 PM` 截图没有时区，只保留为历史补充事实，不作为精确 UTC authority。该 PASS 只授权 WP3 apply/restore；不授权 WP4、pilot 或 formal。

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

2026-07-19 的实时非敏感检查位于 `physical-results/wp0-wp3-20260719T070542Z/wp0.2/`：两机 `~/.config/rescuesched/rclone.conf` 均不存在，expected remote、private bucket 和 project-dedicated prefix 均缺失。状态为 `DEFERRED_UNTIL_PILOT_BLOCKED_MISSING_INPUTS`；upload、remote stream SHA、download、download SHA、delete 和 absence confirmation 全部 `NOT_RUN`。它不阻塞 WP3，但继续硬阻塞 WP4/pilot/formal。

S3 gate 日志只能包含无 secret 的非敏感标识和 hashes；不得运行 `rclone config show`，不得把 credential 放入参数、Git、证据或聊天。缺少输入时只能列缺失项，不能写成 PASS。

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

## 10. WP3 可恢复 host profile 操作入口

本节只执行 host profile discovery/apply/restore，不启动任何 workload。两机 profile：

```text
config/host-profiles/infocom2027-node0.env
config/host-profiles/infocom2027-node1.env
```

node0 profile SHA256：`50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b`；node1 profile SHA256：`fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e`。执行前必须再次只读核对 lease gate、clean/same branch+commit 和冻结 tag object/target。

每台机器必须严格按以下事务顺序使用新的唯一 attempt 目录；**dry-run 必须早于 final restore plan**：

```bash
cd /users/Mingyang/Micro_banch
PROFILE=config/host-profiles/infocom2027-node0.env   # node1 改为 node1 profile
ATTEMPT="physical-results/wp3-$(hostname -s)-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$ATTEMPT"

scripts/capture_physical_host_state.sh \
  --out-dir "$ATTEMPT/before" --label before --profile "$PROFILE"

python3 scripts/validate_host_profile.py \
  --profile "$PROFILE" --repo-root "$PWD" --out-dir "$ATTEMPT/validator"

scripts/apply_host_profile.sh \
  --profile "$PROFILE" --log-dir "$ATTEMPT/dry-run" --dry-run

scripts/prepare_host_restore_plan.sh \
  --profile "$PROFILE" --out-dir "$ATTEMPT/restore-plan"

bash -n scripts/restore_host_state.sh
scripts/restore_host_state.sh --plan "$ATTEMPT/restore-plan" --check-plan

# Atomic apply validates the prepared independent plan. If apply fails after mutation,
# the apply entry invokes the independent restore path and the attempt must stop.
scripts/apply_host_profile.sh \
  --profile "$PROFILE" --log-dir "$ATTEMPT/apply" \
  --plan "$ATTEMPT/restore-plan"

scripts/verify_host_profile.sh \
  --profile "$PROFILE" --plan "$ATTEMPT/restore-plan" \
  --mode effective --out-dir "$ATTEMPT/effective-verify"

# 另开 management SSH/control-plane 检查：hostname、route、branch、HEAD、clean、tag。
# route 必须继续使用 eno33np0；不得修改或关闭 control NIC。

scripts/restore_host_state.sh \
  --plan "$ATTEMPT/restore-plan" --log-dir "$ATTEMPT/restore-1"

scripts/verify_host_profile.sh \
  --profile "$PROFILE" --plan "$ATTEMPT/restore-plan" \
  --mode restored --out-dir "$ATTEMPT/restored-verify-1"

# 第二次独立 restore 是幂等性门禁。
scripts/restore_host_state.sh \
  --plan "$ATTEMPT/restore-plan" --log-dir "$ATTEMPT/restore-2"
scripts/verify_host_profile.sh \
  --profile "$PROFILE" --plan "$ATTEMPT/restore-plan" \
  --mode restored --out-dir "$ATTEMPT/restored-verify-2"
```

成功条件：topology validator PASS、dry-run PASS、apply/effective verification PASS、management SSH PASS、第一次 restore PASS、第二次 restore 为 `PASS_ALREADY_RESTORED` 且 `mutation_count=0`、before/restored semantic diff 和 control diff 均为空。restore 不能只依赖 shell trap；禁止 reboot/reset 掩盖失败。

2026-07-19 已完成的 attempts：

```text
physical-results/wp0-wp3-20260719T070542Z/wp3/node0/attempt-20260719T080242Z/
physical-results/wp0-wp3-20260719T070542Z/wp3/node1/attempt-20260719T080344Z/
```

两机 effective verification 均为 89/89，第一次 restore PASS，第二次 restore 均为 `PASS_ALREADY_RESTORED`、mutation 0；before/restored、idempotency 和 control diff 均为 0 bytes。management SSH 在 effective/restored 后保持可用并经 `eno33np0`。本次没有运行 workload。

## 11. WP3 完成后的硬停止线

WP3 的 `PASS_APPLY_EFFECTIVE_RESTORE_REGRESSION_ON_BOTH_NODES` 只证明主机 profile 能安全 apply、核验并恢复，不证明双机网络实验、性能或论文主张。主机已恢复到 before 状态。

仍然禁止：

1. WP4、双机 RPC smoke、pilot、scheduler period calibration、arrival calibration 和 formal experiment；
2. 使用正式实验端口 `9000`、正式结果目录或 NIC ring `4096` pilot 条件；
3. 在 S3 gate 未完成前进入 WP4/pilot/formal；
4. 把 S3 `DEFERRED` 写成 PASS，或把 GitHub `WAIVED` 写成已 push；
5. 把旧 manifest 子检查改写成 PASS，或忽略其 stale/non-propagated 事实；
6. 把 WP2 loopback/synthetic 或 WP3 host transaction 写成双机或正式性能证据。

进入 WP4/pilot/formal 前必须完成 S3 upload/remote-stream-SHA/download/local-SHA/delete/absence-confirmation 闭环，并获得明确的新授权；当前任务到 WP3 恢复与证据封存为止。
