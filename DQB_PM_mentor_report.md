# DQB-PM 当前算法与实验结果汇报

日期：2026-05-08

## 1. 项目背景：我们在解决什么问题

本项目研究的是 **微秒级 RPC 系统中的尾延迟优化问题**。

在大规模 RPC 服务中，请求会被分发到不同机器和不同 core 上执行。即使整体平均负载不算高，某些机器或 core 也可能因为短时间 burst、长任务阻塞、异构慢节点等原因形成局部队列积压。这些局部积压会显著拉高 P99、P999 等尾延迟。

本项目关注的问题可以概括为：

```text
当某些请求在本地队列中即将产生较大尾延迟时，
系统能否主动把它们迁移到更合适的目标机器，
从而降低整体 P99/P999 和 SLO violation？
```

这里的难点是：系统运行在微秒级时间尺度，调度决策必须非常轻量，不能依赖昂贵的全局实时信息。

### 1.1 为什么不能简单做全局最优调度

理论上，如果调度器能实时知道所有机器、所有 core、所有队列的真实状态，就可以做接近全局最优的调度。但真实系统中很难做到这一点：

| 难点 | 说明 |
|---|---|
| 状态同步太慢 | 微秒级系统中，队列状态变化很快，远端实时状态很容易过期 |
| 全局决策开销高 | 全局扫描、排序和优化不适合高频调度 |
| 远端拥塞不可忽视 | 多个源节点可能同时迁移到同一个目标节点 |
| 迁移不是免费操作 | 网络转发、host 处理、元数据更新都会带来开销 |
| 高负载时迁移可能负收益 | 系统整体饱和时，迁移只是在机器之间转移排队压力 |

因此，本项目的核心约束是：

```text
每个 host 可以读取自己的本地队列，
但不能读取远端 host 的实时队列。
远端状态只能使用周期同步得到的 stale view。
```

### 1.2 为什么单请求迁移不够

早期主动迁移方法通常以单个请求为决策单位。算法发现某个请求预计会超时，就尝试迁移这个请求。

这种思路很直观，但有明显问题：

| 问题 | 说明 |
|---|---|
| 容易受噪声影响 | 单个请求的服务时间和等待时间估计可能不稳定 |
| 忽视队列结构 | burst 场景中真正的问题往往是一整段队列积压 |
| 目标端容易受害 | 单个迁移看似有收益，多个迁移叠加后可能压垮目标 |
| 控制开销偏高 | 对大量任务逐一评分、逐一选目标，不适合微秒级系统 |
| 高负载下可能反噬 | 系统饱和时迁移不能创造容量，反而可能增加延迟 |

当前算法 DQB-PM 的出发点就是解决这些问题：**把迁移控制单位从单个请求提升为队列中的一个 batch**。

## 2. 仿真系统：当前实验环境

本项目实现了一个 C++ 离散事件仿真器，用来模拟微秒级 RPC 请求的生成、分发、排队、执行、迁移和完成。

### 2.1 基本系统参数

| 参数 | 当前值 |
|---|---:|
| host 数量 | 64 |
| 每个 host 的 core 数量 | 16 |
| 总 core 数 | 1024 |
| 时间单位 | 微秒 us |
| host 内处理开销 `T_host` | 2.1 us |
| 单向网络延迟 `T_net_oneway` | 3.15 us |
| 短任务 SLO | 40 us |
| 长任务 SLO | 200 us |
| warmup 请求数 | 200k |
| measurement 请求数 | 1M |
| 固定随机种子 | `{11, 23, 37, 47, 59}` |

### 2.2 事件模型

仿真器使用离散事件队列。主要事件包括：

| 事件 | 含义 |
|---|---|
| `TASK_GENERATE` | 生成一个新请求 |
| `TASK_ARRIVE` | 请求到达某个 host |
| `TASK_FINISH` | 请求执行完成 |
| `SYNC_LOAD` | 同步 stale load view |
| `CHECK_MIGRATION` | 触发一次迁移检查 |

同一时间戳下的事件优先级是：

```text
TASK_FINISH > TASK_ARRIVE > TASK_GENERATE
```

### 2.3 stale view 约束

每个 host 可以读取自己的本地真实队列，但不能读取远端 host 的实时队列。

远端状态只能来自周期性同步的 stale view：

```text
SYNC_LOAD_PERIOD_US = 10 us
```

迁移检查周期默认是：

```text
M0_T_CHECK_US = 1 us
```

这意味着迁移决策往往比全局同步更频繁，算法必须能在过期信息下做稳健判断。

## 3. 已实现方法：当前有哪些算法可以比较

当前项目已经实现多个方法，用于作为基线和对照。

