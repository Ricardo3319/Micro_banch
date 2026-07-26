# RescueSched INFOCOM local physical holdout report

**Artifact:** `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z`  
**Experiment start:** 2026-07-26 09:21:43 UTC  
**Final validation date:** 2026-07-26  
**Branch:** `codex/infocom2027-integration`  
**Commit:** `9fdaf5679dfecfd98b573f4ab9b14085258b7bf3`  
**Source state:** dirty, uncommitted, unstaged; all runs record `dirty=1`  
**Scope:** local synthetic physical/runtime plus single-host loopback UDP RPC only

## 1. Bottom line

The holdout data support a **workload- and environment-qualified** conclusion:

* On all seven synthetic setups, FullHybrid improves deadline-violation rate, SLO goodput, server P99, and server P999 relative to FIFO in all 35 paired holdout-seed comparisons. Every setup-level paired-bootstrap 95% CI excludes zero in the improvement direction.
* Relative to M0, FullHybrid improves miss rate and goodput on all seven setups and improves P99 on all seven at the bootstrap-CI level, but W2 P999 is unresolved/slightly regressive: W2-rho070 mean P999 regression 18.885 us, CI [-40.951, 5.546] in candidate-improvement convention; W2-rho085 regression 11.634 us, CI [-55.235, 25.934].
* Relative to L1Only, FullHybrid improves miss rate/goodput on six of seven setups, but regresses W3-rho070 miss rate by 0.472 percentage points and goodput by 1,958.5 rps. FullHybrid also frequently regresses P99/P999 versus L1Only. Therefore the data do **not** support a blanket tail-latency advantage for the full hybrid.
* In the four strict ablation setups, FullHybrid beats CentralOnly on miss rate, goodput, P99, and P999 in all 20 seed pairs, but pays substantially more migrations, repeated migrations, control-path work, and scheduler lag.
* The real loopback RPC result is negative for distributed L1/full hybrid at this configuration. FullHybrid versus FIFO regresses miss rate by 8.48 percentage points, server goodput by 2,208.1 rps, client P99 RTT by 4,350.6 us, and client P999 RTT by 5,594.6 us on average. These results are retained and are not filtered.
* Scheduler CPU time was **not directly measured**. Decision wall duration, cycles, scan counts, control-path duration, missed epochs, and scheduler lag are reported only as proxies/overhead indicators.

With only five paired seeds per setup, the exact two-sided sign-flip test has minimum attainable p=0.0625. Thus even 5/5 aligned pairs do not reach alpha=0.05. Statements below distinguish a bootstrap CI excluding zero from exact-test significance.

## 2. Protocol and provenance

### 2.1 Holdout and fixed parameters

* Holdout seeds: `71,83,97,109,127`.
* Development/tuning seeds: `11,23,37,47,59`; the sets are disjoint.
* No final parameter scan was run.
* Fixed M1 tuple in every final row and manifest: `moves=4`, `epsilon=6 us`, `lookahead=20 us`, `idle-pulls=0`.
* Bootstrap: paired by holdout seed, 10,000 draws, seed `2026072603`.
* Delta convention in `aggregate.csv` and `bootstrap_95ci.csv`: positive means the candidate is better. For misses and latency this is baseline minus candidate; for goodput it is candidate minus baseline.

### 2.2 Environments

Synthetic physical runtime:

* 5,500 requests/run, 500 warmup, 5,000 measured.
* Strict worker affinity, `time_scale=20`, logical scheduler period 5 us.
* W2 at rho 0.70/0.85 with 16 workers.
* W3 at rho 0.70/0.85/0.90 with 16 workers, plus rho 0.85 with 8/12 workers.

Loopback RPC:

* Real UDP client/server on one host, 8 worker CPUs, two receiver CPUs, two response-sender CPUs, separate scheduler/server-main/client sender/client receiver CPUs.
* `arrival_scale=10`, `time_scale=1`, scheduler period 100 us.
* This is low-load implementation/path evidence, **not** a formal rho=0.85 deployment result and not CloudLab/two-host evidence.
* All 25 runs exceed the original client send-lag P99 4-us warning threshold; the warning is retained, non-exclusionary, and visible in every RPC row.

No S3, CloudLab, remote-host, host tuning, kernel tuning, or deployment tuning was performed.

## 3. Run completeness and correctness

* 160/160 synthetic runs and 25/25 RPC runs are `VALID_COMPLETE` and correctness-valid: 185/185 total.
* Forty setup/seed cells have identical embedded and input trace hashes across all compared variants.
* All runs: 5,500/5,500 completed, 0 cancelled, 0 duplicate execution, 0 duplicate completion, 0 lost descriptors, 0 nonzero reservations, 0 affinity failures, and 0 dropped decision records.
* Every RPC run: 5,500 accepted/enqueued/sent/received, 0 timeout, send failure, invalid/duplicate response, invalid/duplicate packet, unknown request, flow mismatch, enqueue/send failure, or receiver internal failure. All affinity checks pass.
* No outlier or failed attempt is silently removed. Failed protocols and harness mistakes are preserved separately and excluded from final estimates for stated protocol reasons.

