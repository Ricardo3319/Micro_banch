"""
Emit DQB-v2 experiment planning artifacts.

This script does not run simulations. It materializes the planned experiment
matrix, ablations, metrics schema, figure plan, and acceptance criteria so the
next implementation phase can wire runner modes and result processing against a
stable contract.

Usage:
    python scripts/emit_dqb_v2_experiment_plan.py
"""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "step-09-dqb-v2-plan"

SEEDS = "11;23;37;47;59"
METHODS = "B1_PowerOf2;B2_Reactive;M0_Proactive;M1_AQB_PM;M2_DQB_PM;DQB_v2"


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def experiment_matrix() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    scenario_specs = [
        (
            "w1_saturation_boundary",
            "W1_POISSON_BIMODAL",
            "HOMOGENEOUS",
            [0.50, 0.70, 0.85, 0.92, 0.95],
            "saturation guard and no-migrate boundary",
        ),
        (
            "w2_burst_pressure_batch",
            "W2_MMPP_BIMODAL",
            "HOMOGENEOUS",
            [0.50, 0.70, 0.85, 0.92],
            "burst hotspot and pressure-batch repair",
        ),
        (
            "w3_heavytail_blocking_batch",
            "W3_POISSON_LOGNORMAL",
            "HOMOGENEOUS",
            [0.50, 0.70, 0.85, 0.92],
            "mice-behind-elephant and sparse blocking boundary",
        ),
        (
            "hetero_w2_slow_node_pressure",
            "W2_MMPP_BIMODAL",
            "HETERO_25PCT",
            [0.70, 0.85, 0.92],
            "slow-node pressure and slow-to-fast repair",
        ),
        (
            "hetero_w3_slow_heavytail",
            "W3_POISSON_LOGNORMAL",
            "HETERO_25PCT",
            [0.70, 0.85, 0.92],
            "slow-node heavy-tail blocking boundary",
        ),
    ]
    for scenario_id, workload, cluster, rhos, purpose in scenario_specs:
        for rho in rhos:
            rows.append(
                {
                    "scenario_id": scenario_id,
                    "workload": workload,
                    "cluster": cluster,
                    "rho": f"{rho:.2f}",
                    "methods": METHODS,
                    "seeds": SEEDS,
                    "purpose": purpose,
                    "status": "planned",
                }
            )
    return rows


def ablations() -> list[dict[str, object]]:
    groups = {
        "distribution": [
            ("DQB-v2/full", "complete prior + local-summary calibrated policy"),
            ("DQB-v2/prior-only", "remove local queue-summary correction"),
            ("DQB-v2/summary-only", "remove workload prior calibration"),
            ("DQB-v2/queue-length-only", "replace distribution estimate with queue length"),
            ("DQB-v2/no-confidence", "disable confidence-weighted migration margin"),
            ("DQB-v2/no-pattern-type", "disable pressure/blocking/slow-node typing"),
        ],
        "batch_migration": [
            ("DQB-v2/single-move", "force one moved request per selected batch"),
            ("DQB-v2/batch-size=2", "cap moved requests per batch at 2"),
            ("DQB-v2/batch-size=4", "cap moved requests per batch at 4"),
            ("DQB-v2/batch-size=8", "cap moved requests per batch at 8"),
            ("DQB-v2/batch-size=16", "cap moved requests per batch at 16"),
            ("DQB-v2/batch-size=32", "cap moved requests per batch at 32"),
            ("DQB-v2/batch-size=64", "cap moved requests per batch at 64"),
            ("DQB-v2/no-host-aggregation", "disable W3 host-level fragment aggregation"),
            ("DQB-v2/no-arrival-epoch-binning", "disable arrival-epoch batch formation"),
            ("DQB-v2/pressure-batch-only", "disable blocking-oriented batches"),
            ("DQB-v2/blocking-batch-only", "disable pressure-oriented batches"),
        ],
        "target_protection": [
            ("DQB-v2/no-reservation", "ignore scheduled incoming migration work"),
            ("DQB-v2/host-reservation-only", "reserve at host granularity only"),
            ("DQB-v2/core-reservation", "reserve at virtual-core granularity"),
            ("DQB-v2/no-target-harm-guard", "disable target-side harm rejection"),
            ("DQB-v2/no-saturation-guard", "disable global saturation no-migrate guard"),
        ],
        "realism": [
            ("Oracle-E", "use exact service estimate as an upper-bound signal"),
            ("EWMA-E", "use online service estimate as the main realistic setting"),
            ("Noisy-E", "inject service-estimate error"),
            ("eligible_fraction=1.0", "all waiting requests are migratable"),
            ("eligible_fraction=0.75", "75 percent of waiting requests are migratable"),
            ("eligible_fraction=0.50", "50 percent of waiting requests are migratable"),
            ("sync_period_us=5", "stale-view refresh every 5 us"),
            ("sync_period_us=10", "stale-view refresh every 10 us"),
            ("sync_period_us=20", "stale-view refresh every 20 us"),
            ("sync_period_us=50", "stale-view refresh every 50 us"),
            ("check_period_us=0.5", "migration check every 0.5 us"),
            ("check_period_us=1", "migration check every 1 us"),
            ("check_period_us=2", "migration check every 2 us"),
            ("check_period_us=5", "migration check every 5 us"),
        ],
    }
    rows: list[dict[str, object]] = []
    for group, variants in groups.items():
        for variant, purpose in variants:
            rows.append(
                {
                    "ablation_group": group,
                    "variant": variant,
                    "purpose": purpose,
                    "primary_scenarios": "w2_burst_pressure_batch;w3_heavytail_blocking_batch;w1_saturation_boundary",
                    "status": "planned",
                }
            )
    return rows