| 方法 | 全称 | 作用 |
|---|---|---|
| `B0_IdealCFCFS` | Ideal centralized FCFS | 理想全局 FIFO 参考，不是现实可部署算法 |
| `B1_PowerOf2` | Power-of-2 dispatch | 只做初始分发，不做迁移 |
| `B2_Reactive` | Reactive migration | 队列超过阈值后触发被动迁移 |
| `M0_Proactive` | Single-task proactive migration | 对单个请求做预测式主动迁移 |
| `M1_AQB_PM` | Adaptive Queue-Batch PM | 从 task candidates 中做批量选择 |
| `M2_DQB_PM` | Distribution-aware Queue-Batch PM | 当前主算法，基于队列摘要和分布感知 batch |

当前重点汇报的是 `M2_DQB_PM`。

### 3.1 M0 和 M1 的定位

`M0_Proactive` 是单请求主动迁移。它扫描本地等待队列，如果发现某个请求预计在本地会超过风险阈值，就尝试迁移这个请求。

`M1_AQB_PM` 是过渡方案。它仍然从 task candidate 出发，但可以在一次检查中选择多个候选请求，因此比 M0 更接近 batch migration。

但是，M1 的 batch 本质上仍是多个单任务候选的组合，而不是从队列结构中自然形成的 batch。

### 3.2 DQB 和 M1 的核心区别

DQB 与 M1 的区别可以用一句话概括：

```text
M1 是 task-candidate batching；
DQB 是 queue-region batching。
```

也就是说：

- M1 先找危险 task，再把多个 task 组合迁移；
- DQB 先识别危险队列区域，再判断整个区域是否值得迁移。

这使得 DQB 更适合处理 burst 场景中的连续队列压力。

## 4. 当前主算法：DQB-PM 的核心思想

DQB-PM 的全称是：

```text
Distribution-aware Queue-Batch Proactive Migration
```

它的核心思想是：

```text
在线控制路径不逐任务全局排序，
而是扫描本地队列固定前缀，
构造轻量队列摘要，
识别具有共同风险结构的 queue batch，
再在 stale remote view 和 reservation 约束下判断整批迁移是否有收益。
```

### 4.1 控制单位与统计单位

DQB 有一个重要设计：

| 层面 | 单位 |
|---|---|
| 迁移控制 | batch |
| 执行调度 | task |
| 统计记录 | task |

也就是说，DQB 做决策时考虑一批请求，但请求真正迁移、到达、执行和完成时仍然是逐个 task 处理。

这样既能利用 batch 的结构信息，又能保持精确的 per-request latency 和 SLO 统计。

### 4.2 DQB 最想解决的场景

DQB 最适合的场景是：

```text
局部 host/core 上形成连续排队压力，
这一段队列中的多个请求都面临相似风险，
迁移整个 batch 可以释放源端排队压力，
目标端也有足够容量吸收这个 batch。
```

这正是 W2 burst workload 中常见的情况。

## 5. DQB-PM 的完整流程

DQB 的完整在线流程如下：

```text
1. 源 host 触发 CHECK_MIGRATION
2. 扫描本地 core 队列前缀
3. 生成 QueueSummary
4. 将队列前缀切成连续 BatchRegion
5. 为每个 region 估计 risk mass、confidence、batch type
6. 每个 core 选择一个最佳 QueueBatchCandidate
7. 对候选 batch 采样若干目标 host
8. 对每个目标 host 做 virtual-core placement 估计
9. 判断 improved_tasks、harmful_tasks、remote_tail、target_harm
10. 通过 incoming reservation 防止目标端过载
11. 按 batch score 选择要提交的 batch
12. 整批提交迁移，但为每个 task 单独生成 arrival event
13. 请求完成后统计 latency、SLO violation、invalid migration
```

更直观地说：

```text
本地队列
  -> 轻量摘要
  -> 找风险队列段
  -> 判断这段队列能否整体迁移
  -> 判断目标端是否能安全接收
  -> 可以改善才迁移，否则不迁移
```

## 6. 算法关键模块详细说明

### 6.1 QueueSummary：本地队列摘要

DQB 不扫描整个队列，而是扫描固定数量的队列前缀：

```text
DQB_SUMMARY_SCAN_LIMIT = 256
```

对每个被扫描的 task，算法记录：

```text
service_us：预计服务时间
exec_src_us：在源 core 上预计执行时间
age_us：该请求已经等待或存在了多久
prefix_work_us：该请求前方已有多少工作量
slo_us：该请求的 SLO
is_short：是否短任务
is_elephant：是否 elephant 任务
```

其中最重要的是 `prefix_work_us`。例如，一个短任务本身只需要 5 us，但它前面排着一个 100 us 的长任务，那么它的实际完成时间风险主要来自前方排队，而不是自身服务时间。

本地完成延迟估计为：

```text
local_finish_us = age_us + prefix_work_us + exec_src_us
```

如果：

```text
local_finish_us > alpha * SLO
```