Primary machine-readable sources:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/raw_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/paired_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/aggregate.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/correctness_invariant_detail.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/holdout_trace_audit.csv`

## 4. Same-environment variant medians

These are medians across the five holdout seeds; paired deltas and uncertainty follow in later sections.

| setup | variant | n | miss | SLO goodput rps | server P99 us | server P999 us | client P99 RTT us | client P999 RTT us |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| W2-rho070-w16 | FIFO | 5 | 56.08% | 183,624 | 690.135 | 879.036 | — | — |
| W2-rho070-w16 | M0 | 5 | 8.50% | 387,689 | 194.400 | 204.531 | — | — |
| W2-rho070-w16 | L1Only | 5 | 6.66% | 395,896 | 183.340 | 226.188 | — | — |
| W2-rho070-w16 | FullHybrid | 5 | 6.14% | 398,582 | 179.881 | 224.530 | — | — |
| W2-rho085-w16 | FIFO | 5 | 71.34% | 137,231 | 1,011.3 | 1,216.2 | — | — |
| W2-rho085-w16 | M0 | 5 | 17.60% | 415,562 | 199.784 | 244.507 | — | — |
| W2-rho085-w16 | L1Only | 5 | 16.28% | 426,018 | 183.002 | 259.236 | — | — |
| W2-rho085-w16 | CentralOnly | 5 | 11.46% | 450,498 | 215.378 | 297.926 | — | — |
| W2-rho085-w16 | FullHybrid | 5 | 7.30% | 471,839 | 189.906 | 255.834 | — | — |
| W3-rho070-w16 | FIFO | 5 | 40.70% | 245,341 | 1,035.8 | 1,354.5 | — | — |
| W3-rho070-w16 | M0 | 5 | 6.70% | 392,380 | 234.093 | 433.895 | — | — |
| W3-rho070-w16 | L1Only | 5 | 1.42% | 415,662 | 151.099 | 304.426 | — | — |
| W3-rho070-w16 | CentralOnly | 5 | 3.62% | 404,559 | 225.555 | 442.770 | — | — |
| W3-rho070-w16 | FullHybrid | 5 | 1.80% | 413,771 | 156.334 | 302.476 | — | — |
| W3-rho085-w8 | FIFO | 5 | 61.16% | 99,264.1 | 1,227.4 | 1,536.8 | — | — |
| W3-rho085-w8 | M0 | 5 | 18.00% | 208,077 | 271.380 | 457.625 | — | — |
| W3-rho085-w8 | L1Only | 5 | 13.40% | 219,733 | 204.856 | 347.598 | — | — |
| W3-rho085-w8 | FullHybrid | 5 | 9.62% | 229,475 | 210.496 | 354.969 | — | — |
| W3-rho085-w12 | FIFO | 5 | 58.74% | 154,628 | 878.058 | 1,085.7 | — | — |
| W3-rho085-w12 | M0 | 5 | 13.94% | 330,110 | 237.191 | 450.945 | — | — |
| W3-rho085-w12 | L1Only | 5 | 8.18% | 347,788 | 171.510 | 325.795 | — | — |
| W3-rho085-w12 | FullHybrid | 5 | 6.04% | 360,900 | 180.463 | 321.647 | — | — |
| W3-rho085-w16 | FIFO | 5 | 61.50% | 183,250 | 1,292.2 | 1,665.3 | — | — |
| W3-rho085-w16 | M0 | 5 | 11.32% | 444,978 | 256.857 | 432.440 | — | — |
| W3-rho085-w16 | L1Only | 5 | 6.44% | 475,227 | 162.845 | 310.485 | — | — |
| W3-rho085-w16 | CentralOnly | 5 | 7.66% | 466,036 | 246.626 | 428.720 | — | — |
| W3-rho085-w16 | FullHybrid | 5 | 3.96% | 484,907 | 163.312 | 310.207 | — | — |
| W3-rho090-w16 | FIFO | 5 | 68.00% | 152,046 | 1,433.1 | 1,723.7 | — | — |
| W3-rho090-w16 | M0 | 5 | 18.36% | 438,896 | 266.119 | 463.572 | — | — |
| W3-rho090-w16 | L1Only | 5 | 12.92% | 472,274 | 177.575 | 314.660 | — | — |
| W3-rho090-w16 | CentralOnly | 5 | 12.76% | 457,043 | 270.145 | 529.497 | — | — |
| W3-rho090-w16 | FullHybrid | 5 | 8.12% | 497,680 | 182.365 | 338.528 | — | — |

## 5. Paired synthetic outcomes

Each cell is `paired mean candidate improvement [paired-bootstrap 95% CI]; wins/losses; exact two-sided sign-flip p`. Positive is better for the candidate. Full effect sizes (`Cohen dz` and paired rank-biserial) for every row are in `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv`.

### 5.1 FullHybrid versus FIFO

| setup | miss Δ(pp) | goodput Δ(rps) | P99 Δ(us) | P999 Δ(us) |
|---|---:|---:|---:|---:|
| W2-rho070-w16 | 50.472 [47.772, 53.928]; 5/0; p=0.0625 | 221,190 [202,056, 245,164]; 5/0; p=0.0625 | 526.235 [410.697, 641.774]; 5/0; p=0.0625 | 616.270 [485.731, 746.809]; 5/0; p=0.0625 |
| W2-rho085-w16 | 64.356 [59.576, 68.396]; 5/0; p=0.0625 | 335,448 [318,250, 352,646]; 5/0; p=0.0625 | 889.162 [751.107, 1,027.2]; 5/0; p=0.0625 | 982.042 [837.389, 1,125.9]; 5/0; p=0.0625 |
| W3-rho070-w16 | 37.708 [35.984, 39.432]; 5/0; p=0.0625 | 167,773 [157,102, 177,547]; 5/0; p=0.0625 | 725.986 [497.707, 925.103]; 5/0; p=0.0625 | 896.690 [580.561, 1,194.0]; 5/0; p=0.0625 |
| W3-rho085-w8 | 48.964 [46.704, 51.224]; 5/0; p=0.0625 | 128,799 [124,366, 134,057]; 5/0; p=0.0625 | 958.676 [725.188, 1,192.2]; 5/0; p=0.0625 | 990.285 [701.118, 1,234.9]; 5/0; p=0.0625 |
| W3-rho085-w12 | 53.304 [51.616, 54.756]; 5/0; p=0.0625 | 209,481 [201,230, 217,733]; 5/0; p=0.0625 | 790.936 [553.775, 1,042.8]; 5/0; p=0.0625 | 912.849 [593.929, 1,234.0]; 5/0; p=0.0625 |
| W3-rho085-w16 | 55.048 [53.252, 56.844]; 5/0; p=0.0625 | 297,452 [283,743, 306,658]; 5/0; p=0.0625 | 1,063.8 [822.659, 1,271.4]; 5/0; p=0.0625 | 1,226.1 [865.905, 1,539.3]; 5/0; p=0.0625 |
| W3-rho090-w16 | 58.644 [56.800, 60.456]; 5/0; p=0.0625 | 336,913 [325,720, 347,673]; 5/0; p=0.0625 | 1,214.8 [1,033.8, 1,388.2]; 5/0; p=0.0625 | 1,378.4 [1,079.4, 1,719.6]; 5/0; p=0.0625 |

All 28 setup/metric CIs exclude zero in FullHybrid's favor; all four outcome metrics are 35/0 over seed pairs. Exact p is 0.0625 for each 5/5 row, so alpha=0.05 is not reached.

### 5.2 FullHybrid versus M0

| setup | miss Δ(pp) | goodput Δ(rps) | P99 Δ(us) | P999 Δ(us) |
|---|---:|---:|---:|---:|
| W2-rho070-w16 | 3.108 [1.400, 4.980]; 5/0; p=0.0625 | 15,365.5 [7,458.9, 24,856.8]; 5/0; p=0.0625 | 32.419 [9.889, 56.815]; 5/0; p=0.0625 | -18.885 [-40.951, 5.546]; 1/4; p=0.25 |
| W2-rho085-w16 | 9.652 [6.564, 13.036]; 5/0; p=0.0625 | 51,812.6 [33,660.8, 68,805.8]; 5/0; p=0.0625 | 25.885 [0.398, 51.373]; 4/1; p=0.1875 | -11.634 [-55.235, 25.934]; 2/3; p=0.75 |
| W3-rho070-w16 | 4.792 [4.508, 5.060]; 5/0; p=0.0625 | 24,933.2 [20,805.9, 30,003.0]; 5/0; p=0.0625 | 77.245 [63.970, 88.125]; 5/0; p=0.0625 | 174.495 [94.485, 295.217]; 5/0; p=0.0625 |
| W3-rho085-w8 | 8.432 [7.684, 9.256]; 5/0; p=0.0625 | 22,467.5 [20,214.0, 24,784.0]; 5/0; p=0.0625 | 57.564 [50.974, 66.375]; 5/0; p=0.0625 | 153.224 [86.064, 238.059]; 5/0; p=0.0625 |
| W3-rho085-w12 | 7.924 [7.444, 8.420]; 5/0; p=0.0625 | 32,282.9 [29,715.1, 34,625.7]; 5/0; p=0.0625 | 73.242 [58.852, 91.671]; 5/0; p=0.0625 | 175.476 [102.876, 306.665]; 5/0; p=0.0625 |
| W3-rho085-w16 | 7.012 [6.536, 7.384]; 5/0; p=0.0625 | 39,815.8 [35,327.0, 43,342.3]; 5/0; p=0.0625 | 85.065 [76.688, 93.990]; 5/0; p=0.0625 | 196.773 [103.761, 349.028]; 5/0; p=0.0625 |
| W3-rho090-w16 | 10.688 [8.984, 12.520]; 5/0; p=0.0625 | 61,896.0 [50,490.1, 73,302.0]; 5/0; p=0.0625 | 74.461 [64.455, 84.428]; 5/0; p=0.0625 | 179.241 [106.538, 313.034]; 5/0; p=0.0625 |

The two W2 P999 rows are unresolved and have negative mean candidate improvement. All other M0 outcome rows shown above have positive mean improvement; several CIs, especially W2 tails, overlap zero.

### 5.3 FullHybrid versus L1Only

| setup | miss Δ(pp) | goodput Δ(rps) | P99 Δ(us) | P999 Δ(us) |
|---|---:|---:|---:|---:|
| W2-rho070-w16 | 2.188 [0.336, 4.420]; 4/1; p=0.125 | 9,705.6 [1,385.5, 20,208.0]; 4/1; p=0.125 | -3.328 [-9.115, 3.247]; 2/3; p=0.375 | -16.386 [-31.730, -3.976]; 1/4; p=0.125 |
| W2-rho085-w16 | 7.616 [5.816, 9.328]; 5/0; p=0.0625 | 38,489.5 [28,957.7, 47,365.9]; 5/0; p=0.0625 | -12.762 [-33.317, 2.911]; 1/4; p=0.3125 | -19.402 [-49.394, -0.051]; 1/4; p=0.1875 |
| W3-rho070-w16 | -0.472 [-0.680, -0.260]; 0/5; p=0.0625 | -1,958.5 [-2,760.6, -1,098.2]; 0/5; p=0.0625 | -3.762 [-6.302, -1.734]; 0/5; p=0.0625 | 0.194 [-0.659, 1.158]; 2/3; p=0.75 |
| W3-rho085-w8 | 4.000 [3.452, 4.600]; 5/0; p=0.0625 | 10,343.1 [8,847.5, 12,007.5]; 5/0; p=0.0625 | -11.057 [-16.358, -6.119]; 0/5; p=0.0625 | -27.710 [-83.189, 16.946]; 1/4; p=0.5 |
| W3-rho085-w12 | 3.172 [2.524, 3.896]; 5/0; p=0.0625 | 12,195.6 [9,678.8, 15,250.0]; 5/0; p=0.0625 | -5.370 [-10.504, 0.129]; 1/4; p=0.1875 | -5.426 [-12.743, 0.762]; 1/4; p=0.1875 |
| W3-rho085-w16 | 2.628 [1.804, 3.452]; 5/0; p=0.0625 | 13,502.4 [9,081.5, 17,923.2]; 5/0; p=0.0625 | -3.544 [-6.296, -1.240]; 0/5; p=0.0625 | -6.946 [-17.008, -0.281]; 1/4; p=0.1875 |
| W3-rho090-w16 | 5.172 [4.044, 6.928]; 5/0; p=0.0625 | 28,080.5 [21,353.3, 37,990.8]; 5/0; p=0.0625 | -8.843 [-17.939, -2.984]; 0/5; p=0.0625 | -4.464 [-17.479, 12.211]; 1/4; p=0.625 |

This comparison exposes the principal synthetic tradeoff: central rescue usually reduces misses but often worsens high quantiles relative to distributed L1 alone. W3-rho070 is a direct outcome regression, not merely an overhead increase.

### 5.4 Mechanism ablation: FullHybrid versus CentralOnly

| setup | miss Δ(pp) | goodput Δ(rps) | P99 Δ(us) | P999 Δ(us) |
|---|---:|---:|---:|---:|
| W2-rho085-w16 | 2.872 [1.984, 3.656]; 5/0; p=0.0625 | 16,650.0 [12,539.6, 19,779.0]; 5/0; p=0.0625 | 35.586 [26.844, 44.328]; 5/0; p=0.0625 | 47.825 [20.599, 80.909]; 5/0; p=0.0625 |
| W3-rho070-w16 | 1.660 [1.500, 1.868]; 5/0; p=0.0625 | 9,381.6 [7,628.8, 10,804.4]; 5/0; p=0.0625 | 70.098 [59.851, 79.555]; 5/0; p=0.0625 | 177.101 [95.428, 304.887]; 5/0; p=0.0625 |
| W3-rho085-w16 | 3.200 [2.868, 3.568]; 5/0; p=0.0625 | 19,710.1 [16,495.9, 23,283.3]; 5/0; p=0.0625 | 80.920 [74.266, 88.442]; 5/0; p=0.0625 | 185.012 [96.377, 315.105]; 5/0; p=0.0625 |
| W3-rho090-w16 | 6.044 [4.856, 7.660]; 5/0; p=0.0625 | 37,153.9 [29,159.9, 46,505.3]; 5/0; p=0.0625 | 80.848 [74.817, 86.879]; 5/0; p=0.0625 | 221.798 [128.151, 340.672]; 5/0; p=0.0625 |

FullHybrid is directionally better on all four outcomes and all 20 seed pairs, while using the distributed L1 path in addition to central rescue.

### 5.5 Mechanism ablation: CentralOnly versus L1Only

| setup | miss Δ(pp) | goodput Δ(rps) | P99 Δ(us) | P999 Δ(us) |
|---|---:|---:|---:|---:|
| W2-rho085-w16 | 4.744 [3.376, 6.136]; 5/0; p=0.0625 | 21,839.5 [15,105.4, 28,694.6]; 5/0; p=0.0625 | -48.349 [-76.490, -24.991]; 0/5; p=0.0625 | -67.227 [-127.077, -27.392]; 0/5; p=0.0625 |
| W3-rho070-w16 | -2.132 [-2.324, -1.912]; 0/5; p=0.0625 | -11,340.2 [-12,729.5, -9,950.8]; 0/5; p=0.0625 | -73.860 [-82.478, -65.130]; 0/5; p=0.0625 | -176.907 [-303.741, -95.926]; 0/5; p=0.0625 |
| W3-rho085-w16 | -0.572 [-1.464, 0.320]; 2/3; p=0.375 | -6,207.7 [-11,788.0, -627.464]; 1/4; p=0.1875 | -84.464 [-94.541, -77.463]; 0/5; p=0.0625 | -191.958 [-319.799, -108.909]; 0/5; p=0.0625 |
| W3-rho090-w16 | -0.872 [-1.440, -0.248]; 1/4; p=0.125 | -9,073.3 [-13,619.4, -4,527.3]; 0/5; p=0.0625 | -89.691 [-102.463, -79.306]; 0/5; p=0.0625 | -226.262 [-352.972, -135.755]; 0/5; p=0.0625 |

CentralOnly improves W2-rho085 misses/goodput but regresses P99/P999. On W3, its tails are consistently worse, and miss/goodput are usually worse or unresolved. CentralOnly has 0/20 P99 and 0/20 P999 wins against L1Only across these four setups.

## 6. Holdout generalization across worker/load setups

| comparison | metric | setups better / worse / overlap | seed-pair W/L/T |
|---|---|---:|---:|
| FullHybrid_vs_FIFO | deadline_violation_rate | 7/0/0 | 35/0/0 |
| FullHybrid_vs_FIFO | server_p999_us | 7/0/0 | 35/0/0 |
| FullHybrid_vs_FIFO | server_p99_us | 7/0/0 | 35/0/0 |
| FullHybrid_vs_FIFO | slo_goodput_rps | 7/0/0 | 35/0/0 |
| FullHybrid_vs_L1Only | deadline_violation_rate | 6/1/0 | 29/6/0 |
| FullHybrid_vs_L1Only | server_p999_us | 0/3/4 | 8/27/0 |
| FullHybrid_vs_L1Only | server_p99_us | 0/4/3 | 4/31/0 |
| FullHybrid_vs_L1Only | slo_goodput_rps | 6/1/0 | 29/6/0 |
| FullHybrid_vs_M0 | deadline_violation_rate | 7/0/0 | 35/0/0 |
| FullHybrid_vs_M0 | server_p999_us | 5/0/2 | 28/7/0 |
| FullHybrid_vs_M0 | server_p99_us | 7/0/0 | 34/1/0 |
| FullHybrid_vs_M0 | slo_goodput_rps | 7/0/0 | 35/0/0 |

Coverage includes W3 rho=0.85 with 8/12/16 workers, W3 with rho=0.70/0.85/0.90 at 16 workers, and W2 with rho=0.70/0.85 at 16 workers. FullHybrid-versus-FIFO direction is stable over this tested matrix. The effects are heterogeneous and were intentionally not pooled into a universal effect; no claim is made beyond the tested host, traces, worker counts, and loads.

Detailed files:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/worker_load_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/generalization_directional_summary.csv`

