# Step-06 AQB-PM Evaluation Summary

## Scope
- Goal: introduce and validate `M1_AQB_PM`, a queueing-pressure batched proactive migration variant.
- Build: `build-aqb-check`
- Command: `.\build-aqb-check\simulator.exe aqb-eval`
- Output CSV: `artifacts/step-06-aqb/aqb_eval.csv`

## Algorithm Delta
`M1_AQB_PM` extends the prior single-task M0 mechanism with:
- workload-aware stale view (`stale_workload_view`) instead of only queue length;
- bounded queue-prefix candidate scan (`AQB_SCAN_DEPTH=4`);
- SLO-normalized urgency and net migration gain scoring;
- host-level bounded batch-selected migration decisions (`AQB_MAX_BATCH_PER_HOST=4`);
- per-core and per-destination caps;
- global saturation guard that falls back to a single migration per host check.

Important terminology correction: the current implementation is a **batched decision** mechanism, not a transport-level batched migration mechanism. After a host selects multiple tasks in one `CHECK_MIGRATION` epoch, the simulator still schedules one `TASK_ARRIVE` event per migrated task. Therefore the current model does not yet amortize network/control overhead across multiple tasks, does not create a `MigrationBatch` object, and does not give the destination host a batch-aware placement opportunity. The current benefit of batching is faster control response to burst hotspots, not shared migration transmission cost.

## Representative Evaluation
Each scenario runs B2, M0, and M1 across the frozen 5 seeds `{11,23,37,47,59}` with warmup=200k and measurement=1M.

| Scenario | Method | P99 median | P999 median | SLO violation median | migration_rate median | invalid_migration_ratio median |
|---|---:|---:|---:|---:|---:|---:|
| W2_burst_homo rho=0.85 | B2_Reactive | 1610 | 2270 | 0.468508 | 0.0123568 | 0.221303 |
| W2_burst_homo rho=0.85 | M0_Proactive | 1420 | 2010 | 0.386676 | 0.0400124 | 0.145056 |
| W2_burst_homo rho=0.85 | M1_AQB_PM | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| W3_heavytail_homo rho=0.85 | B2_Reactive | 200 | 394 | 0.096214 | 0.0221686 | 0.297758 |
| W3_heavytail_homo rho=0.85 | M0_Proactive | 186 | 360 | 0.088628 | 0.0402885 | 0.0508681 |
| W3_heavytail_homo rho=0.85 | M1_AQB_PM | 176 | 338 | 0.084086 | 0.0451337 | 0.035445 |
| W1_saturation_homo rho=0.95 | B2_Reactive | 1380 | 1600 | 0.996324 | 0.000411213 | 0.402612 |
| W1_saturation_homo rho=0.95 | M0_Proactive | 1500 | 2700 | 0.991473 | 0.0394141 | 0.379253 |
| W1_saturation_homo rho=0.95 | M1_AQB_PM | 1370 | 1580 | 0.996731 | 0.0175483 | 0.249164 |

## Key Findings
1. W2 burst: M1 improves P99 by 31.7% vs B2 and 22.5% vs M0. Median migration rate is 4.54%, under the 5% budget, and imr drops to 0.101.
2. W3 heavy-tail: M1 improves P99 by 12.0% vs B2 and 5.4% vs M0, while P999 improves from M0's 360us to 338us.
3. W1 saturation: M1 does not create capacity under near-saturation, but it avoids M0's degradation. P99 is close to B2, P999 is slightly lower than B2, and imr is reduced below 0.30.

## Interpretation
The first evaluation supports the intended AQB-PM design:
- batching helps W2 because burst hotspots often contain multiple risky queued tasks;
- workload-aware scoring helps W3 because long-service blockers are better represented by work than queue length;
- the saturation guard reduces harmful proactive migration in W1 rho=0.95.

## Next Validation
- Add per-run batch diagnostics: candidate count, selected count, saturation guard activations, destination-cap rejections.

## Revised Method Limitations From Discussion

### 1. Batch decision is not batch transport

The current `M1_AQB_PM` should be described as **batch-selected proactive migration**. It chooses several high-score tasks in one local control epoch, but each selected task is still migrated independently. This distinction matters for paper positioning:

- Current implementation: one local scan, multiple migration decisions, multiple individual arrivals.
- Not yet implemented: one source-destination migration transaction carrying several tasks.
- Not yet implemented: shared fixed migration overhead, batch serialization cost, or batch-aware destination-side placement.

Consequently, any claim about M1 should be limited to decision-layer batching unless the simulator is extended with a real batch commit model.

### 2. Local reservation only prevents intra-source herding

The current destination reservation is local to one source host's AQB decision epoch. It prevents a single hot source host from sending every selected task to the same destination in one batch, because each selected task adds virtual workload to that destination before later selections are considered.

However, this does **not** prevent cross-source herding. If several source hosts check at nearly the same time, each host has its own local reservation array and cannot see the others' pending migrations. In heterogeneous settings, many sources may still converge on the same apparently light fast nodes based on stale workload views. This is a plausible contributor to the `rho=0.85` heterogeneous regression.

