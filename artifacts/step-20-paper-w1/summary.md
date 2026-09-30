# Step-20: First-paper W1 saturation (no-storm)

Command: `./build-linux/simulator paper-w1`  
CSV: `artifacts/step-20-paper-w1/paper_w1.csv`  
Wall time: 38s

## Median table (W1 ρ=0.95, seeds 11,23,37,47,59)

| method | P99 | P999 | SLO | move rate | invalid | selected | sat_guard |
|---|---:|---:|---:|---:|---:|---:|---:|
| B1 Power-of-2 | 1320 | 1580 | 0.99584 | 0 | 0 | 0 | 0 |
| B2 Reactive | 1380 | 1600 | 0.99632 | 0.000411 | 0.4026 | 0 | 0 |
| M0 Proactive | 1500 | 2700 | 0.99147 | 0.039414 | 0.3793 | 0 | 0 |
| M1 AQB-PM | 1370 | 1580 | 0.99673 | 0.017548 | 0.2492 | 0 | 0 |
| **M2 DQB-PM** | **1310** | **1560** | **0.99580** | **0** | **0** | **0** | **3264062** |

## Close?

**PASS.** DQB selected=0 and move rate=0 on 5/5 seeds. Saturation guard fires ~3.26M times per seed. `dst_tail_harm` is 0 because the global guard trips first.

M0 still migrates ~4% with invalid ~0.38 (storm). M1 migrates ~1.8% with invalid ~0.25. DQB matches B1 latency and does not dump work.

This is the same boundary as W2 seed 47, just fully clustered: no slack → do not migrate → do not create capacity.
