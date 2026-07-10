# Artifact and Figure Provenance

This file binds each existing figure family to its input CSVs, generation
script, command, and run metadata that must be recorded when regenerating.

Current source commit recorded when this file was written: `4f8bed8`.
Worktree state at creation: dirty, because this change set is not yet
committed.

## Required Run Metadata

Every regenerated artifact directory should include a manifest with:

- `git rev-parse HEAD`
- `git status --short`
- exact build command
- exact simulator command
- exact analysis script command
- compiler and CMake versions
- hostname and OS/kernel version
- input CSV path, output figure path, and timestamp

Minimal build command:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Minimal test gate:

```bash
ctest --test-dir build --output-on-failure
```

## Simulator CSV Producers

| CSV | Producer command |
| --- | --- |
| `artifacts/step-15-rescuesched/rescue_main.csv` | `./build/simulator --mode rescue-main` |
| `artifacts/step-15-rescuesched/rescue_w3_only.csv` | `./build/simulator --mode rescue-w3-only` |
| `artifacts/step-15-rescuesched/rescue_ablation.csv` | `./build/simulator --mode rescue-ablation` |
| `artifacts/step-15-rescuesched/rescue_check_sweep.csv` | `./build/simulator --mode rescue-check-sweep` |
| `artifacts/step-15-rescuesched/rescue_overload_sanity.csv` | `./build/simulator --mode rescue-overload-sanity` |
| `artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv` | `./build/simulator --mode rescue-w2-burst` |
| `artifacts/step-17-rescuesched-closure/rescue_robustness_10seed.csv` | `./build/simulator --mode rescue-robustness-10seed` |
| `artifacts/step-17-rescuesched-closure/migration_cost_microbench.csv` | `./build/simulator --mode rescue-cost-microbench` |
| `artifacts/step-17-rescuesched-closure/rescue_calibration.csv` | `./build/simulator --mode rescue-calibration` |
| `artifacts/step-18-infocom-readiness/rescue_estimator_main.csv` | `./build/simulator --mode rescue-estimator-main` |
| `artifacts/step-18-infocom-readiness/rescue_estimator_w2.csv` | `./build/simulator --mode rescue-estimator-w2` |
| `artifacts/step-18-infocom-readiness/rescue_cost_calibration.csv` | `./build/simulator --mode rescue-cost-calibration` |
| `artifacts/step-18-infocom-readiness/rescue_w2_boundary.csv` | `./build/simulator --mode rescue-w2-boundary` |
| `artifacts/step-18-infocom-readiness/rescue_hybrid_main.csv` | `./build/simulator --mode rescue-hybrid-main` |
| `artifacts/step-18-infocom-readiness/rescue_target_safety_stress.csv` | `./build/simulator --mode rescue-target-safety-stress` |

Use `--out-dir DIR` to relocate these outputs under a different artifact root.
Use `--output FILE.csv` only for a single-output mode.

## RescueSched Figures

Generation command:

```bash
python scripts/rescue_analysis.py
```

| Figure outputs | Input CSVs | Script function / source |
| --- | --- | --- |
| `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_slo_vs_rho.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_main.csv`, `artifacts/step-15-rescuesched/rescue_w3_only.csv` | `scripts/rescue_analysis.py` |
| `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_ablation_quality.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_ablation.csv` | `scripts/rescue_analysis.py` |
| `artifacts/step-16-rescuesched-readiness/figures/fig_rescue_budget_sweep.{png,pdf}` | `artifacts/step-15-rescuesched/rescue_check_sweep.csv` | `scripts/rescue_analysis.py` |
| `artifacts/step-17-rescuesched-closure/figures/fig_w2_burst_slo_vs_rho.{png,pdf}` | `artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv` | `scripts/rescue_analysis.py` |
| `artifacts/step-17-rescuesched-closure/figures/fig_rescue_calibration.{png,pdf}` | `artifacts/step-17-rescuesched-closure/rescue_calibration.csv` | `scripts/rescue_analysis.py` |

Summary outputs from the same script:

- `artifacts/step-16-rescuesched-readiness/median_summary.csv`
- `artifacts/step-16-rescuesched-readiness/readiness_report.md`
- `artifacts/step-17-rescuesched-closure/closure_median_summary.csv`
- `artifacts/step-17-rescuesched-closure/ci_summary.csv`
- `artifacts/step-17-rescuesched-closure/summary.md`

## INFOCOM Readiness Tables

Generation command:

```bash
python scripts/infocom_readiness_analysis.py
```

Inputs:

- `artifacts/step-18-infocom-readiness/rescue_estimator_main.csv`
- `artifacts/step-18-infocom-readiness/rescue_estimator_w2.csv`
- `artifacts/step-18-infocom-readiness/rescue_cost_calibration.csv`
- `artifacts/step-18-infocom-readiness/rescue_w2_boundary.csv`
- `artifacts/step-18-infocom-readiness/rescue_hybrid_main.csv`
- `artifacts/step-18-infocom-readiness/rescue_target_safety_stress.csv`
- `artifacts/step-15-rescuesched/rescue_overload_sanity.csv`

Outputs:

- `artifacts/step-18-infocom-readiness/infocom_median_summary.csv`
- `artifacts/step-18-infocom-readiness/infocom_ci_summary.csv`
- `artifacts/step-18-infocom-readiness/summary.md`

The INFOCOM readiness script currently generates tables and summary text, not
figure files.

## Legacy Figures

Generation command:

```bash
python scripts/generate_charts.py
```

| Figure outputs | Input CSVs | Script function |
| --- | --- | --- |
| `docs/figures/fig1_w1_p99_full.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `fig_w1_p99_full` |
| `docs/figures/fig2_w1_p999_full.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `fig_w1_p999_full` |
| `docs/figures/fig3_w2_bar.{png,pdf}` | `artifacts/step-01-tier1/metrics_table.csv` | `fig_w2_bar` |
| `docs/figures/fig4_w3_ci.{png,pdf}` | `artifacts/step-03-tier3/metrics_table.csv` | `fig_w3_ci` |
| `docs/figures/fig5_cross_workload.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv`, `artifacts/step-01-tier1/metrics_table.csv`, `artifacts/step-03-tier3/metrics_table.csv` | `fig_cross_workload` |
| `docs/figures/fig6_migration_metrics.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv` | `fig_migration_metrics` |
| `docs/figures/fig7_sensitivity.{png,pdf}` | `artifacts/step-04b-sensitivity/sensitivity_scan.csv` | `fig_sensitivity` |
| `docs/figures/fig8_heterogeneous.{png,pdf}` | `artifacts/step-04c-heterogeneous/metrics_table.csv`, `artifacts/step-01-tier1/metrics_table.csv` | `fig_heterogeneous` |
| `docs/figures/fig9_negative_case.{png,pdf}` | `artifacts/step-02-tier2/metrics_scan.csv`, `artifacts/step-03-tier3/metrics_table.csv`, `artifacts/step-01-tier1/metrics_table.csv` | `fig_negative_case` |

## Validation Commands

CSV schema gate:

```bash
python tests/integration/validate_rescue_csv_schema.py \
  artifacts/step-15-rescuesched/rescue_main.csv \
  artifacts/step-18-infocom-readiness/rescue_estimator_main.csv
```

CTest gate:

```bash
ctest --test-dir build --output-on-failure
```

## Known Gaps

- Existing artifact directories do not yet include per-run manifests.
- Some old RescueSched CSVs predate the current extended header; the schema
  validator therefore enforces a stable minimum column set and reports optional
  current columns when absent.
- Physical-machine figures are not defined yet because physical trace replay
  and physical result schema are not implemented.