The next implementation should add a short-window global incoming reservation, such as `incoming_reservation[dst]`, that is updated when any migration is scheduled and decays or is cleared when the migrated task arrives.

### 3. Queue-prefix scan is a bounded short-term risk approximation

The current candidate generator scans only the first `AQB_SCAN_DEPTH=4` waiting tasks per core. This is not a complete risk detector. It approximates near-term SLO risk for tasks close to execution, which keeps the control-plane cost bounded, but it can miss deeper short tasks blocked behind long tasks.

This limitation is especially relevant in heterogeneous clusters. On slow nodes (`C=0.2`), a few long tasks can create large hidden waiting time for short tasks deeper than the fixed prefix. Those deeper tasks may dominate P99/P999 but never become AQB candidates if they sit beyond the scan window.

The next implementation should replace the fixed prefix with an adaptive bounded window:

- always scan the first `L_base` tasks;
- continue up to `L_max` when cumulative workload already indicates SLO risk;
- cap the number of candidates per core;
- optionally add a short-task probe to catch head-of-line blocking behind long jobs.

### 4. Heterogeneous `rho=0.85` is a boundary case, not a global failure

The heterogeneous experiment shows M1 is strong at `rho=0.70` and `rho=0.92`, but weak at `rho=0.85`. Per-seed results indicate the median regression is driven mainly by seeds 11, 23, and 37, while seeds 47 and 59 are good. This suggests the problem is not that individual migrations are mostly bad: M1's median invalid migration ratio is low at this point. Instead, M1 may spend its migration budget on locally high-score tasks that do not reduce the true burst bottleneck.

The most likely causes are:

- local-only reservation cannot coordinate multiple sources aiming at the same fast destinations;
- the fixed queue-prefix scan can miss deeper blocked short tasks on slow nodes;
- the saturation guard is based on global P25 pressure and may not trigger in a middle-high load regime with strong local bursts;
- batch selection is not dynamically re-scored after each selected migration, so several candidates may be scored against an optimistic destination state;
- M1 has no explicit slow-to-fast priority, so some budget may be spent on migrations that are locally valid but less important than draining slow-node bottlenecks.

## Additional Validation: Heterogeneous Cluster
Command:

```powershell
.\build-aqb-check\simulator.exe aqb-hetero
```

Output CSV: `artifacts/step-06-aqb/aqb_heterogeneous.csv`.

The experiment repeats the Step-04c heterogeneous W2 setup with 48 fast nodes (`C=1.0`) and 16 slow nodes (`C=0.2`), across `rho={0.50,0.70,0.85,0.92}`, methods `{B1,B2,M0,M1}`, and 5 frozen seeds. The CSV contains 80 completed runs.

| rho | Method | P99 median | P999 median | SLO violation median | migration_rate median | invalid_migration_ratio median |
|---:|---|---:|---:|---:|---:|---:|
| 0.50 | B1_PowerOf2 | 806 | 2740 | 0.07258 | 0 | 0 |
| 0.50 | B2_Reactive | 834 | 2510 | 0.10373 | 0.0119744 | 0.00768456 |
| 0.50 | M0_Proactive | 506 | 506 | 0.022631 | 0.029794 | 0.000186561 |
| 0.50 | M1_AQB_PM | 506 | 506 | 0.02027 | 0.0344897 | 0.0141045 |
| 0.70 | B1_PowerOf2 | 4900 | 6900 | 0.189974 | 0 | 0 |
| 0.70 | B2_Reactive | 2240 | 3940 | 0.158406 | 0.0295252 | 0.0449546 |
| 0.70 | M0_Proactive | 1890 | 5280 | 0.151504 | 0.0423843 | 0.0111923 |
| 0.70 | M1_AQB_PM | 1000 | 3040 | 0.085547 | 0.0450295 | 0.0141328 |
| 0.85 | B1_PowerOf2 | 4150 | 7400 | 0.376403 | 0 | 0 |
| 0.85 | B2_Reactive | 2990 | 4920 | 0.350303 | 0.00552047 | 0 |
| 0.85 | M0_Proactive | 2790 | 5970 | 0.467783 | 0.0404281 | 0.129008 |
| 0.85 | M1_AQB_PM | 3690 | 8160 | 0.413334 | 0.0452607 | 0.0568489 |
| 0.92 | B1_PowerOf2 | 4460 | 6800 | 0.901844 | 0 | 0 |
| 0.92 | B2_Reactive | 6700 | 8390 | 0.996467 | 0.000348857 | 0 |
| 0.92 | M0_Proactive | 6460 | 10000 | 0.981236 | 0.0396374 | 0.3058 |
| 0.92 | M1_AQB_PM | 3440 | 4750 | 0.756636 | 0.0456856 | 0.0630875 |

### Heterogeneous Interpretation
M1 is strongest at `rho=0.70` and `rho=0.92`:

