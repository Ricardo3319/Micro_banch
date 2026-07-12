# Corrected full evaluation manifest

- Code commit used for simulation: `3182c73`
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

```powershell
python scripts/corrected_eval_analysis.py --tier full `
  --out-dir artifacts/step-21-corrected-full --inputs `
  artifacts/step-21-corrected-full/w3.csv `
  artifacts/step-21-corrected-full/w2.csv `
  artifacts/step-21-corrected-full/w1.csv
```

## Pre-registered gate result

Status: **PASS**, narrowly interpreted.

- W3 rho 0.85: 15.1% relative miss reduction versus polling work stealing and
  15.3% versus ALTO-style migration. RescueSched moves 70.0% less work than
  polling work stealing and 3.0% less than ALTO-style migration.
- W3 rho 0.90: 17.2% relative miss reduction versus polling work stealing and
  16.7% versus ALTO-style migration. RescueSched moves 62.7% less work than
  polling work stealing and 10.5% less than ALTO-style migration.
- W3 rho 0.70: RescueSched is 125% worse in miss rate than polling work stealing
  and moves 1.1% more work than ALTO-style migration. This is a required
  low-load boundary.
- W2 rho 0.85: RescueSched reduces misses by 12.6%--15.4%, but its median P99 is
  6.6x--8.6x and P999 is 4.7x--8.8x the strong baselines. It cannot support a
  general tail-latency claim.

All paired comparison rows and confidence intervals are stored in
`paired_comparisons.csv`; unrounded medians are stored in `summary.csv`.
