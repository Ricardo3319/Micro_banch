# Step-11 DQB Diagnostic Closure

Date: 2026-05-08

## Goal

Continue the DQB closed loop by adding the diagnostic fields required before
DQB-v2 and validating that the current DQB-v1 behavior remains explainable on
W1/W2/W3.

## Implementation

Added Phase-A diagnostics to `MetricsCollector` and focused CSV outputs:

- short/long and mice/elephant SLO violation rates
- migration work rate and migrated work
- exact batch-size histogram
- no-migrate reason counters
- source queue depth/work summaries
- destination virtual occupancy and target harm estimate
- estimated summary, batch-estimation, and target-selection costs

Also fixed two validation blockers:

- Replaced the dynamic exact batch-size map with a fixed bounded histogram.
- Moved W1 saturation/budget no-migrate guard before queue-summary construction.

The second change is algorithmically important: under W1 saturation, DQB now
decides no-migrate using low-state pressure before scanning candidates. This is
the intended bounded-control behavior.

## Validation Commands

```powershell
.\build-aqb-check\simulator.exe noop
.\build-aqb-check\simulator.exe regression
.\build-aqb-check\simulator.exe aqb-smoke
.\build-aqb-check\simulator.exe dqb-w2-only
.\build-aqb-check\simulator.exe dqb-w3-only
.\build-aqb-check\simulator.exe dqb-w1-only
```

Build note: full Ninja had stale process/lock behavior in this workspace, so
the final binary was refreshed with single-step compile/archive/link commands.
The archive was checked to contain a single `simulator.cpp.obj`.

## Focused Diagnostic Results

Median over seeds `{11,23,37,47,59}`:

| Scenario | P99 us | P999 us | migration rate | batch candidates | selected batches | moved requests | saturation guards |
|---|---:|---:|---:|---:|---:|---:|---:|
| W2 burst rho=0.85 | 358 | 572 | 0.0115643 | 840591 | 1201 | 14446 | 0 |
| W3 heavy-tail rho=0.85 | 202 | 398 | 0.00000666 | 1 | 1 | 8 | 0 |
| W1 saturation rho=0.95 | 1310 | 1560 | 0 | 0 | 0 | 0 | 3264062 |

## Analysis

W2 remains the positive case. DQB selects real batch moves, mostly in the 8+
request range, with migration rate around 1.16%.

W3 remains the sparse heavy-tail boundary. The algorithm can form an occasional
8-request batch, but the candidate rate is too low to move aggregate P99/P999.

W1 is now a clean no-migrate boundary. The algorithm emits saturation guards,
builds no candidate batches, and moves no work. This supports the claim that
DQB does not waste migration budget when system-wide pressure is high.

## Next Experiment

Use these diagnostics to run the next DQB-v1 matrix:

- W2 burst at rho `0.70`, `0.85`, `0.92`
- W3 heavy-tail at rho `0.70`, `0.85`, `0.92`
- heterogeneous W2/W3 at rho `0.70`, `0.85`, `0.92`

Then implement DQB-v2 descriptors and compare:

- `DQB-v2/full`
- `DQB-v2/prior-only`
- `DQB-v2/summary-only`
- `DQB-v2/no-reservation`
- `DQB-v2/no-saturation-guard`