def metrics_schema() -> list[dict[str, object]]:
    metrics = [
        ("primary", "P99_us", "us", "99th percentile end-to-end latency"),
        ("primary", "P999_us", "us", "99.9th percentile end-to-end latency"),
        ("primary", "slo_violation_rate", "ratio", "all-request SLO violation rate"),
        ("primary", "short_slo_violation_rate", "ratio", "short-request SLO violation rate"),
        ("primary", "long_slo_violation_rate", "ratio", "long-request SLO violation rate"),
        ("primary", "mice_slo_violation_rate", "ratio", "mice-request SLO violation rate"),
        ("primary", "elephant_slo_violation_rate", "ratio", "elephant-request SLO violation rate"),
        ("migration", "migration_rate", "ratio", "moved request count / generated request count"),
        ("migration", "migration_work_rate", "ratio", "moved estimated work / generated estimated work"),
        ("migration", "invalid_migration_ratio", "ratio", "migrations whose actual latency exceeded local estimate"),
        ("migration", "target_harm_est_us", "us", "estimated target-side harm from selected batch"),
        ("migration", "dst_queue_harm_after_arrival", "us", "observed target queue impact after migrated arrivals"),
        ("migration", "per_dst_migrated_work_us", "us", "incoming migrated work by destination host"),
        ("batch", "batch_candidate_count", "count", "batch candidates generated"),
        ("batch", "batch_selected_count", "count", "batch candidates committed"),
        ("batch", "batch_move_count", "count", "moved requests in committed batches"),
        ("batch", "batch_size_exact_histogram", "histogram", "exact moved-request count per committed batch"),
        ("batch", "batch_type", "enum", "pressure/blocking/slow-node/distribution batch type"),
        ("batch", "batch_confidence", "ratio", "batch distribution estimate confidence"),
        ("batch", "risk_mass_est", "us", "estimated source-side batch risk mass"),
        ("batch", "risk_reduction_est", "us", "estimated risk reduction after migration"),
        ("batch", "source_queue_depth", "count", "source queue depth at decision"),
        ("batch", "source_queue_work_us", "us", "source queue work at decision"),
        ("batch", "destination_virtual_occupancy", "us", "target virtual-core occupancy estimate"),
        ("no_migrate", "NO_BATCH_FORMED", "count", "no legal batch could be formed"),
        ("no_migrate", "LOW_CONFIDENCE", "count", "distribution estimate confidence below threshold"),
        ("no_migrate", "LOW_EXPECTED_GAIN", "count", "expected gain below migration margin"),
        ("no_migrate", "DST_RESERVATION_HIGH", "count", "target incoming reservation too high"),
        ("no_migrate", "DST_TAIL_HARM", "count", "target-side harm guard rejected batch"),
        ("no_migrate", "SATURATION_GUARD", "count", "global saturation guard rejected migration"),
        ("no_migrate", "BUDGET_EXHAUSTED", "count", "migration budget exhausted"),
        ("no_migrate", "SPARSE_BLOCKING_NOT_BATCHABLE", "count", "blocking risk too sparse for legal batch"),
        ("overhead", "summary_update_cost_est_us", "us", "estimated cost to update queue summary"),
        ("overhead", "batch_estimation_cost_est_us", "us", "estimated cost to build and score batch descriptor"),
        ("overhead", "target_selection_cost_est_us", "us", "estimated target sampling and rejection cost"),
        ("overhead", "candidates_per_check", "count", "candidate count per migration check"),
        ("overhead", "summaries_per_check", "count", "summary count per migration check"),
        ("overhead", "control_messages_per_migration", "count", "control-plane message count per committed migration"),
        ("overhead", "decision_cost_over_check_period", "ratio", "decision cost divided by check period"),
    ]
    return [
        {
            "metric_group": group,
            "metric_name": name,
            "unit": unit,
            "description": desc,
            "status": "planned",
        }
        for group, name, unit, desc in metrics
    ]