## 7. Mechanism cost and scheduler overhead

### 7.1 Synthetic ablation medians

| setup | variant | migrations | repeats | max migrations/request | central commits | central success | control ns/request | scheduler missed | max lag us | handoff P99 ns | scans/request |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| W2-rho070-w16 | L1Only | 3,241.0 | 184 | 4 | 0 | — | 7,085.1 | — | 0.00 | 10,609.3 | 0.809 |
| W2-rho070-w16 | FullHybrid | 3,422.0 | 238 | 5 | 904 | 1.000 | 11,551.3 | 0.01176 | 79.33 | 15,073.2 | 7.345 |
| W2-rho085-w16 | L1Only | 2,824.0 | 96 | 4 | 0 | — | 3,642.0 | — | 0.00 | 8,463.6 | 0.684 |
| W2-rho085-w16 | CentralOnly | 2,470.0 | 0 | 1 | 2,701.0 | 1.000 | 2,330.1 | 0.00184 | 14.14 | 10,601.6 | 16.747 |
| W2-rho085-w16 | FullHybrid | 3,346.0 | 96 | 4 | 1,440.0 | 1.000 | 5,533.9 | 0.00511 | 28.33 | 10,789.6 | 11.251 |
| W3-rho070-w16 | L1Only | 3,516.0 | 216 | 4 | 0 | — | 7,111.9 | — | 0.00 | 9,973.8 | 0.875 |
| W3-rho070-w16 | CentralOnly | 1,935.0 | 0 | 1 | 2,108.0 | 0.966 | 2,414.6 | 0.00192 | 15.40 | 10,862.4 | 12.631 |
| W3-rho070-w16 | FullHybrid | 3,846.0 | 449 | 4 | 635 | 0.945 | 11,158.3 | 0.01011 | 69.42 | 12,444.4 | 2.786 |
| W3-rho085-w8 | L1Only | 2,657.0 | 39 | 3 | 0 | — | 2,429.1 | — | 0.00 | 2,174.0 | 0.610 |
| W3-rho085-w8 | FullHybrid | 3,386.0 | 279 | 3 | 1,297.0 | 0.867 | 5,038.7 | 0.00258 | 44.53 | 6,255.4 | 8.763 |
| W3-rho085-w12 | L1Only | 3,041.0 | 58 | 3 | 0 | — | 3,199.3 | — | 0.00 | 6,949.0 | 0.702 |
| W3-rho085-w12 | FullHybrid | 3,787.0 | 348 | 4 | 1,297.0 | 0.883 | 5,385.5 | 0.00354 | 21.71 | 8,770.0 | 7.665 |
| W3-rho085-w16 | L1Only | 3,344.0 | 110 | 4 | 0 | — | 4,085.5 | — | 0.00 | 8,590.0 | 0.787 |
| W3-rho085-w16 | CentralOnly | 2,226.0 | 0 | 1 | 2,433.0 | 0.952 | 2,507.9 | 0.00233 | 16.76 | 10,293.1 | 21.048 |
| W3-rho085-w16 | FullHybrid | 4,035.0 | 457 | 4 | 1,209.0 | 0.906 | 6,092.5 | 0.00612 | 28.84 | 10,207.8 | 6.993 |
| W3-rho090-w16 | L1Only | 3,003.0 | 68 | 4 | 0 | — | 3,050.4 | — | 0.00 | 7,983.4 | 0.698 |
| W3-rho090-w16 | CentralOnly | 2,218.0 | 0 | 1 | 2,445.0 | 0.931 | 2,277.0 | 0.00197 | 12.22 | 9,318.6 | 27.707 |
| W3-rho090-w16 | FullHybrid | 3,908.0 | 391 | 4 | 1,524.0 | 0.888 | 5,550.0 | 0.00431 | 28.62 | 10,315.4 | 10.801 |

