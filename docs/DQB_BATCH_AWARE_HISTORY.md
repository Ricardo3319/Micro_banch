# DQB Batch-Aware Algorithm History

> **Legacy research history.** RescueSched is the sole current paper line.
> This document is retained for reproducibility and must not be cited as current
> architecture, current evidence, or a planned RescueSched experiment.

## Motivation

The early DQB path showed that queue summaries were safer than per-task sorting
in `W2`, but the practical move size was often still `1`. The first large-batch
revision fixed that for burst repair by switching to contiguous queue segments
plus whole-batch target feasibility.

That left one unresolved question: if `W3 heavy-tail` cannot form useful
batches from one core queue, will host-level aggregation recover the missing
gain?

## Current Batch Definitions

The repository now contains two DQB batch constructions:

1. **Per-core contiguous segment** for W1/W2 and the default DQB path.
2. **Host-level fragment aggregation** for W3, where multiple mice-like blocked
   fragments from different cores are combined into one host batch inside an
   age window.

Both paths still require destination-side whole-batch feasibility. The control
unit is the batch; the measurement unit remains the task.

## Update Log

- `M0_Proactive`: single-task predictive migration with stale global load.
- `M1_AQB_PM`: bounded task-candidate scan plus batched host-level selection.
- `M2_DQB_PM` Step-07: queue summaries, structural batch candidates, global
  incoming reservation, and initial saturation guard.
- `M2_DQB_PM` Step-08a: distribution windows, FIFO region expansion, virtual
  core placement estimates, and batch-size/type diagnostics.
- `M2_DQB_PM` Step-08b: large contiguous segment construction, whole-batch
  target feasibility, W1 strict no-migrate saturation guard, and focused
  scenario runners for W1/W2/W3.
- `M2_DQB_PM` Step-09: W3 host-level fragment aggregation with age-window batch
  assembly and multi-source-core batch commit.

## Current Findings

- **W2 burst**: the large-batch DQB result holds. Median moved batch size is
  `8.59`, selected batches are dominated by `8-31`, and median P99 improves
  from `1100us` (`M1`) to `358us`.
- **W1 saturation**: DQB still performs zero migration across the frozen seeds.
- **W3 heavy-tail**: host-level aggregation does create real batches now, but
  only at a very low rate. Median selected batch size is `8`, median selected
  batch count is `1`, and median P99/P999 still do not improve.

## What This Means

The current evidence says the project should stop treating burst repair and
heavy-tail repair as the same batch problem.

- For `W2`, honest large-batch migration is the right abstraction.
- For `W3`, honest large-batch migration remains too coarse even after
  host-level aggregation.

So the next W3 step should be a hybrid sparse-risk repair path or a richer
host-level batch definition, not another round of threshold tuning that quietly
shrinks DQB back toward per-task migration.
