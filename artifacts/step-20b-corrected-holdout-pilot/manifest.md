# Corrected holdout pilot manifest

- Code commit: `9568223`
- Trace/schema: `rescuesched-trace-v2` / `rescuesched-v2`
- Cohorts: 500 warmup + 5,000 measurement requests
- Holdout seeds: 71, 83, 97, 109, 127
- Command: `scripts/run_corrected_eval.ps1 -Tier holdout`
- Primary methods analyzed: L0, polling work stealing, ALTO-style threshold,
  and RescueSched. No-target-safety rows are stored but excluded from the gate.

Directional W3 result: rho 0.70 remains a clear loss to polling work stealing;
rho 0.85 beats ALTO-style migration but its interval versus work stealing
includes zero; rho 0.90 is the only candidate point passing both comparisons.
Pilot results cannot pass the paper gate.
