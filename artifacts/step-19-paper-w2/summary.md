# Step-19: First-paper W2 flagship rerun

Date: 2026-09-22  
Command: `./build-linux/simulator paper-w2`  
Wall time: 68s  
CSV: `artifacts/step-19-paper-w2/paper_w2.csv`

## Median table (seeds 11,23,37,47,59)

| method | P99 | P999 | SLO | move rate | invalid |
|---|---:|---:|---:|---:|---:|
| B1 Power-of-2 | 1160 | 1870 | 0.36059 | 0 | 0 |
| B2 Reactive | 1610 | 2270 | 0.46851 | 0.01236 | 0.22130 |
| M0 Proactive | 1420 | 2010 | 0.38668 | 0.04001 | 0.14506 |
| M1 AQB-PM | 1100 | 1730 | 0.38553 | 0.04538 | 0.10094 |
| **M2 DQB-PM** | **358** | **572** | **0.31124** | **0.01156** | **0.02903** |

This reproduces the previously frozen W2 medians for B2/M0/M1/M2 exactly. B1 is new in this matrix.

## Can the conclusion close?

**Yes, for the repair-vs-repair claim. Not yet for a universal beat-dispatch claim.**

DQB median P99 vs M1 is −67.5% (1100→358), vs M0 −74.8%, vs B2 −77.8%, vs B1 −69.1%.  
DQB migrates *less* than M0/M1 and has the lowest invalid ratio among methods that migrate.

Seed-direction (frozen rule: ≥4/5 same direction):

| comparison | P99 wins | SLO wins | median >5% | close? |
|---|---:|---:|---|---|
| DQB vs M1 AQB | 5/5 | 5/5 | yes | **PASS** |
| DQB vs M0 | 4/5 | 4/5 | yes | **PASS** |
| DQB vs B2 | 3/5 | 3/5 | yes | median only |
| DQB vs B1 | 3/5 | 2/5 | yes | **not closed** |

## Seed 47 / 59 diagnosis

Reject counters (DQB only):

| seed | P99 | src depth | dst occupancy | candidates | selected | `dst_tail_harm` | `target_plan_reject` | `low_expected_gain` |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 11 | 336 | 163 | 200 | 0.24M | 1920 | 0.29M | 0.91M | 0.63M |
| 23 | 358 | 270 | 421 | 0.84M | 1444 | 2.92M | 3.30M | 0.38M |
| 37 | 334 | 151 | 220 | 0.25M | 1201 | 0.33M | 0.98M | 0.65M |
| **47** | **2610** | **444** | **736** | **10.9M** | **924** | **42.6M** | **42.9M** | 0.32M |
| **59** | **772** | **246** | **419** | **4.5M** | **150** | **16.9M** | **17.7M** | 0.75M |

Control path (not a random crash):

1. Destination sample is only `k_dst=4` random hosts.
2. If stale `avg_remote_wait > DQB_TARGET_HARM_LIMIT_US` (250us), the plan is dropped as `DST_TAIL_HARM`.
3. `reservation_reject` and `saturation_guard` are **0** on all five seeds. The stopper is target-harm, not the W1-style global guard.

**Seed 47 is cluster-wide hot, not a DQB bug.** Same seed: B1 P99=2580, M1=2860, B2=1680. Only M0 (P99=866) wins by migrating 4x more without the 250us host-average harm gate. DQB still forms real 8–53 batches, but source queues are ~3x deeper than the good seeds, so 14k moves cannot drain the burst. This is the same *no slack → do not dump* boundary as W1 saturation, just not yet fully clustered.

**Seed 59 is not a loss.** DQB P99=772 still beats B1 1160 / M1 1100 / M0 2590 / B2 4430. It only looks bad next to the 334–358us hero seeds. Low `selected=150` is the harm gate firing while dest occupancy sits at 419us (>250).

So the 358us median is the *local-slack* W2 story. The outliers are *few/no cold hosts*. Frozen 4/5 vs B1 fails because of seed 47 (global hot) plus B1 seed 23 already matching DQB.

## Paper sentence that the data actually support

> When bursty arrivals leave some hosts with slack, DQB batch-repairs waiting queues at lower move rate and lower invalid ratio than per-task scoring. When the cluster is globally hot, DQB refuses most targets; it does not create capacity.

Do not retune `TARGET_HARM_LIMIT` or `k_dst` just to make seed 47 look like 358us — that would collapse the negative-case story.

## Next

W1 ρ=0.95 no-storm rerun, then write seed 47 as the W2 instance of that boundary. Leave RescueSched closed.