则该请求对 batch 的风险质量 `risk_mass` 有贡献。

### 6.2 BatchRegion：连续队列区域

DQB 会把扫描到的队列前缀切成若干连续区域。每个区域称为 `BatchRegion`。

W1/W2 默认 batch 参数：

| 参数 | 值 | 含义 |
|---|---:|---|
| `DQB_MIN_TASKS_PER_BATCH` | 8 | 至少 8 个请求才算有效 batch |
| `DQB_SEGMENT_TARGET_TASKS` | 16 | 一个 region 的目标大小 |
| `DQB_MAX_TASKS_PER_BATCH` | 64 | 一个 batch 最多 64 个请求 |
| `DQB_MIN_BATCH_WORK_US` | 80 | batch 最小工作量 |
| `DQB_MAX_BATCH_WORK_US` | 2400 | batch 最大工作量 |

每个 `BatchRegion` 会记录：

```text
task_count：请求数量
short_count：短任务数量
elephant_count：elephant 数量
batch_work_src_us：源端工作量
work_before_us：batch 前方已有工作量
estimated_local_tail_us：留在本地时最坏请求延迟
risk_mass：风险质量
confidence：分布置信度
blocking_long_work_us：长任务阻塞量
blocking_elephant_work_us：elephant 阻塞量
```

这里的 `risk_mass` 和 `confidence` 是 DQB 后续评分的核心。

### 6.3 Distribution-aware confidence：分布置信度

DQB 会根据 workload prior 和当前 batch 中的任务分布，估计这个 batch 是否是一个可信的风险结构。

任务分类包括：

```text
short / long
mice / medium / elephant
```

短任务阈值：

```text
SLO_SHORT_SERVICE_THRESHOLD_US = 20 us
```

elephant 阈值：

```text
DQB_ELEPHANT_SERVICE_US = 80 us
```

分布置信度主要考虑：

1. batch 是否足够大；
2. batch 中短任务比例是否接近 workload prior；
3. 当前 batch 是否具有稳定的结构特征。

直观理解：

```text
如果一个 batch 太小，或者任务组成非常偶然，
算法就不应该过度相信它代表一种可迁移的队列风险。
```

### 6.4 Batch type：批类型

DQB 会给每个 batch 分配一个类型。

| 类型 | 含义 | 典型场景 |
|---|---|---|
| `GenericPressure` | 一般队列压力 | 队列积压但结构不明显 |
| `ShortBehindLong` | 短任务排在长任务后面 | W1/W2 |
| `MiceBehindElephant` | mice 被 elephant 阻塞 | W3 |
| `SlowNodeBatchPressure` | 慢节点上的批量压力 | 异构集群 |
| `DistributionWindow` | 分布稳定的队列窗口 | W2 burst |

batch type 会影响候选评分。具有明确结构解释的 batch 会获得更高权重。

### 6.5 QueueBatchCandidate：候选迁移批

每个 core 通常最多生成一个最佳候选批 `QueueBatchCandidate`。

候选批包含：

```text
源 host / 源 core
batch type
batch 中的 tasks
每个 task 的本地完成估计
batch 总工作量
batch 风险质量 risk_mass
batch 阻塞分数 blocking_score
batch 分布置信度 estimate_confidence
batch 中 short / elephant 数量
```

候选批评分大致由以下因素组成：

```text
type_weight * risk_mass
+ blocking_score
+ estimate_confidence
+ move_count
```

含义是：

- `risk_mass` 越高，说明留在本地越危险；
- `blocking_score` 越高，说明存在明显阻塞结构；
- `estimate_confidence` 越高，说明 batch 结构更可信；
- `move_count` 适度鼓励真实 batch，而不是退化为单请求迁移。

## 7. 三类 workload 的算法行为

当前实验主要关注三类 workload。

### 7.1 W1：Poisson + Bimodal，饱和边界

W1 是 Poisson 到达 + bimodal 服务时间。它主要用于测试系统在高负载饱和情况下是否会错误迁移。

在 `rho=0.95` 时，系统整体接近饱和。此时迁移不能创造容量，最好的策略通常是减少迁移，避免把压力转移到目标端。

DQB 在 W1 中的关键机制是 saturation guard。

### 7.2 W2：MMPP + Bimodal，burst 场景

W2 使用 MMPP 到达过程，会产生 normal/burst 两种状态。burst 状态下部分 hot nodes 会收到更多请求。

这类场景非常适合 DQB，因为 burst 会产生局部连续队列压力：

```text
一个 host/core 上不是单个请求危险，
而是一段队列整体危险。
```

DQB 的连续 batch 迁移正好可以修复这种问题。

### 7.3 W3：Poisson + Lognormal，heavy-tail 场景

W3 使用 lognormal 服务时间，会产生 heavy-tail。它的典型问题是：

```text
少量 elephant task 阻塞后方 mice task。
```

