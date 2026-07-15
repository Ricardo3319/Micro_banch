# RescueSched Physical Machine Runbook

本文档用于在 CloudLab 或独立 Ubuntu 裸机上直接执行当前仓库能够支持的
物理机预检。命令不会生成图，也不会运行完整 corrected full matrix。

当前提交只包含离散事件模拟器和一个进程内 descriptor handoff 微基准，尚未
包含真实 RPC server、物理 worker runtime 或 trace replay loader。因此本手册
第一部分能够立即执行；第二部分列出的真实迁移实验必须等 physical runtime
实现后才能开始，不能把 `rescue-main` 的输出描述为物理迁移结果。

## 1. 推荐机器

- Ubuntu 22.04 或 24.04 裸机，不使用共享虚拟机。
- 至少 8 个物理 core，推荐 16 个或更多。
- 第一阶段只需要 1 台机器，用于构建、测试和 handoff 标定。
- 真实 RPC 阶段建议 2 台机器：1 台 client/load generator，1 台 server。
- CloudLab 可优先选择带 25G NIC 的 `c6525-25g` 或同等级裸机；若该型号
  不可用，记录实际硬件型号即可。

创建 CloudLab experiment 后，等待节点状态变为 `Ready`，然后 SSH 登录服务端：

```bash
ssh -A <cloudlab-user>@<server-hostname>
```

以下命令都在 server 节点执行。

## 2. 安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
  git build-essential cmake ninja-build python3 \
  tmux numactl hwloc ethtool iproute2 sysstat chrony
```

绘图不是物理机预检的一部分，不需要安装 `matplotlib`、pandas 或 scipy。

检查时间同步和机器基本状态：

```bash
timedatectl status || true
chronyc tracking || true
uname -a
lscpu
numactl --hardware
```

## 3. 固定 CPU 频率策略

如果 CloudLab profile 允许修改 governor，先切换为 `performance`：

```bash
if compgen -G '/sys/devices/system/cpu/cpufreq/policy*/scaling_governor' >/dev/null; then
  for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    echo performance | sudo tee "$policy/scaling_governor"
  done
fi
```

确认结果：

```bash
for governor in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
  printf '%s: ' "$governor"
  cat "$governor"
done 2>/dev/null || true
```

不要在第一轮实验中关闭 SMT、Turbo Boost、NUMA balancing 或修改 IRQ
affinity。先保持平台默认值并完整记录；后续只通过预注册的 sensitivity run
改变这些设置。

## 4. 克隆并固定版本

本轮 Linux corrected evaluation 的代码基线是：

```text
ba81d825eaf1e0b6701e21dbb6462c2a801da0b9
```

推荐先把本手册和 `scripts/run_physical_preflight.sh` 提交并推送到实验分支。
物理机随后克隆该分支，并把克隆时的 HEAD 冻结为本次实验提交：

```bash
cd ~
git clone --branch codex/rescuesched-baselines \
  https://github.com/Ricardo3319/Micro_banch.git
cd Micro_banch

git fetch origin codex/rescuesched-baselines
EXPERIMENT_COMMIT="$(git rev-parse origin/codex/rescuesched-baselines)"
git checkout --detach "$EXPERIMENT_COMMIT"

echo "$EXPERIMENT_COMMIT"
git status --short
```

将 `EXPERIMENT_COMMIT` 的输出保存到实验记录中。使用 detached HEAD 是为了
避免实验过程中远程分支移动导致代码版本变化。正式实验应保证
`git status --short` 无输出。

如果暂时不提交这两个新文件，也可以从开发机临时复制脚本。先在物理机克隆并
固定原代码基线 `ba81d82`，再从开发机执行：

```bash
scp /home/ricardo/projects/Micro_banch/scripts/run_physical_preflight.sh \
  <cloudlab-user>@<server-hostname>:~/Micro_banch/scripts/
