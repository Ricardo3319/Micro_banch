# Step-21: First-paper W3 heavy-tail boundary

Command: `./build-linux/simulator paper-w3`  
CSV: `artifacts/step-21-paper-w3/paper_w3.csv`  
Wall time: 32s

## Median table (W3 ρ=0.85, seeds 11,23,37,47,59)

| method | P99 | P999 | SLO | move rate | invalid | selected | moved |
|---|---:|---:|---:|---:|---:|---:|---:|
| B1 Power-of-2 | 202 | 400 | 0.09638 | 0 | 0 | 0 | 0 |
| B2 Reactive | 200 | 394 | 0.09621 | 0.02217 | 0.2978 | 0 | 0 |
| M0 Proactive | 186 | 360 | 0.08863 | 0.04029 | 0.0509 | 0 | 0 |
| **M1 AQB-PM** | **176** | **338** | **0.08409** | 0.04513 | 0.0354 | 0 | 0 |
| M2 DQB-PM | 202 | 398 | 0.09634 | 0.00000666 | 0 | 1 | 8 |

## Close?

**PASS as a negative/boundary result. Not a main-table win.**

DQB P99 does not beat anyone (0/5 vs all methods). It forms about one legal 8-task batch per run (`sparse_blocking_not_batchable_count ≈ 1.76M`). Invalid stays 0 because it almost never moves.

This is the intended claim:

> Sparse heavy-tail HoL is not a contiguous host-level batch. Per-task scoring (M1) still has a small SLO edge; DQB must not be retuned into single-request migration to chase W3.

Do not use W3 as a flagship. Keep it as the algorithm boundary next to W1 no-storm.