但这些阻塞往往比较稀疏，分散在多个浅队列中，不一定形成一个连续的大 batch。

因此 W3 是检验 DQB 边界的重要场景。

## 8. W3 的特殊处理：Host-level Fragment Aggregation

为了处理 W3，当前 DQB 不只使用单 core 连续队列段，还增加了 host-level fragment aggregation。

具体流程：

```text
1. 扫描同一个 host 上所有 cores
2. 每个 core 找 mice-like blocked fragment
3. 过滤太小或低置信度 fragment
4. 按 type、age、score 排序
5. 在 age window 内聚合同类 fragment
6. 形成 host-level QueueBatchCandidate
```

主要参数：

| 参数 | 值 |
|---|---:|
| `DQB_W3_MIN_FRAGMENT_TASKS` | 1 |
| `DQB_W3_HOST_MIN_TASKS` | 8 |
| `DQB_W3_HOST_TARGET_TASKS` | 16 |
| `DQB_W3_HOST_MAX_FRAGMENTS` | 12 |
| `DQB_W3_HOST_AGE_SPREAD_US` | 160 |

这个设计说明：当前 W3 已经不再受限于“单个 core 上必须形成大 batch”。它可以把同一个 host 内多个 core 上的 blocked mice fragments 聚合成一个 batch。

但是实验结果显示，即使这样，W3 中可迁移 batch 仍然非常少。这说明问题本身可能不是大 batch repair 能充分解决的。

## 9. 目标端选择与安全保护

候选 batch 形成后，DQB 会随机采样若干目标 host。默认目标采样数为：

```text
M0_K_DST = 4
```

对每个候选目标，DQB 会判断目标端是否安全。

### 9.1 incoming reservation

由于多个源 host 可能同时把任务迁移到同一个目标 host，单纯 stale view 不够。DQB 维护了 incoming reservation：

```text
incoming_reservation[dst]
incoming_core_reservation[dst][core]
```

它表示已经决定迁移、但还没有真正到达目标端的工作量。

目标端平均虚拟等待估计为：

```text
avg_remote_wait_us =
    (stale_workload_view[dst] + incoming_reservation[dst]) / CORES_PER_HOST
```

如果 reservation 太高，则拒绝该目标。

### 9.2 virtual-core placement

DQB 会为目标 host 构造 16 个虚拟 core slot：

```text
slot[c] =
    stale_workload_view[dst] / CORES_PER_HOST
    + incoming_core_reservation[dst][c]
```

然后把 batch 中的请求逐个放到当前最空的虚拟 core 上：

```text
task_work_us = service_us / dst_capacity + T_host_us

remote_latency_us =
    age_us
    + T_net_oneway_us
    + T_host_us
    + slot[chosen_core]
    + task_work_us

slot[chosen_core] += task_work_us
```

这一步的作用是估计：如果整个 batch 迁移到目标 host，它会不会在目标端造成新的排队问题。

### 9.3 整批收益判断

对一个 batch，DQB 不是只看平均收益，而是统计：

```text
improved_tasks：迁移后明显变好的请求数
harmful_tasks：迁移后明显变差的请求数
improvement_mass_us：总改善量
target_harm_est_us：目标端伤害估计
remote_tail_us：迁移后 batch 中最差延迟
```

约束条件包括：

```text
improved_tasks >= max(4, 0.60 * move_count)
harmful_tasks <= max(1, move_count / 5)
```

最终 batch 的 tail gain 必须为正：

```text
gain_us =
    estimated_local_tail_us
    - remote_tail_us
    - effective_margin
```

只有满足这些条件，batch 才会被加入可迁移候选。

## 10. Saturation Guard：饱和保护

DQB 会用 stale workload 的 P25 判断系统是否整体饱和：

```text
p25_pressure =
    P25(stale_workload_view) / CORES_PER_HOST
```

不同 workload 有不同阈值：

```text
W2: DQB_SATURATION_P25_US * 4.0
W1: DQB_SATURATION_P25_US * 0.60
其他: DQB_SATURATION_P25_US
```

如果判定系统饱和：

```text
W1: batch_cap = 0，完全不迁移
W2: batch_cap = 1，极度收紧
```

这个机制非常重要。它体现了 DQB 的一个核心原则：

```text
迁移只能重新分配队列压力，不能创造计算容量。
当全局都很忙时，主动迁移应该被抑制。
```

## 11. 迁移提交与 invalid migration 统计

当 batch 被选中后，DQB 会整批提交迁移。

提交时会：

```text
从源 core wait_queue 移除 task
标记 task->migrated = true
记录 src_host
记录 migration_batch_id
记录 estimated_local_latency_us
记录目标 host 和目标 core reservation
为每个 task 创建 TASK_ARRIVE event
更新 incoming reservation
更新 batch metrics
```

