# Corrected full evaluation manifest

- Source revision used for the Linux rerun: `ba81d825eaf1e0b6701e21dbb6462c2a801da0b9`
- Run date: `2026-07-15` (Asia/Shanghai)
- Environment: WSL2 Linux 6.6.87.2, CMake 3.22.1, GNU C++ 11.4.0
- Trace/schema: `rescuesched-trace-v2` / `rescuesched-v2`
- Cohorts per run: 200,000 warmup + 1,000,000 measurement requests
- Seeds: 11, 23, 37, 47, 59, 71, 83, 97, 109, 127
- Primary methods: L0, polling work stealing, ALTO-style threshold migration,
  and RescueSched
- W3 rho: 0.70, 0.85, 0.90
- W1/W2 rho: 0.70, 0.85
- Total runs: 280
- Analysis: deterministic 10,000-draw paired bootstrap, positive difference
  means fewer deadline misses under RescueSched

## Commands

The simulation commands use `--mode rescue-main`, the workload/rho/seed sets
above, `--warmup-requests 200000`, and `--measurement-requests 1000000`.
Analysis command:

```bash
/usr/bin/time -v bash scripts/run_corrected_eval.sh full build-linux-release
```

The full Linux run completed 280/280 simulations in 6 minutes 2.70 seconds and
exited with status 0. The source tree matched the revision above; generated
evidence and local support files made the worktree non-clean during the rerun.

## Evidence checksums

```text
132b18ff4551446f64c13c42e6dbda3e088b46297c68051ccc0f60bc13a2011a  w1.csv
023118a50fb725265546dcacbccb32601fccc77daab9d337c65ae23a7fbd844b  w2.csv
b0c1074de917fa2e385362478125a1376a234ec327407c5d4b726ff075859b9b  w3.csv
95d80ac0c5aa68aabe79958525ef5952c12a0b0d11594b040bb9f56329773057  summary.csv
aad62450f2bfa07ce75524657071ba9e370fcc8c176e66533b9347355541f528  paired_comparisons.csv
```

## Pre-registered gate result

Status: **PASS**, narrowly interpreted.

- W3 rho 0.85: 15.29% relative miss reduction versus polling work stealing and
  15.30% versus ALTO-style migration. RescueSched moves 69.98% less work than
  polling work stealing and 3.19% less than ALTO-style migration.
- W3 rho 0.90: 17.22% relative miss reduction versus polling work stealing and
  16.79% versus ALTO-style migration. RescueSched moves 62.68% less work than
  polling work stealing and 10.46% less than ALTO-style migration.
- W3 rho 0.70: RescueSched is 124.89% worse in miss rate than polling work
  stealing and moves 0.98% more work than ALTO-style migration. This is a
  required low-load boundary.
- W2 rho 0.85: RescueSched reduces misses by 12.6%--15.4%, but its median P99 is
  6.6x--8.6x and P999 is 4.7x--8.8x the strong baselines. It cannot support a
  general tail-latency claim.

All paired comparison rows and confidence intervals are stored in
`paired_comparisons.csv`; unrounded medians are stored in `summary.csv`.