FullHybrid medians span 3,346–4,035 migrations/run, 96–457 repeats/run, and 635–1,524 central commits/run. Central rescue success is about 1.0 for W2 and 0.867–0.945 for W3. FullHybrid control-path duration is about 5.0–11.6 us/request, scheduler missed fraction 0.00258–0.01176, scheduler maximum-lag median 21.7–79.3 us, and handoff P99 median about 6.3–15.1 us.

These values do not support a low-migration or low-overhead claim.

### 7.2 Cost per net avoided miss versus FIFO

| setup | net avoided misses | migrations / net avoided miss | central commits / net avoided miss |
|---|---:|---:|---:|
| W2-rho070-w16 | 2,413.0 | 1.316 | 0.365 |
| W2-rho085-w16 | 3,289.0 | 1.068 | 0.466 |
| W3-rho070-w16 | 1,918.0 | 2.023 | 0.331 |
| W3-rho085-w8 | 2,444.0 | 1.332 | 0.538 |
| W3-rho085-w12 | 2,721.0 | 1.392 | 0.501 |
| W3-rho085-w16 | 2,783.0 | 1.429 | 0.436 |
| W3-rho090-w16 | 2,915.0 | 1.289 | 0.525 |

The migration cost is 1.068–2.023 migrations per net avoided miss across synthetic setups; central-commit cost is 0.331–0.539 per net avoided miss. `net avoided miss` is paired request-level accounting versus FIFO. In RPC, FullHybrid has **negative** net avoided misses (median -404), so a positive migrations-per-avoided-miss ratio is undefined and is not reported.