需要强调的是：DQB 只迁移等待队列中的请求元数据，不迁移正在执行的 running task。

任务完成后，如果它是迁移任务，仿真器会判断：

```text
actual_latency_us > estimated_local_latency_us
```

如果迁移后的实际延迟比“不迁移时的估计延迟”还差，则记为 invalid migration。

因此 `invalid_migration_ratio` 是衡量迁移副作用的重要指标。

## 12. 实验结果与解释

下面所有结果均为 5 个固定 seeds 的 median：

```text
{11, 23, 37, 47, 59}
```

### 12.1 W2 Burst：DQB 的主要正结果

场景：

```text
W2 MMPP+Bimodal
rho = 0.85
```

| Method | P99 us | P999 us | SLO violation | Migration rate | Invalid ratio |
|---|---:|---:|---:|---:|---:|
| B2 Reactive | 1610 | 2270 | 0.468508 | 0.0123568 | 0.221303 |
| M0 Proactive | 1420 | 2010 | 0.386676 | 0.0400124 | 0.145056 |
| M1 AQB-PM | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| M2 DQB-PM | 358 | 572 | 0.311241 | 0.0115643 | 0.0290325 |

#### 结果解释

W2 是 DQB 当前最强的正结果。

相比 M1-AQB：

```text
P99: 1100 us -> 358 us
P999: 1730 us -> 572 us
migration_rate: 0.0453816 -> 0.0115643
invalid_ratio: 0.100943 -> 0.0290325
```

这说明 DQB 不是靠“移动更多请求”获得收益，而是靠“更准确地识别应该移动的队列批”获得收益。

W2 的 batch 诊断结果：

| 指标 | Median |
|---|---:|
| batch candidate | 840,591 |
| selected batch | 1,201 |
| moved requests | 14,446 |
| avg move batch size | 8.59 |
| batch size 8-31 | 1,201 |

这说明 DQB 在 W2 中确实形成了真实的 `8-31` 请求 batch，而不是退化成单请求迁移。

可以向导师强调：

```text
W2 证明 DQB 的 batch abstraction 是有效的。
它能识别 burst 造成的局部连续队列压力，
并用更少迁移显著降低尾延迟。
```

### 12.2 W1 Saturation：No-migrate 保护有效

场景：

```text
W1 Poisson+Bimodal
rho = 0.95
```

| Method | P99 us | P999 us | SLO violation | Migration rate | Invalid ratio |
|---|---:|---:|---:|---:|---:|
| B2 Reactive | 1380 | 1600 | 0.996324 | 0.000411213 | 0.402612 |
| M0 Proactive | 1500 | 2700 | 0.991473 | 0.0394141 | 0.379253 |
| M1 AQB-PM | 1370 | 1580 | 0.996731 | 0.0175483 | 0.249164 |
| M2 DQB-PM | 1380 | 1590 | 0.996014 | 0 | 0 |

#### 结果解释

W1 saturation 下，DQB 的迁移率为 0，invalid ratio 也为 0。

这是正确行为。因为系统已经接近整体饱和，此时迁移不会创造新容量，只会把延迟从源端转移到目标端。

相比之下，M0 仍然执行主动迁移，导致 P999 达到 `2700 us`。这说明单请求主动迁移在高负载下可能出现反噬。

可以向导师强调：

```text
W1 证明 DQB 不仅能主动迁移，也知道什么时候不应该迁移。
这个 no-migrate 能力对真实系统很重要。
```

### 12.3 W3 Heavy-tail：当前算法边界

场景：

```text
W3 Poisson+Lognormal
rho = 0.85
```

| Method | P99 us | P999 us | SLO violation | Migration rate | Invalid ratio |
|---|---:|---:|---:|---:|---:|
| B2 Reactive | 200 | 394 | 0.096214 | 0.0221686 | 0.297758 |
| M0 Proactive | 186 | 360 | 0.088628 | 0.0402885 | 0.0508681 |
| M1 AQB-PM | 176 | 338 | 0.084086 | 0.0451337 | 0.035445 |
| M2 DQB-PM | 202 | 398 | 0.096336 | 0.00000666 | 0 |

W3 batch 诊断：

| 指标 | Median |
|---|---:|
| batch candidate | 1 |
| selected batch | 1 |
| moved requests | 8 |
| avg move batch size | 8 |
| batch type mice | 1 |

#### 结果解释

W3 是当前 DQB 的边界场景。

当前算法已经加入 host-level fragment aggregation，因此 W3 不是完全不能形成 batch。实验显示它确实能形成合法的 8-task mice batch。

但是 batch 形成频率太低：

```text
selected batch median = 1
moved requests median = 8
migration_rate = 0.00000666
```

这说明 W3 的问题主要是 sparse heavy-tail blocking，而不是 burst 型空间负载不均衡。也就是说，短任务被长任务阻塞是真实存在的，但这些风险分散在多个浅队列中，不适合单纯用大 batch migration 修复。