```

这种临时方式会使物理机工作区出现一个未跟踪脚本，只适合环境预检，不适合
正式论文实验。正式实验必须使用已提交的脚本版本。

## 5. 一键执行物理机预检

建议在 `tmux` 中执行：

```bash
tmux new -s rescuesched-preflight

EXPECTED_COMMIT="$(git rev-parse HEAD)"
bash scripts/run_physical_preflight.sh \
  --expected-commit "$EXPECTED_COMMIT" \
  --require-clean \
  2>&1 | tee physical-preflight-console.log
```

按 `Ctrl+B`，再按 `D` 可以退出但保持命令运行。重新进入：

```bash
tmux attach -t rescuesched-preflight
```

脚本自动执行：

1. 校验 Git 提交号并记录工作区状态。
2. 记录 OS、kernel、CPU、NUMA、SMT、governor、NIC 和工具链。
3. 使用 Release 模式配置和构建。
4. 执行全部 CTest。
5. 执行 deterministic `rescue-smoke`。
6. 执行 3 次 descriptor handoff 微基准。
7. 检查三次 handoff 结果的相对极差是否不超过 25%。
8. 执行 W3、`rho=0.85`、seed 11 的短锚点仿真和 schema 校验。
9. 对结果目录中的文件生成 SHA-256 校验和。

脚本不覆盖 `artifacts/step-20-*` 或 `artifacts/step-21-*`，默认结果写入：

```text
physical-results/preflight-<UTC timestamp>-<hostname>/
```

## 6. 检查预检结果

找到最新结果目录：

```bash
RESULT_DIR="$(find physical-results -maxdepth 1 -type d \
  -name 'preflight-*' | sort | tail -n 1)"
echo "$RESULT_DIR"
```

检查总状态和微基准状态：

```bash
cat "$RESULT_DIR/PREFLIGHT_STATUS.txt"
cat "$RESULT_DIR/MICROBENCH_STATUS.txt"
cat "$RESULT_DIR/microbench_summary.csv"
```

成功时应看到：

```text
status=PASS
```

进一步检查：

```bash
cat "$RESULT_DIR/metadata/manifest.env"
cat "$RESULT_DIR/metadata/git-status.txt"
tail -n 30 "$RESULT_DIR/logs/ctest.log"
tail -n 20 "$RESULT_DIR/logs/rescue-smoke.log"
tail -n 20 "$RESULT_DIR/logs/short-anchor-schema.log"
```

预检通过标准：

- Git 提交与 `EXPECTED_COMMIT` 一致，且正式运行时工作区干净。
- CMake Release 构建成功。
- CTest 全部通过。
- smoke 输出 `RescueSched smoke status: PASS`。
- 三次 handoff 微基准相对极差不超过 25%。
- 短锚点 CSV 通过 `rescuesched-v2` schema 校验。

短锚点只验证执行链路、四方法输出和 CSV schema。其样本量不足以用于性能优劣
判断，也不能替代 corrected full evaluation。

如果只有微基准稳定性失败，先确认机器空闲、governor 固定、没有其他实验占用
CPU，然后重新运行到新的结果目录。不要删除失败结果，失败结果也是环境审计
的一部分。

## 7. 可选：运行四个仿真锚点

这一步仍然是物理机上的模拟器执行，不是真实物理迁移实验。它用于确认更换
机器后论文主线方向没有异常，不生成图。

```bash
cd ~/Micro_banch

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$(hostname -s)"
ANCHOR_DIR="physical-results/simulator-anchors-$RUN_ID"
mkdir -p "$ANCHOR_DIR"

SEEDS='11,23,37,47,59,71,83,97,109,127'

./build/simulator --mode rescue-main \
  --workload W3 --rho 0.70,0.85,0.90 --seed "$SEEDS" \
  --warmup-requests 200000 --measurement-requests 1000000 \
  --output "$ANCHOR_DIR/w3.csv" \
  2>&1 | tee "$ANCHOR_DIR/w3.log"