Complete mechanism data:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_cost_aggregate.csv`

## 8. Tail decomposition and regressions

### 8.1 Same request class: FullHybrid versus L1Only

Rows below are the same-trace short/long request classes whose paired-bootstrap CI lies wholly in the regression direction. The candidate-improvement number is therefore negative.

| setup | class | quantile | FullHybrid improvement vs L1Only us | bootstrap 95% CI | exact p | dz | W/L |
|---|---|---|---:|---|---:|---:|---:|
| W2-rho085-w16 | long | p99 | -11.691 | [-18.507, -4.194] | 0.125 | -1.284 | 1/4 |
| W2-rho085-w16 | long | p999 | -19.554 | [-52.399, -1.382] | 0.0625 | -0.533 | 0/5 |
| W3-rho070-w16 | short | p99 | -13.802 | [-22.804, -8.468] | 0.0625 | -1.390 | 0/5 |
| W3-rho085-w8 | long | p999 | -20.312 | [-57.278, -0.442] | 0.0625 | -0.496 | 0/5 |
| W3-rho085-w12 | short | p999 | -33.435 | [-48.860, -21.045] | 0.0625 | -1.870 | 0/5 |
| W3-rho090-w16 | long | p99 | -12.644 | [-17.476, -8.919] | 0.0625 | -2.360 | 0/5 |
| W3-rho090-w16 | short | p999 | -51.698 | [-86.390, -19.319] | 0.0625 | -1.183 | 0/5 |

This locates, rather than hides, tail regressions: short-request P99 at W3-rho070, long-request P99/P999 at W2-rho085, short-request P999 at W3-rho085-w12, long-request P999 at W3-rho085-w8, and long-P99/short-P999 at W3-rho090.

### 8.2 Rescued/non-rescued and migrated/non-migrated groups

`tail_group_summary.csv` includes all, short/long, central-rescued/non-rescued, migrated/non-migrated, and method×rescue groups with P99, P999, miss rate, counts, bootstrap CIs, and small-group warnings. `within_variant_tail_contrasts.csv` marks every rescue/migration contrast as policy-selected and descriptive, not causal.

Descriptively, W2 central-rescued groups often have near-zero miss rates and migrated groups often have lower tails than non-migrated groups. This does not generalize to W3: for W3-rho085-w16 FullHybrid, median miss rate is 9.40% in the rescued group versus 2.87% in the non-rescued group. The scheduler selects high-risk requests, so these are selection-biased subpopulations and cannot establish that rescue caused the contrast.

Files:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_group_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_policy_comparisons.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_regressions.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/within_variant_tail_contrasts.csv`

