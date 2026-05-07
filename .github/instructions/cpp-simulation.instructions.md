---
description: "Use when implementing or modifying C++ discrete-event simulation code for microsecond-level proactive migration scheduling. Covers event model, timing units, queue data structures, and metrics collection conventions."
name: "C++ DES Simulation Rules"
applyTo:
  - "**/*.cpp"
  - "**/*.cc"
  - "**/*.cxx"
  - "**/*.h"
  - "**/*.hh"
  - "**/*.hpp"
  - "**/*.hxx"
---
# C++ DES Simulation Rules

- Target language level: prefer C++17 or newer; avoid compiler-specific extensions unless explicitly requested.
- Keep simulator logic event-driven (no fixed time-step loops for core scheduling behavior).
- Use microseconds as the primary latency unit in code and comments; name variables with clear unit suffixes like `_us`.
- Keep event ordering deterministic. For identical timestamps, process in this priority order: `TASK_FINISH > TASK_ARRIVE > TASK_GENERATE`.
- Separate concerns clearly: `Simulator` (time/event queue), `Node/Core` (execution resources), `Task` (request metadata), `Event` (state transition action).
- Prefer data structures that support efficient middle removal for migration candidates (for example intrusive doubly linked list patterns).
- Avoid O(N) middle-erase containers in hot paths for migration scanning unless benchmark evidence proves acceptable.
- Model host and network overhead explicitly instead of hiding them in generic constants.
- Preserve key timing assumptions as named constants, not magic numbers:
  - `T_host_us = 2.1`
  - `T_net_oneway_us = 3.15`
  - `T_rpc_us = 6.3` (when needed by model/reporting)
- Migration logic should include: SLO risk check, absolute benefit check, and hysteresis margin to reduce ping-pong migration.
- Treat stale global load view as a first-class simulation factor (sync heartbeat vs local real-time state).
- Prefer memory pools or PMR strategies for high-frequency `Task/Event` creation to reduce allocation overhead in heavy-load simulations.

## Metrics And Output

- Always report tail latency with at least `P99` and `P99.9`.
- Include migration-related metrics: migration rate and invalid migration ratio.
- Prefer histogram-based aggregation (for example HDR histogram style) over storing all per-task latencies in large vectors.
- Keep metric names stable and explicit (for example `tail_p99_us`, `migration_rate`, `invalid_migration_ratio`).

## Style Expectations

- Keep public API names descriptive and aligned with simulation semantics (`schedule_event`, `run`, `process`).
- Add short comments only where model assumptions or event ordering might be misread.
- Avoid hidden side effects in event handlers; state transitions should be easy to trace.

## 中文补充

- 以离散事件驱动为核心，不使用固定步长循环替代核心调度逻辑。
- 时间单位统一为微秒，变量命名显式带单位后缀（如 `_us`）。
- 相同时间戳事件必须按固定优先级处理，保证可复现实验结果。
- 主动迁移判定需包含：违约风险、绝对收益、防抖阈值三重约束。
- 热路径中避免使用中部删除为 O(N) 的容器。
- 指标输出至少包含 `P99`、`P99.9`、迁移率、无效迁移率。
