# RescueSched CloudLab Quickstart

> **状态：integration baseline quickstart。** 根目录 `PROJECT_STATE.md` 是当前门禁来源；完整操作、成功条件和恢复入口见根目录 `RUNBOOK.md`。当前 real UDP RPC runtime 已存在，但 WP0 lease/S3 gate 未通过，所以不得启动 host tuning、pilot、calibration 或 formal matrix。

## 1. 节点与网络

| Node | Role | SSH/control | RPC/experiment |
| --- | --- | --- | --- |
| node0 | server | `amd140.utah.cloudlab.us` / alias `node0` | `10.10.1.1` |
| node1 | client/coordinator | `amd136.utah.cloudlab.us` / alias `node1` | `10.10.1.2` |

RPC 必须使用实验地址；hostname/alias 仅用于 SSH control。不要把 private key、AWS key 或 rclone config 放进仓库。

## 2. 先检查 WP0

在 node0 采集两机 uncached manifest：

```bash
cd /users/Mingyang/Micro_banch
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="physical-results/wp0-lease-$STAMP"
mkdir -p "$OUT/node0" "$OUT/node1"
geni-get -n manifest > "$OUT/node0/manifest.xml"
ssh -o BatchMode=yes node1 'geni-get -n manifest' > "$OUT/node1/manifest.xml"
sha256sum "$OUT"/node*/manifest.xml > "$OUT/MANIFEST_SHA256SUMS"
grep -oE 'expires="[^"]+"' "$OUT"/node*/manifest.xml
```

只有两机明确显示 2026-07-25 的精确 expiration 且 S3 可恢复性闭环 PASS，才能继续正式物理工作。2026-07-17T16:10:11Z 的实际采集仍显示两机 `2026-07-18T03:00:00Z`。

## 3. 部署同一 integration commit

node0：

```bash
cd /users/Mingyang/Micro_banch
git fetch --prune origin
git switch codex/infocom2027-integration
git pull --ff-only origin codex/infocom2027-integration
COMMIT="$(git rev-parse HEAD)"
test -z "$(git status --porcelain)"
printf '%s\n' "$COMMIT"
```

node1：

```bash
cd /users/Mingyang/Micro_banch
git fetch --prune origin
git switch --force-create codex/infocom2027-integration \
  origin/codex/infocom2027-integration
test -z "$(git status --porcelain)"
git rev-parse HEAD
```

两机 full SHA 必须完全相同。

## 4. Release build/test

两机分别执行：

```bash
cd /users/Mingyang/Micro_banch
BUILD="build-integration-$(date -u +%Y%m%dT%H%M%SZ)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
```

WP1 gate 要求两机 `24/24` tests PASS、working tree clean、commit 相同。

## 5. 实现级检查（非正式证据）

WP0 未通过时，只允许在明确授权范围内执行不会修改 host profile 的 implementation gate：

```bash
bash scripts/run_local_rpc_smoke.sh --build-dir <release-build-dir>
bash scripts/run_local_physical_runtime_smoke.sh --build-dir <release-build-dir>
bash scripts/run_pinned_handoff_microbench.sh --build-dir <release-build-dir>
```

它们不构成论文-facing physical result。

## 6. 两节点 RPC smoke 入口

WP0 通过、两机同 commit 且 topology validator 确认后，才从 node1 使用：

```bash
bash scripts/run_two_node_rpc_smoke.sh \
  --server-host node0 \
  --server-ip 10.10.1.1 \
  --server-repo-dir /users/Mingyang/Micro_banch \
  --build-dir <release-build-dir> \
  --workers 16 \
  --cpus 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 \
  --flow-sockets 256 \
  --workload W3 --rho 0.85 --seed 11 \
  --warmup 500 --requests 5000
```

CPU 16-31 是 CPU 0-15 的 SMT siblings；禁止使用旧的 `0,2,4,...,30` worker 列表。

## 7. 权威导航

- 根目录 `PROJECT_STATE.md`：实时门禁和阻断。
- 根目录 `RUNBOOK.md`：WP0/WP1 完整命令与恢复入口。
- `docs/CLOUDLAB_RUNBOOK.md`：物理 runtime 操作流程。
- `docs/CLOUDLAB_PREREGISTRATION.md`：矩阵、失败和统计边界。
- `docs/PHYSICAL_RUNTIME_CONTRACT.md`：descriptor/RPC/output 语义。
- `config/infocom2027-physical.yaml`：机器可读合同。