./build/simulator --mode rescue-main \
  --workload W2 --rho 0.85 --seed "$SEEDS" \
  --warmup-requests 200000 --measurement-requests 1000000 \
  --output "$ANCHOR_DIR/w2.csv" \
  2>&1 | tee "$ANCHOR_DIR/w2.log"

python3 tests/integration/validate_rescue_csv_schema.py \
  "$ANCHOR_DIR/w3.csv" "$ANCHOR_DIR/w2.csv" \
  2>&1 | tee "$ANCHOR_DIR/schema.log"

sha256sum "$ANCHOR_DIR"/*.csv > "$ANCHOR_DIR/SHA256SUMS"
```

这组命令覆盖论文计划中的四个锚点：W3 `rho=0.70/0.85/0.90` 和 W2
`rho=0.85`，每点 10 个 paired seeds。

## 8. 归档和下载结果

在物理机打包：

```bash
cd ~/Micro_banch
RESULT_DIR="$(find physical-results -maxdepth 1 -type d \
  -name 'preflight-*' | sort | tail -n 1)"
ARCHIVE="$(basename "$RESULT_DIR").tar.gz"
tar -czf "$ARCHIVE" "$RESULT_DIR" physical-preflight-console.log
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
ls -lh "$ARCHIVE" "$ARCHIVE.sha256"
```

在本地电脑下载，命令中的远程路径按实际用户名修改：

```bash
scp <cloudlab-user>@<server-hostname>:~/Micro_banch/preflight-*.tar.gz .
scp <cloudlab-user>@<server-hostname>:~/Micro_banch/preflight-*.tar.gz.sha256 .
sha256sum -c preflight-*.tar.gz.sha256
```

## 9. 真实物理迁移实验的停止线

完成上述步骤后，可以确认物理机环境、模拟器和 handoff 微基准可用，但此时
必须停止使用“真实 RPC 实验”“物理迁移收益”或“CloudLab 系统验证”等表述。

开始真实物理实验前，仓库至少需要新增并通过验收：

1. 单机多 worker 的真实请求 runtime，以及明确的 CPU affinity。
2. W3 请求生成器或冻结 trace replay loader。
3. 不读取当前请求真实 service time 的在线 estimator。
4. `L0_RandomCore`、polling work stealing、ALTO-style threshold 和
   `M1_RescueSched` 的同运行时实现。
5. request、decision、migration、scheduler overhead 和 deadline miss 日志。
6. 自动生成的 run manifest 和物理 summary CSV。
7. 同一冻结 trace 下的方法配对执行和物理结果比较脚本。

真实 runtime 完成后，第一轮只运行以下四个预注册锚点，不立即扩展矩阵：

- W3 `rho=0.85`
- W3 `rho=0.90`
- W3 `rho=0.70`，保留低负载反例
- W2 `rho=0.85`，保留 tail amplification 边界

每个方法至少执行 10 次 paired trace repetitions，并分别报告 server-side
deadline violation、goodput、P50/P99/P999、迁移请求和迁移工作量、CPU 周期、
cache miss、NUMA movement 以及端到端 RTT。

## 10. 常见失败

`ninja` 不可用：

```bash
sudo apt-get install -y ninja-build
```

没有 governor 文件：平台可能不暴露 CPU frequency control。保留
`host-inventory.txt` 中的记录，不要伪造 `performance` 状态。

微基准抖动超过 25%：

```bash
uptime
mpstat -P ALL 1 10
ps -eo pid,psr,pcpu,comm --sort=-pcpu | head -n 20
```

确认没有其他负载后重新运行。若连续失败，记录该节点不适合作为正式实验节点，
更换节点或增加 CPU affinity 后重新预注册测试条件。

CTest 或 smoke 失败：保留完整结果目录，不继续运行锚点或论文实验。先根据
`logs/ctest.log`、`logs/rescue-smoke.log` 修复环境或代码问题。
