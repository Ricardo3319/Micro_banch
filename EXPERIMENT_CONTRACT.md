# RescueSched INFOCOM 2027 物理实验合同

> `contract_version: infocom2027-physical-v0.1`
> `contract_status: DRAFT_UNFROZEN`
> WP1 建立合同结构；带 `PENDING` 或 `null` 的字段必须在对应工作包中冻结后提升版本。任何正式结果可见后的合同变化都要求重跑所有受影响 paired blocks。

## 1. 研究边界

物理实验验证稳定 flow/RSS 初始放置下的瞬时 per-core FIFO 不平衡，以及只迁移未开始 queued descriptor 的 RescueSched 是否改善 server-side deadline violation rate 和 SLO goodput。不得把 local replay、loopback、short smoke、handoff microbenchmark 或物理机上的 simulator 结果称为正式物理证据。

主指标：

1. server-side deadline violation rate；
2. SLO goodput。

次要指标：server completion P50/P99/P999、client RTT P50/P99/P999、throughput、migrations、migrated work、handoff 和 scheduler overhead。W3 低负载边界和 W2 burst/tail 边界无论方向均报告；物理结果与仿真相反时如实报告。

## 2. 固定方法与共享机制

```text
L0_RandomCore
L1_WorkStealingPolling
M0_AltoThreshold
M1_RescueSched
```

四方法共享 UDP wire protocol、`SO_REUSEPORT` ingress shards、worker 数与 CPU 列表、queue/estimator/handoff primitive、trace bytes、flow/source-port 规则、日志 schema、host profile 和同一 anchor 的 arrival scale。

策略边界：

- `L0_RandomCore`：保留 kernel-selected ingress worker；
- `L1_WorkStealingPolling`：idle worker 通过公共 handoff primitive 拉取 queued work；
- `M0_AltoThreshold`：队列超过阈值且预测改善时迁移 bounded-prefix candidate；
- `M1_RescueSched`：仅当本地预计 late、目标预计 feasible 且 bounded destination prefix 安全时迁移。

Service 非抢占；只有 `QUEUED` descriptor 可迁移；所有迁移 append 到目标 FIFO tail。完成后更新 method-keyed EWMA，alpha 固定为 `0.05`。policy 不得读取当前请求隐藏的真实 service demand。

## 3. 正式矩阵

| Anchor ID | Workload | rho label | 角色 | Seeds | 方法数 | 有效 method-runs |
| --- | --- | ---: | --- | ---: | ---: | ---: |
| `W3-rho085` | W3 | 0.85 | primary confirmation | 10 | 4 | 40 |
| `W3-rho090` | W3 | 0.90 | primary confirmation | 10 | 4 | 40 |
| `W3-rho070` | W3 | 0.70 | negative boundary | 10 | 4 | 40 |
| `W2-rho085` | W2 | 0.85 | burst/tail boundary | 10 | 4 | 40 |

固定 seeds：

```text
11, 23, 37, 47, 59, 71, 83, 97, 109, 127
```

固定 cohort：

```text
warmup_requests: 200000
measurement_requests: 1000000
```

一个 `anchor × seed` 的四方法构成不可拆分 paired block。四方法必须消费 byte-identical trace。block 内方法顺序由 `scripts/generate_cloudlab_run_order.py` 生成并归档；不得因结果方向改变顺序、seed 或停止时点。

## 4. 两机角色与网络

| Node | 角色 | control hostname | experiment IPv4 | experiment NIC |
| --- | --- | --- | --- | --- |
| node0 | server/main experiment | `amd140.utah.cloudlab.us` | `10.10.1.1` | `enp65s0f0np0` |
| node1 | load generator/observer/coordinator | `amd136.utah.cloudlab.us` | `10.10.1.2` | `enp65s0f0np0` |

SSH/control traffic 走 management network；RPC 只能走 experiment network。node0 不运行 client；所有正式操作由 node1 coordinator 发起。

计划 UDP/flow 参数（WP5 前仍为 `PLANNED`）：

```yaml
server_port: 9000
source_port_base: 20000
client_count: 2
flow_sockets_per_client: 256
partition_rule: "flow_id % client_count"
shared_absolute_start: true
```

两个 clients 接收同一 `source_port_base`，client 根据 `client_index * flow_sockets` 自动形成不重叠端口区间。

## 5. CPU、IRQ 与 NIC 合同

### node0 计划布局（WP3 未 apply）

```text
workers:          CPU 0-15
receivers:        CPU 16-17
response senders: CPU 18-19
scheduler:        CPU 20
server main:      CPU 21
housekeeping:     CPU 22-23
experiment IRQs: CPU 24-31
```

实际 topology 为 16 physical / 32 logical，CPU 16-31 是 CPU 0-15 的 SMT siblings。worker 集合 `0-15` 每个 physical core 唯一；control/IRQ 逻辑 CPU 与 worker 共享 sibling core。正式使用前 topology validator 必须验证并记录该事实。禁止使用旧示例 `0,2,4,...,30`，因为它只覆盖 8 个 physical cores。

### node1 计划布局（WP3 未 apply）