可以向导师强调：

```text
W3 不是简单失败，而是定义了算法边界。
它说明 burst repair 和 heavy-tail sparse blocking repair 不应该强行使用同一种 batch abstraction。
```

## 13. 当前算法状态总结

当前 DQB-v1 已经完成以下能力：

| 能力 | 状态 |
|---|---|
| 本地队列摘要 `QueueSummary` | 已实现 |
| 连续队列 batch region | 已实现 |
| batch type 分类 | 已实现 |
| risk mass 和 confidence 估计 | 已实现 |
| batch-level target selection | 已实现 |
| incoming reservation | 已实现 |
| virtual-core placement | 已实现 |
| saturation guard | 已实现 |
| W3 host-level fragment aggregation | 已实现 |
| batch-size/type 基础诊断 | 已实现 |
| DQB-v2 | 尚未实现，处于实验计划阶段 |

当前结论：

```text
DQB-PM 当前已经证明自己是一个有效的 burst repair 算法。
它在 W2 burst 下显著降低 P99/P999，同时迁移率和 invalid ratio 都更低。
它在 W1 saturation 下能够正确 no-migrate，避免高负载迁移反噬。
它在 W3 heavy-tail 下暴露边界，说明 sparse blocking 需要 hybrid 机制。
```

## 14. 当前问题与注意事项

### 14.1 W3 focused CSV 生成 bug

当前 `artifacts/step-08-dqb-batch/dqb_w3_only.csv` 文件为空，这是一个需要修复的数据生成 bug。

不过 W3 的汇总结果仍然保存在：

```text
artifacts/step-08-dqb-batch/metrics_summary.csv
docs/DQB_CURRENT_RESULTS_ANALYSIS.md
```

因此当前可以先基于汇总结果向导师汇报，但后续必须修复 focused CSV 生成流程。

### 14.2 诊断指标已经开始加入，但还需要重新跑实验

当前代码已经加入了一批下一阶段诊断指标，包括：

```text
short_slo_violation_rate
long_slo_violation_rate
mice_slo_violation_rate
elephant_slo_violation_rate
migration_work_rate
exact_batch_size_histogram
no-migrate reason counters
source_queue_depth
source_queue_work_us
destination_virtual_occupancy
target_harm_est_us
control-plane cost estimates
```

但已有 CSV 大多还不是完整新格式，因此需要重新生成数据。

### 14.3 W1 中 saturation guard 可以前移

W1 中 DQB 最终没有迁移，这是正确结果。但它仍生成大量 candidates 和 target rejects。

这说明实现上可以进一步优化：

```text
在明显饱和时，先判断 no-migrate，
再决定是否需要构造 batch candidate。
```

这样可以降低控制路径开销。

### 14.4 W3 需要新的 hybrid 设计

W3 的结果说明，大 batch migration 不是所有尾延迟问题的通用解。

下一步可以考虑：

- 保留 DQB 作为 W2 burst repair 主路径；
- 增加 W3 sparse-risk fallback；
- 使用 host-level arrival-epoch bin 聚合分散 mice；
- 用 no-migrate reason 解释哪些阻塞不适合 batch migration；
- 避免把 DQB 调参调回单请求迁移。

## 15. 下一阶段计划

建议下一阶段按以下顺序推进：

1. 修复 `dqb_w3_only.csv` 生成 bug。
2. 重新运行 W1/W2/W3 focused validation。
3. 重新生成带完整诊断指标的 DQB-v1 CSV。
4. 分析 no-migrate reason、exact batch-size histogram、source queue work、destination virtual occupancy。
5. 在诊断结果清楚后，实现 DQB-v2。
6. 对比 `M1_AQB_PM`、当前 `M2_DQB_PM` 和 DQB-v2 的 ablation。

DQB-v2 的计划方向是：

```text
workload prior + local queue summary
  -> DistributionBatchDescriptor
  -> batch-level risk estimate
  -> batch-level destination estimate
  -> whole-batch commit or no-migrate
```

计划中的 DQB-v2 对比组包括：

```text
M1_AQB_PM
current M2_DQB_PM
DQB-v2/full
DQB-v2/prior-only
DQB-v2/summary-only
DQB-v2/no-reservation
DQB-v2/no-saturation-guard
```

## 总结

```text
当前 DQB-PM 将主动迁移的控制单位从单请求提升为分布感知的队列批。
它通过本地队列摘要识别具有共同风险结构的 batch，并在 stale view、incoming reservation 和目标端 virtual-core placement 约束下，只迁移预计能整体降低尾延迟的 batch。
实验表明，DQB 在 W2 burst 场景下用更少迁移显著降低 P99/P999，在 W1 saturation 下能正确不迁移，而 W3 heavy-tail 结果说明 sparse blocking 需要 hybrid 机制。
```