def figure_plan() -> list[dict[str, object]]:
    rows = [
        (
            "fig1_main_gain",
            "W2 burst P99/P999 across B1/B2/M0/M1/M2-v1/DQB-v2 plus migration rate",
            "w2_burst_pressure_batch",
            "effectiveness without relying on higher migration rate",
        ),
        (
            "fig2_mechanism_ablation",
            "full vs prior-only vs summary-only vs queue-length-only vs no-reservation vs no-saturation-guard",
            "w2_burst_pressure_batch;w1_saturation_boundary",
            "distribution, batch-risk, target protection, and saturation mechanisms",
        ),
        (
            "fig3_batch_behavior",
            "exact batch-size histogram, batch type distribution, risk mass vs selected probability",
            "w2_burst_pressure_batch;w3_heavytail_blocking_batch",
            "DQB-v2 is truly batch-level and not a size-1 fallback",
        ),
        (
            "fig4_w3_boundary",
            "W3 no-migrate reason distribution and sparse blocking diagnostics",
            "w3_heavytail_blocking_batch",
            "heavy-tail boundary or hybrid blocking-batch benefit",
        ),
        (
            "fig5_heterogeneous",
            "fast/slow source-destination migration matrix and target congestion",
            "hetero_w2_slow_node_pressure;hetero_w3_slow_heavytail",
            "slow-node pressure and receiver-side protection",
        ),
        (
            "fig6_control_overhead",
            "decision cost, candidate count, summary count versus load and queue depth",
            "all",
            "bounded control-plane cost",
        ),
        (
            "fig7_cloudlab_replay",
            "key results replayed with calibrated CloudLab constants",
            "w2_burst_pressure_batch;w1_saturation_boundary;w3_heavytail_blocking_batch",
            "realism of simulator constants and trend preservation",
        ),
    ]
    return [
        {
            "figure_id": fid,
            "content": content,
            "scenarios": scenarios,
            "evidence_goal": goal,
            "status": "planned",
        }
        for fid, content, scenarios, goal in rows
    ]


def write_readme() -> None:
    text = """# Step-09 DQB-v2 Experiment Plan

This directory is generated by `scripts/emit_dqb_v2_experiment_plan.py`.

It contains planning artifacts only. No simulator run is started by the script.

Files:

- `experiment_matrix.csv`: scenario, workload, cluster, rho, method, and seed plan.
- `ablations.csv`: required DQB-v2 ablation variants.
- `metrics_schema.csv`: metrics that later runner/export code should produce.
- `figure_plan.csv`: planned evidence figures and their required scenarios.
- `acceptance_criteria.md`: pass/fail criteria for the DQB-v2 study.

The implemented codebase currently contains `M2_DQB_PM` as DQB-v1. `DQB-v2`
is the planned prior-calibrated distribution-batch design.
"""
    (OUT / "README.md").write_text(text, encoding="utf-8")


def write_acceptance() -> None:
    text = """# DQB-v2 Acceptance Criteria

- W2 burst: DQB-v2 improves at least one key P99/P999 load point by more than
  5% over both M1_AQB_PM and current M2_DQB_PM, with migration_rate <= 0.05.
- W1 saturation: DQB-v2 is not meaningfully worse than B2_Reactive and keeps
  migration low or disabled through the saturation guard.
- W3 heavy-tail: the result must be explainable. A gain supports hybrid
  blocking-batch repair; no gain must be backed by no-migrate reasons showing
  sparse blocking is a batch-migration boundary.
- Heterogeneous scenarios: DQB-v2 must not silently repeat M1's rho=0.85
  budget-misalignment failure. Reservation and slow-node diagnostics must
  identify the mechanism.
- Control overhead: estimated control cost should remain below 20% of the
  check period in the main configuration.
- CloudLab replay: method ranking should not invert, and the knee point should
  shift by no more than one rho bucket after calibrated constants are applied.
"""
    (OUT / "acceptance_criteria.md").write_text(text, encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    write_readme()
    write_csv(
        OUT / "experiment_matrix.csv",
        ["scenario_id", "workload", "cluster", "rho", "methods", "seeds", "purpose", "status"],
        experiment_matrix(),
    )
    write_csv(
        OUT / "ablations.csv",
        ["ablation_group", "variant", "purpose", "primary_scenarios", "status"],
        ablations(),
    )
    write_csv(
        OUT / "metrics_schema.csv",
        ["metric_group", "metric_name", "unit", "description", "status"],
        metrics_schema(),
    )
    write_csv(
        OUT / "figure_plan.csv",
        ["figure_id", "content", "scenarios", "evidence_goal", "status"],
        figure_plan(),
    )
    write_acceptance()
    print(f"Wrote DQB-v2 experiment plan artifacts to {OUT}")


if __name__ == "__main__":
    main()