## 9. Real loopback RPC end-to-end results

### 9.1 Variant medians

| setup | variant | n | miss | SLO goodput rps | server P99 us | server P999 us | client P99 RTT us | client P999 RTT us |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| RPC-W3-rho085-w8-scale10 | FIFO | 5 | 13.34% | 22,640.5 | 207.680 | 416.413 | 260.059 | 492.296 |
| RPC-W3-rho085-w8-scale10 | M0 | 5 | 13.52% | 22,556.9 | 208.602 | 408.057 | 260.830 | 471.267 |
| RPC-W3-rho085-w8-scale10 | L1Only | 5 | 17.82% | 21,419.9 | 210.326 | 376.539 | 3,230.1 | 4,857.3 |
| RPC-W3-rho085-w8-scale10 | CentralOnly | 5 | 14.96% | 22,257.6 | 202.030 | 381.407 | 260.831 | 463.642 |
| RPC-W3-rho085-w8-scale10 | FullHybrid | 5 | 21.92% | 20,316.0 | 255.962 | 422.726 | 3,871.2 | 5,382.0 |

Mechanism medians for the RPC variants:

| setup | variant | migrations | repeats | max migrations/request | central commits | central success | control ns/request | scheduler missed | max lag us | handoff P99 ns | scans/request |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| RPC-W3-rho085-w8-scale10 | L1Only | 2,333.0 | 526 | 7 | 0 | — | 29,164.6 | — | 0.00 | 25,542.1 | 0.781 |
| RPC-W3-rho085-w8-scale10 | CentralOnly | 61 | 0 | 1 | 74 | 0.328 | 2,110.9 | 0.00213 | 461.74 | 29,208.0 | 0.504 |
| RPC-W3-rho085-w8-scale10 | FullHybrid | 2,513.0 | 610 | 6 | 76 | 0.203 | 38,930.3 | 0.01686 | 4,778.8 | 38,808.0 | 1.415 |

M0 has median 42 migrations, 1.344 us/request control-path duration, 468.5 us max scheduler lag, and 0.00257 scheduler missed fraction. FIFO has no scheduler/migration overhead.

### 9.2 Paired RPC deltas, uncertainty, exact tests, and effect sizes