## 附录：从一个任务的角度理解 DQB-PM

向导师讲算法时，可以把系统级流程换成“一个请求的一生”。这样会更直观，也更容易说明 DQB 和单任务迁移算法的区别。

### 1. 任务生成：它先获得自己的基本属性

一个请求首先在 `TASK_GENERATE` 事件中生成。生成时，它会得到几个基础属性：

```text
generate_time_us：请求生成时间
base_service_time_us：真实服务时间
expected_service_time_us：算法用于估计的服务时间
slo_target_us：该请求的 SLO
```

如果请求是短任务，它的 SLO 是：

```text
SLO_SHORT_US = 40 us
```

如果请求是长任务，它的 SLO 是：

```text
SLO_LONG_US = 200 us
```

随后，请求会通过初始调度器分发到某个 host。当前非 B0 方法默认使用 stale view 下的 Power-of-2 分发：

```text
随机选两个 host
比较 stale queue length
发送到 stale queue length 较小的 host
```

在 W2 burst 场景中，如果系统处于 burst 状态，请求还有一定概率被导向 hot nodes。这用于模拟真实系统中局部热点的形成。

### 2. 任务到达 host：进入某个 core 队列

请求经过网络延迟后到达目标 host：

```text
arrival_time = generate_time + T_net_oneway_us
```

到达后，它会被放入该 host 的某个 core 队列。默认选择等待队列较短的 core。

如果该 core 空闲，请求立即开始执行；否则，它进入等待队列。

从这个时刻开始，请求有两种可能：

```text
1. 留在本地队列，等待执行完成；
2. 在后续 CHECK_MIGRATION 中被 DQB 选入某个 batch，迁移到其他 host。
```

### 3. 任务在本地队列中：被 DQB 周期性观察

DQB 并不是每次都对所有任务做完整分析。每个 host 会周期性触发 `CHECK_MIGRATION`。当检查发生时，DQB 会扫描本地 core 等待队列的固定前缀。

如果这个任务排在队列前缀内，它会被转换成一个 `ScanItem`，算法会为它计算：

```text
age_us = now_us - generate_time_us
exec_src_us = expected_service_time_us / source_core_capacity + T_host_us
prefix_work_us = 它前面所有任务的预计剩余工作量
local_finish_us = age_us + prefix_work_us + exec_src_us
```

这里最重要的是 `prefix_work_us`。它表示该任务前面还有多少工作要执行。

例如，一个短任务自身只需要 5 us，但如果它前面排着一个 100 us 的长任务，那么它真正的风险不是自身服务时间，而是被前面的长任务阻塞。

### 4. 任务贡献风险，但不单独做决定

算法会比较该任务留在本地时的预计完成延迟和它的 SLO：

```text
local_finish_us > alpha * slo_target_us
```

如果这个条件成立，说明该任务已经进入风险区间。它会为所在 batch 贡献一部分 `risk_mass`。

从单个任务角度看：

```text
任务不会单独决定自己是否迁移；
它先贡献风险信息；
这些风险信息会和同一段队列中的其他任务合并成 batch 级风险。
```

这就是 DQB 和单任务迁移方法的关键区别。

### 5. 任务进入 BatchRegion：从单点风险变成队列区域风险

DQB 会把队列前缀切成连续的 `BatchRegion`。如果这个任务位于某个连续区域内，它就成为该 region 的一部分。

这个 region 会统计：

```text
包含多少任务
其中多少是 short task
其中多少是 elephant task
总工作量是多少
最老任务等待了多久
最差本地完成延迟是多少
总 risk_mass 是多少
分布置信度 confidence 是多少
```

从任务角度看，它不再是一个孤立的迁移候选，而是某个队列区域中的一员。

例如：

```text
如果它是一个短任务，
并且它和一批短任务都排在长任务后面，
那么这个区域可能被识别为 ShortBehindLong。

如果它是一个 mice task，
并且它排在 elephant task 后面，
那么在 W3 中它可能参与 MiceBehindElephant batch。
```

### 6. 任务进入 QueueBatchCandidate：它所在的区域通过筛选

不是所有 `BatchRegion` 都会成为迁移候选。DQB 会先过滤：

```text
任务数是否达到最小 batch size
batch work 是否足够
risk_mass 是否大于 0
分布 confidence 是否足够可信
batch type 是否有明确结构
```

如果该任务所在的 region 通过过滤，并且在评分中胜出，那么它就进入 `QueueBatchCandidate`。

候选 batch 会记录每个任务的本地完成延迟估计：

```text
local_finish_us_per_task
```

这对后续判断迁移是否真的改善该任务非常关键。

### 7. 任务经过目标端筛选：模拟它迁移后会怎样

进入候选 batch 并不意味着任务一定会迁移。DQB 还要为整个 batch 选择目标 host，并判断目标端是否安全。