```text
client-0 sender:   CPU 0
client-0 receiver: CPU 1
client-1 sender:   CPU 2
client-1 receiver: CPU 3
coordinator:       CPU 4
monitor:           CPU 5
post-run archive:  CPU 6-7
experiment IRQs:   CPU 8-15
CPU 16-31:         no experiment threads
```

### 计划 host profile（WP3 未冻结）

- node0 governor `performance`；node1 若无 cpufreq policy，必须记录而非伪造；
- `irqbalance` stopped；experiment NIC combined queues `8`；
- RPS off；XPS 仅 response/control CPUs；GRO/GSO/TSO off，LRO off，RX/TX checksum on；
- requested socket buffer `8 MiB`；`rmem_max/wmem_max=16777216`；`netdev_max_backlog=250000`；
- `ulimit -n=65536`；ring 默认 `1024`，只有预注册 pilot 条件满足才可改为 `4096`；
- 每次 apply 必须 before capture → validate → apply → verify → run → restore → restore verify。

`host_profile_sha256` 当前为 `null`。

## 6. 未冻结 runtime 参数

| 参数 | 当前值 | 冻结工作包 |
| --- | --- | --- |
| scheduler period | `null`；候选 10/25/50/100 us | WP4 |
| L1 poll period | `null` | WP4 |
| handoff estimate | `null` | WP5 |
| W3-rho085 arrival scale | `null` | WP5 |
| W3-rho090 arrival scale | `null` | WP5 |
| W3-rho070 arrival scale | `null` | WP5 |
| W2-rho085 arrival scale | `null` | WP5 |
| decision logging mode/cap/bucket | `PENDING`/`null` | WP2/WP5 |
| worker mode | primary 16, fallback 12 | WP4 |

12-worker fallback 只能在 `2026-07-20T12:00:00Z` 前、所有 16-worker period candidates 连续失败且原因属于已登记基础设施条件时触发。触发后必须重新冻结 CPU/IRQ/NIC/arrival scale/handoff/period 并全量统一运行，禁止混用 16/12-worker 正式数据。

## 7. Calibration 与统计

Trace rho 只是源标签，不等于 realized physical load。使用非正式 seeds `1009, 1013` 做 infrastructure pilot 和 arrival calibration；每个 anchor 冻结一个 scale，四方法共用。不得根据 deadline/tail 或方法效果列校准。

正式统计：

- seed 是 paired unit；
- 比较 `baseline violation rate - M1 violation rate`，分别做 L1 vs M1、M0 vs M1；
- paired bootstrap 95% CI，10,000 draws；analysis random seed 在分析 manifest 中冻结；
- 效率约束：M1 median migrated-work rate 不高于比较 baseline；
- 10 seeds 全部执行，不因 CI 或结果方向提前停止。

## 8. 有效性、失败和重跑

### `VALID_COMPLETE`

请求/响应完整；commit/trace/config/flow-map/schedule/profile SHA 一致；duplicate/unknown/malformed 为零；affinity 和 runtime invariants PASS；response queue 无 overflow；输出完整可解压；archive 与 remote streaming SHA 一致。

### `VALID_APPLICATION_OVERLOAD`

请求正确接受，但真实应用负载导致 drain/timeout；无基础设施故障或数据损坏。它是有效结果，禁止因不利重跑。

### `INFRASTRUCTURE_FAILURE`

包括 node/kernel/NIC/control loss、identity mismatch、affinity/topology failure、packet corruption/drop、malformed/duplicate/unknown response、response queue overflow、runtime invariant failure、scheduler catch-up storm、ingress stall、输出缺失/损坏/SHA mismatch 或 host-profile drift。

### `INCOMPLETE`

用户/lease/进程中止、未完成四方法、未完成验证/归档/remote verify。不得作为有效 block。

任一方法 infrastructure failure 会使整个 paired block 成为失败 attempt；保留全部日志，修复后从第一个方法开始新 attempt。最多 3 个 infrastructure attempts，采用第一个完整有效 attempt，禁止跨 attempt 拼接四方法。

## 9. Formal validator 阈值

```yaml
expected_request_count_complete: true
send_failures_max: 0
malformed_requests_max: 0
unknown_requests_max: 0
duplicate_requests_max: 0
response_queue_overflow_max: 0
nic_drop_delta_max: 0
udp_drop_delta_max: 0
offered_load_relative_error_max: 0.02
realized_rho_absolute_error_max: 0.01
send_lag_p99_us_max: 4.0
scheduler_missed_epoch_fraction_max: 0.05
all_identity_and_schema_checks: PASS
```

在 node1 无可用 cpufreq policy 时，不直接判失败；以实际频率样本、send failure、send-lag、offered load 和 request completeness 判定。

## 10. 输出与证据

当前 runtime candidate 输出 schema 为 `rescuesched-physical-v1-candidate`，正式 schema 在 WP2/WP5 冻结。method 至少保留：

```text
manifest.env
RUNTIME_STATUS.txt / RPC_SERVER_STATUS.txt / RPC_CLIENT_STATUS.txt
requests.csv
client_requests.csv
client_summary.csv
decisions.csv
migrations.csv
summary.csv
stdout.log
stderr.log
command.txt
SHA256SUMS
```

正式 block 另含 trace/schedule/config/flow-map/profile hashes、validator report、attempt classification、deterministic archive SHA 和 remote object verification。raw 在 remote SHA PASS 前不得删除。