| comparison | metric | mean candidate improvement | bootstrap 95% CI | exact p | dz | rank-biserial | W/L |
|---|---|---:|---|---:|---:|---:|---:|
| FullHybrid_vs_FIFO | deadline_violation_rate | -8.480 | [-10.288, -6.956] | 0.0625 | -4.017 | -1.000 | 0/5 |
| FullHybrid_vs_FIFO | slo_goodput_rps | -2,208.1 | [-2,718.0, -1,818.4] | 0.0625 | -3.860 | -1.000 | 0/5 |
| FullHybrid_vs_FIFO | server_p99_us | -1,512.4 | [-4,440.5, -31.558] | 0.0625 | -0.462 | -1.000 | 0/5 |
| FullHybrid_vs_FIFO | server_p999_us | -1,683.9 | [-5,027.8, 17.711] | 0.3125 | -0.451 | -0.600 | 1/4 |
| FullHybrid_vs_FIFO | client_p99_rtt_us | -4,350.6 | [-6,074.1, -3,238.1] | 0.0625 | -2.260 | -1.000 | 0/5 |
| FullHybrid_vs_FIFO | client_p999_rtt_us | -5,594.6 | [-7,271.9, -4,594.9] | 0.0625 | -3.026 | -1.000 | 0/5 |
| FullHybrid_vs_M0 | deadline_violation_rate | -8.776 | [-10.428, -7.552] | 0.0625 | -4.746 | -1.000 | 0/5 |
| FullHybrid_vs_M0 | slo_goodput_rps | -2,285.2 | [-2,732.3, -1,938.6] | 0.0625 | -4.549 | -1.000 | 0/5 |
| FullHybrid_vs_M0 | server_p99_us | -1,501.5 | [-4,455.7, 18.820] | 0.25 | -0.457 | -0.600 | 1/4 |
| FullHybrid_vs_M0 | server_p999_us | -1,234.0 | [-5,087.5, 1,521.6] | 0.5625 | -0.293 | -0.600 | 1/4 |
| FullHybrid_vs_M0 | client_p99_rtt_us | -4,335.2 | [-6,090.2, -3,184.8] | 0.0625 | -2.222 | -1.000 | 0/5 |
| FullHybrid_vs_M0 | client_p999_rtt_us | -4,951.8 | [-7,324.2, -2,635.9] | 0.0625 | -1.776 | -1.000 | 0/5 |
| FullHybrid_vs_L1Only | deadline_violation_rate | -4.672 | [-6.684, -2.924] | 0.0625 | -1.856 | -1.000 | 0/5 |
| FullHybrid_vs_L1Only | slo_goodput_rps | -1,219.5 | [-1,742.3, -755.251] | 0.0625 | -1.834 | -1.000 | 0/5 |
| FullHybrid_vs_L1Only | server_p99_us | -1,509.6 | [-4,454.4, -12.924] | 0.125 | -0.459 | -0.600 | 1/4 |
| FullHybrid_vs_L1Only | server_p999_us | -1,752.9 | [-5,133.4, 23.063] | 0.375 | -0.467 | -0.200 | 2/3 |
| FullHybrid_vs_L1Only | client_p99_rtt_us | -1,367.7 | [-3,022.4, -266.147] | 0.125 | -0.751 | -0.600 | 1/4 |
| FullHybrid_vs_L1Only | client_p999_rtt_us | -1,240.9 | [-2,849.0, -290.093] | 0.0625 | -0.701 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | deadline_violation_rate | -7.360 | [-8.772, -6.144] | 0.0625 | -4.358 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | slo_goodput_rps | -1,917.3 | [-2,283.5, -1,582.8] | 0.0625 | -4.224 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | server_p99_us | -1,519.0 | [-4,458.5, -30.723] | 0.0625 | -0.463 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | server_p999_us | -1,706.0 | [-5,053.7, -19.971] | 0.0625 | -0.456 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | client_p99_rtt_us | -4,353.7 | [-6,090.0, -3,247.3] | 0.0625 | -2.252 | -1.000 | 0/5 |
| FullHybrid_vs_CentralOnly | client_p999_rtt_us | -5,606.0 | [-7,259.2, -4,611.3] | 0.0625 | -3.020 | -1.000 | 0/5 |
| CentralOnly_vs_FIFO | deadline_violation_rate | -1.120 | [-1.772, -0.332] | 0.125 | -1.168 | -0.600 | 1/4 |
| CentralOnly_vs_FIFO | slo_goodput_rps | -290.820 | [-462.820, -64.732] | 0.125 | -1.161 | -0.600 | 1/4 |
| CentralOnly_vs_FIFO | server_p99_us | 6.670 | [-4.132, 17.867] | 0.3125 | 0.510 | 0.600 | 4/1 |
| CentralOnly_vs_FIFO | server_p999_us | 22.118 | [-6.581, 44.676] | 0.25 | 0.684 | 0.600 | 4/1 |
| CentralOnly_vs_FIFO | client_p99_rtt_us | 3.064 | [-5.051, 11.737] | 0.625 | 0.284 | 0.200 | 3/2 |
| CentralOnly_vs_FIFO | client_p999_rtt_us | 11.433 | [-9.009, 26.875] | 0.3125 | 0.486 | 0.600 | 4/1 |

For FullHybrid versus FIFO, the exact p is 0.0625 for the 0/5 aligned miss, goodput, server P99, client P99, and client P999 regressions; server P999 is 1/4 with CI crossing zero. Bootstrap CIs exclude zero for miss, goodput, server P99, client P99, and client P999. The exact n=5 limitation still applies.

### 9.3 Per-request non-runtime residual

`non_runtime_residual = client RTT - server completion` is calculated per request before quantiles; it is **not** quantile subtraction and not a direct mutex-wait measurement.