对于 batch 中的这个任务，算法会在候选目标 host 上模拟它的迁移后延迟。

目标端有 16 个 virtual core slot：

```text
slot[c] =
    stale_workload_view[dst] / CORES_PER_HOST
    + incoming_core_reservation[dst][c]
```

算法会把 batch 中的任务逐个放到当前最空的 virtual core 上。对这个任务来说，迁移后的延迟估计是：

```text
remote_latency_us =
    age_us
    + T_net_oneway_us
    + T_host_us
    + slot[chosen_core]
    + remote_exec_us
```

其中：

```text
remote_exec_us = expected_service_time_us / dst_capacity + T_host_us
```

然后比较：

```text
local_finish_us - remote_latency_us
```

如果这个值足够大，说明该任务迁移后会受益；如果 remote latency 反而更高，说明它会被迁移伤害。

### 8. 任务不是单独被决定，而是随 batch 一起被决定

DQB 的判断不是“这个任务一个人是否收益”，而是看整个 batch：

```text
batch 中至少 60% 的任务需要改善；
被伤害的任务数量不能太多；
batch 的 remote_tail 必须低于 local_tail；
目标端 reservation 不能太高；
系统不能处于禁止迁移的饱和状态；
迁移预算不能耗尽。
```

因此，一个任务即使自己可能略有收益，也不一定会迁移。它必须所在的 batch 整体值得迁移。

这体现了 DQB 的保守性：

```text
只有当迁移对一批任务整体有利，
并且目标端不会被明显伤害时，
才提交迁移。
```

### 9. 任务被迁移时：它随 batch 一起提交

如果 batch 被选中，该任务会从源 core 的等待队列中移除，并被标记为迁移任务：

```text
task->migrated = true
task->src_host = source host
task->migration_batch_id = batch id
task->estimated_local_latency_us = 不迁移时的本地延迟估计
task->reserved_dst_host = 目标 host
task->reserved_dst_core = 目标 core
task->pending_reserved_work_us = 在目标端占用的预计工作量
```

然后仿真器为它创建一个新的 `TASK_ARRIVE` 事件：

```text
arrival_time = now_us + T_net_oneway_us
```

同时，目标端的 incoming reservation 会增加，表示这个任务虽然还没到达，但目标端已经为它预留了虚拟工作量。

### 10. 任务到达目标端：释放 reservation 并重新排队

任务到达目标 host 后，会进入目标 core 队列。

如果之前 DQB 已经为它规划了目标 core，那么它会优先进入该 core。到达后，之前记录的 pending reservation 会被释放：

```text
incoming_reservation 减少
incoming_core_reservation 减少
```

之后它就像普通任务一样等待执行或立即执行。

### 11. 任务完成：统计真实效果

任务完成时，仿真器计算真实端到端延迟：

```text
latency_us = finish_time_us - generate_time_us
```

然后判断是否违反 SLO：

```text
latency_us > slo_target_us
```

如果这个任务迁移过，还会判断迁移是否无效：

```text
latency_us > estimated_local_latency_us
```

如果迁移后的真实延迟比当时估计的“不迁移本地延迟”还差，则记为 invalid migration。

因此，从任务角度看，它最终会贡献以下统计：

```text
latency histogram
P99 / P999
SLO violation rate
migration rate
invalid migration ratio
short/long 或 mice/elephant 分类统计
```

### 12. 一句话讲清楚一个任务的一生

可以在汇报中这样说：

```text
一个请求生成后先被分发到某个 host 和 core。
如果它在本地队列中等待，DQB 会在周期性检查中估计它的 age、前方排队工作量、本地完成时间和 SLO 风险。
但 DQB 不会立刻单独迁移它，而是把它和相邻请求一起组成一个队列区域，计算这个区域的 risk mass、分布置信度和 batch type。
如果该区域成为候选 batch，算法会在 stale view 和 incoming reservation 下模拟整个 batch 放到目标 host 的 virtual cores 上，判断该请求以及 batch 中其他请求是否整体受益。
只有当 batch 整体改善、目标端安全、系统未饱和且预算允许时，这个请求才会随 batch 一起迁移。
完成后，系统再根据真实延迟判断它是否违反 SLO，以及这次迁移是否真的有效。
```

### 13. 和 M0 单任务迁移的对比讲法

如果导师问 DQB 和传统主动迁移有什么本质区别，可以这样解释：

```text
M0 看到的是“这个请求危险吗？迁移它是否有利？”
DQB 看到的是“这个请求所在的一段队列是否形成了共同风险？整段队列迁移是否整体有利？”
```

因此，DQB 更适合 W2 burst 这种连续队列压力场景；而 W3 heavy-tail 中风险更稀疏，所以 DQB 的大 batch abstraction 就会遇到边界。
