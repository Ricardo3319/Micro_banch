# Corrected development pilot manifest

- Code commit: `9568223`
- Trace/schema: `rescuesched-trace-v2` / `rescuesched-v2`
- Cohorts: 500 warmup + 5,000 measurement requests
- Seeds: 11, 23, 37, 47, 59
- Command: `scripts/run_corrected_eval.ps1 -Tier pilot`
- Primary methods analyzed: L0, polling work stealing, ALTO-style threshold,
  and RescueSched. No-target-safety rows are stored but excluded from the gate.

Directional W3 result: RescueSched is worse than polling work stealing at rho
0.70, while rho 0.85 and 0.90 are candidate passing points against both strong
baselines. Pilot results cannot pass the paper gate.