| rho | M1 P99 | M1 vs B2 P99 | M1 vs M0 P99 |
|---:|---:|---:|---:|
| 0.50 | 506 | +39.3% | 0.0% |
| 0.70 | 1000 | +55.4% | +47.1% |
| 0.85 | 3690 | -23.4% | -32.3% |
| 0.92 | 3440 | +48.7% | +46.7% |

The result confirms the value of workload/capacity-aware batching in heterogeneous clusters, but also exposes a boundary case at `rho=0.85`. There, M1 keeps imr low (`0.0568`) but P99/P999 worsen, suggesting the policy is not simply making bad migrations; rather, it may be spending the migration budget on locally high-score tasks that do not align with the true MMPP burst trajectory. This points to a need for per-burst or per-destination diagnostics rather than only aggregate imr.

The high-load `rho=0.92` result is especially important: M1 reduces P99 from B2's 6700us and M0's 6460us to 3440us, while keeping imr at 0.063. This supports the saturation-guarded AQB design: in deep heterogeneous saturation, batch migration can still be useful when slow-to-fast capacity gaps create real migration gain.

## Additional Validation: Batch Size Sweep
Command:

```powershell
.\build-aqb-check\simulator.exe aqb-batch-sweep
```

Output CSV: `artifacts/step-06-aqb/aqb_batch_sweep.csv`.

This experiment fixes W2 homogeneous `rho=0.85`, method `M1_AQB_PM`, 5 frozen seeds, and sweeps the runtime AQB batch cap `{1,2,4,8}`. The CSV contains 20 completed runs.

| Batch cap | P99 median | P999 median | SLO violation median | migration_rate median | invalid_migration_ratio median |
|---:|---:|---:|---:|---:|---:|
| 1 | 1440 | 2030 | 0.419177 | 0.0454061 | 0.0962852 |
| 2 | 1930 | 2720 | 0.438782 | 0.0445258 | 0.102848 |
| 4 | 1100 | 1730 | 0.385527 | 0.0453816 | 0.100943 |
| 8 | 1060 | 1520 | 0.278001 | 0.0453145 | 0.093824 |

### Batch Sweep Interpretation
The expected "small batch is safer" intuition only partly holds. Batch cap 1 is stable but not best, and cap 2 is worse than cap 1 in this workload. Caps 4 and 8 are clearly better, with cap 8 giving the best median P99/P999 and the lowest SLO violation rate in this specific W2 burst setting.

This does not necessarily mean unbounded batching is safe. The implementation still has several guards active:
- global scheduled-migration budget (`AQB_EFFECTIVE_BUDGET=0.045`);
- per-core migration cap;
- per-destination cap;
- destination workload reservation;
- host cooldown;
- saturation fallback.

So `batch_cap=8` is better here because it allows a hot host to use the available budget faster during bursts, while the other guards prevent a full migration storm. The next step is to sweep batch cap in heterogeneous `rho=0.85` and `rho=0.92`, where the additional experiment showed both a regression and a strong win.

## Revised Next Plan

1. Add diagnostics before changing the policy again:
   - candidates generated per host check;
   - selected migrations per batch;
   - source and destination capacity classes;
   - destination reservation hits/rejections;
   - saturation guard activations;
   - per-destination incoming migrations over short windows.

2. Run a heterogeneous batch-size sweep at `rho=0.85` and `rho=0.92`:
   - `AQB_MAX_BATCH_PER_HOST={1,2,4,8}`;
   - compare per-seed P99/P999, migration rate, and invalid migration ratio;
   - determine whether the `rho=0.85` regression is batch-size dependent.

3. Implement global short-window destination reservation:
   - include scheduled but not-yet-arrived incoming workload in remote wait estimation;
   - decay or clear reservation when tasks arrive;
   - evaluate whether this reduces cross-source herding in heterogeneous clusters.

4. Replace fixed prefix scanning with adaptive bounded candidate windows:
   - keep `L_base=4` for low overhead;
   - allow deeper scanning up to `L_max` when cumulative workload exceeds an SLO-risk threshold;
   - cap candidates per core to preserve microsecond-scale feasibility.

5. Separate two algorithmic layers in future documentation:
   - **Batch selection**: choosing multiple tasks in one local control epoch; already implemented.
   - **Batch commit/transport**: grouping tasks by source-destination pair, amortizing fixed migration overhead, and applying destination-side batch placement; not yet implemented.

6. If batch commit is added, introduce a `MigrationBatch` model:
   - group selected tasks by `(src_host, dst_host)`;
   - model `T_batch(k) = T_net_oneway + T_host_batch + k * T_task_meta`;
   - add destination-side batch-aware placement that spreads urgent tasks across low-workload cores.

## Follow-up Optimization Directions

- Improve destination selection in heterogeneous clusters: avoid sending too many tasks to slow nodes or to a small set of locally attractive targets.
- Add burst-aware migration budgeting: dynamically adjust batch size and migration frequency according to burst intensity.
- Add diagnostic experiments: analyze per-window migration count, invalid migration ratio, destination host distribution, and budget usage.
- Compare against more baselines: queue-length-only migration, workload-aware single-task migration, random migration, and oracle-like upper bound.