| variant | residual P99 us | residual P999 us | response queue HWM |
|---|---:|---:|---:|
| FIFO | 85.158 | 187.217 | 6 |
| M0 | 84.682 | 205.717 | 5 |
| L1Only | 3037.130 | 4753.915 | 6 |
| CentralOnly | 87.590 | 212.636 | 6 |
| FullHybrid | 3399.606 | 5253.147 | 7 |

Response queue high-water marks are only 4–15 across all runs, with zero response enqueue/send failures. The large L1Only/FullHybrid residual, low response-queue occupancy, and code path jointly support the following inference:

* distributed L1 idle workers poll every 100 us;
* the poll path holds the runtime global mutex while scanning (`src/physical/runtime.cpp`, approximately lines 911–950 in the final dirty tree);
* network submission also needs that mutex (`submit_network_request`, approximately line 1289), called by the RPC receiver (`src/app/rpc_server_main.cpp`, approximately line 546);
* therefore L1 polling likely delays ingress before successful submit. Server completion starts after submit and can hide this delay, while client RTT includes it.

This is an evidence-supported inference, not a direct measurement of mutex wait. Direct lock-wait instrumentation remains future work.

The retained RPC anomalies include seed 97 FullHybrid (server P99 about 7,592.4 us, client P99 about 8,002.1 us, scheduler max lag 10,924.219 us) and seed 71 M0 (scheduler max lag above 5,393 us and a P999 outlier). They remain in all means, CIs, and tests.

Files:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_path_breakdown.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_path_breakdown_by_class.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_variant_summary.csv`

## 10. Statistical interpretation

* Pairing unit: holdout seed; traces are hash-identical across compared variants.
* Bootstrap: 10,000 paired resamples of seed-level differences; percentile 95% CI.
* Paired significance: exact two-sided sign-flip test; exact sign test is also recorded.
* Effect sizes: Cohen's dz and paired rank-biserial for every aggregate row.
* At n=5, all-aligned exact two-sided p=0.0625. A bootstrap CI excluding zero is evidence of directional stability in these five pairs, but it is not equivalent to an exact alpha=0.05 rejection.
* No multiple-comparison-adjusted confirmatory family was predeclared. The many tail/class analyses should be treated as diagnostic/exploratory.
* Heterogeneous setups are not pooled into a universal effect.

Complete statistical table: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv`.

## 11. Failures retained and diagnosed

1. Synthetic attempt 1 hung at W3-rho070-w16 seed 127 M0 after 24 valid runs. Thread snapshots showed the scheduler thread spinning while workers waited. A terminal scheduler handshake/liveness fix was added; the exact configuration then passed 100/100 five-second hard-timeout Release regressions. All attempt-1 outputs are preserved and excluded from final metrics; the entire 160-run paired matrix was restarted cleanly.
2. RPC attempt 1 used a 5-us physical scheduler period. It missed 87.85% of epochs even though decision P50 was 0.712 us, showing host wake/scheduling granularity—not decision compute—dominated. The attempt was invalid under its timing gate and is preserved. Final RPC uses 100 us without changing the M1 tuple.
3. Analyzer attempt 1 incorrectly treated the predeclared 4-us client send-lag performance gate as a correctness failure. All 25 RPC runs had complete delivery and correct invariants. The analyzer was corrected to retain this as a warning, then all 185 final runs were reanalyzed.
4. Initial targeted TSan runs reported a GCC 11/glibc `pthread_cond_clockwait` double-lock false positive. The steady-clock accounting was preserved while blocking waits were translated through the TSan-visible realtime condition-variable path. Final targeted TSan is 2/2 PASS.

Detailed diagnosis: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FAILURE_DIAGNOSIS.md`.

## 12. Final tests and validation

* Final Release build + complete CTest: 32/32 PASS.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-release-build-and-full-ctest.log`
* Final targeted TSan: 2/2 PASS.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/final-tsan-build-and-targeted-ctest.log`
* Independent synthetic validation: PASS.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/synthetic-attempt2-independent-validation.log`
* Independent RPC validation: PASS_WITH_SEND_TIMING_WARNINGS.  
  `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validation-logs/rpc-attempt2-independent-validation.log`
* Final artifact validator: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validate_final_artifact.py`

## 13. Remaining risks and claims explicitly not made

* Only five holdout seeds per setup; exact alpha=0.05 significance is unattainable for a 5/5 sign pattern.
* One local host and one loopback RPC topology; no two-host/network-contention/CloudLab evidence.
* RPC is low-load `arrival_scale=10` implementation evidence and carries send-timing warnings in all 25 runs.
* Synthetic CPU-service execution and time scaling do not reproduce every OS/network deployment effect.
* Scheduler CPU time and mutex wait are not directly instrumented.
* P999 estimates use 5,000 measured requests/run and are therefore noisy; several comparisons cross zero or regress.
* Rescued/migrated subgroup contrasts are selection-biased.
* Migration counts, repeated migrations, and control overhead are high; no low-overhead claim is supported.
* FullHybrid is not universally superior to L1Only, especially for W3-rho070 outcomes and several P99/P999/class tails.
* The data support holdout generalization only over the tested W2/W3 traces, loads, and worker counts, not universal generality.

## 14. Reproducibility index

* Run plans: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/synthetic_run_plan.json`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_run_plan.json`
* Commands: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/commands.jsonl`
* Raw/paired/aggregate: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/raw_metrics.csv`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/paired_metrics.csv`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/aggregate.csv`
* Analysis scripts: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/analyze_results.py`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/build_audit_outputs.py`
* Analysis manifests: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/analysis_manifest.json`, `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/derived_analysis_manifest.json`
* Validation summary: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/VALIDATION_SUMMARY.md`
* Failure diagnosis: `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FAILURE_DIAGNOSIS.md`
