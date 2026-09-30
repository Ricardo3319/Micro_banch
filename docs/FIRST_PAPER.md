# First Paper Contract (frozen)

> 2026-09-26：本冻结约定已被用户明确要求重新确定方向、实现并对比算法的指令覆盖。保留以下内容作为旧研究记录。当前入口为 `batchsched`，当前证据与结论见 `QBR_RESEARCH_SPEC.md` 和 `RESEARCH_DECISION_2026-09-26.md`；旧结果不再作为新方向的验证。

This file freezes the first completable paper. Do not reopen intra-host
RescueSched, INFOCOM claims, or a host-queue rewrite until this paper is done.

## Claim

After imperfect inter-host dispatch, waiting RPC descriptors can still form
host-level queue pressure. DQB-PM repairs that pressure by migrating
distribution-aware queue batches under a stale load view and incoming
reservation. It is not a generic load balancer and does not solve global
overload or sparse heavy-tail HoL.

One sentence:

> DQB-PM reduces tail latency under bursty host-level queue pressure by
> batch-repairing waiting requests after dispatch, with lower control-path
> cost than per-task scoring.

## Why this, not RescueSched

Frontier systems already split the stack:

- Inter-host: better *dispatch* (R2P2, RackSched, Horus, Pallas).
- Intra-host: steal / preempt / hardware migrate (ZygOS, Shinjuku, ALTO).

Software post-dispatch *host-level batch repair* is the remaining seam that
this simulator already implements, and that CloudLab 2–4 nodes can calibrate.
Per-task intra-host rescue is the wrong first paper: slack is too small, and
work stealing already occupies that Pareto frontier.

## Frozen system boundary

- Research layer: inter-host.
- Execution: tasks still complete on cores (16 cores/host). Intra-host policy
  is identical across hosts and is not a contribution.
- Migration unit: waiting-request batch / FIFO prefix / distribution region.
  Not per-task rescuability triples.
- Evidence: simulation is primary. CloudLab is constants + 2–4 node trend,
  not a rack-scale prototype.

## Methods

| Role | Method |
| --- | --- |
| Dispatch-only | `B1_PowerOf2` |
| Reactive repair | `B2_Reactive` |
| Per-task proactive | `M0_Proactive` |
| Task-score then batch | `M1_AQB_PM` |
| Target | `M2_DQB_PM` |

## Experiment order

1. **WP1:** freeze contract + runners. **DONE**
2. **WP2:** `paper-w2` main table. **DONE** — DQB median P99 358 vs M1 1100.
   Repair-vs-repair closes (vs M1 5/5, vs M0 4/5). Vs B1 does not (3/5);
   seed 47 is global-hot, same family as W1.
3. **WP3:** `paper-w1` no-storm. **DONE** — DQB moves=0 on 5/5, sat_guard~3.26M.
4. **WP4:** `paper-w3` boundary. **DONE** — DQB ~1 batch of size 8; M1 still
   slightly better. Sparse HoL, not a flagship.
5. **WP5 (optional):** CloudLab c6525-25g constant calibration. **NOT RUN**

The first paper can be written from WP2–WP4. Do not reopen RescueSched.

## Commands

```text
./build-linux/simulator paper-w2   # artifacts/step-19-paper-w2/paper_w2.csv
./build-linux/simulator paper-w1   # artifacts/step-20-paper-w1/paper_w1.csv
./build-linux/simulator paper-w3   # artifacts/step-21-paper-w3/paper_w3.csv
```
