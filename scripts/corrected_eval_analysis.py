#!/usr/bin/env python3
"""Analyze corrected RescueSched evaluation CSVs with paired bootstrap CIs."""

import argparse
import csv
import pathlib
import random
import statistics


PRIMARY_METHODS = [
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
]
BASELINES = ["L1_WorkStealingPolling", "M0_AltoThreshold"]


def percentile(values: list[float], probability: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = int(round((len(ordered) - 1) * probability))
    return ordered[index]


def paired_bootstrap(values: list[float], draws: int = 10000) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    rng = random.Random(20260712)
    means = []
    for _ in range(draws):
        sample = [values[rng.randrange(len(values))] for _ in values]
        means.append(statistics.mean(sample))
    return percentile(means, 0.025), percentile(means, 0.975)


def read_rows(paths: list[pathlib.Path]) -> list[dict[str, str]]:
    rows = []
    for path in paths:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            current = list(csv.DictReader(handle))
        if not current:
            raise ValueError(f"{path} has no rows")
        if {row.get("schema_version") for row in current} != {"rescuesched-v2"}:
            raise ValueError(f"{path} is not strict rescuesched-v2")
        rows.extend(current)
    return rows


def number(row: dict[str, str], key: str) -> float:
    if key not in row or row[key] == "":
        raise KeyError(f"missing required field {key}")
    return float(row[key])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inputs", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--tier", choices=["pilot", "holdout", "full"], default="pilot")
    args = parser.parse_args()

    rows = [row for row in read_rows(args.inputs) if row["method"] in PRIMARY_METHODS]
    args.out_dir.mkdir(parents=True, exist_ok=True)

    groups: dict[tuple[str, float, str], list[dict[str, str]]] = {}
    by_seed: dict[tuple[str, float, str, int], dict[str, str]] = {}
    for row in rows:
        key = (row["workload"], number(row, "rho"), row["method"])
        groups.setdefault(key, []).append(row)
        by_seed[(key[0], key[1], key[2], int(number(row, "seed")))] = row

    summary_fields = [
        "workload", "rho", "method", "seeds", "slo_violation_median",
        "slo_goodput_per_us_median", "P99_us_median", "P999_us_median",
        "migrated_work_rate_median", "handoffs_per_request_median",
        "scheduler_attempts_per_request_median",
    ]
    summary_rows = []
    for (workload, rho, method), current in sorted(groups.items()):
        goodput = [
            number(row, "total_finished")
            * (1.0 - number(row, "slo_violation_rate"))
            / number(row, "experiment_duration_us")
            for row in current
        ]
        work_rate = [
            number(row, "intra_moved_work_us")
            / number(row, "measured_generated_work_us")
            for row in current
        ]
        attempts = [
            (number(row, "steal_attempt_count")
             + number(row, "proactive_intra_attempt_count")
             + number(row, "rescue_attempt_count"))
            / number(row, "measurement_requests")
            for row in current
        ]
        summary_rows.append({
            "workload": workload,
            "rho": rho,
            "method": method,
            "seeds": len(current),
            "slo_violation_median": statistics.median(
                number(row, "slo_violation_rate") for row in current),
            "slo_goodput_per_us_median": statistics.median(goodput),
            "P99_us_median": statistics.median(number(row, "P99_us") for row in current),
            "P999_us_median": statistics.median(number(row, "P999_us") for row in current),
            "migrated_work_rate_median": statistics.median(work_rate),
            "handoffs_per_request_median": statistics.median(
                number(row, "descriptor_handoff_count")
                / number(row, "measurement_requests") for row in current),
            "scheduler_attempts_per_request_median": statistics.median(attempts),
        })

    with (args.out_dir / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=summary_fields)
        writer.writeheader()
        writer.writerows(summary_rows)

    comparison_fields = [
        "workload", "rho", "baseline", "paired_seeds",
        "mean_slo_reduction", "ci95_low", "ci95_high",
        "rescue_work_rate_median", "baseline_work_rate_median",
        "significant_rescue_win", "no_more_migrated_work",
    ]
    comparisons = []
    points = sorted({(row["workload"], number(row, "rho")) for row in rows})
    for workload, rho in points:
        for baseline in BASELINES:
            seeds = sorted({
                int(number(row, "seed")) for row in rows
                if row["workload"] == workload and number(row, "rho") == rho
            })
            paired = []
            rescue_work = []
            baseline_work = []
            for seed in seeds:
                rescue = by_seed.get((workload, rho, "M1_RescueSched", seed))
                other = by_seed.get((workload, rho, baseline, seed))
                if not rescue or not other:
                    continue
                paired.append(number(other, "slo_violation_rate")
                              - number(rescue, "slo_violation_rate"))
                rescue_work.append(number(rescue, "intra_moved_work_us")
                                   / number(rescue, "measured_generated_work_us"))
                baseline_work.append(number(other, "intra_moved_work_us")
                                     / number(other, "measured_generated_work_us"))
            low, high = paired_bootstrap(paired)
            rescue_work_median = statistics.median(rescue_work) if rescue_work else 0.0
            baseline_work_median = statistics.median(baseline_work) if baseline_work else 0.0
            comparisons.append({
                "workload": workload,
                "rho": rho,
                "baseline": baseline,
                "paired_seeds": len(paired),
                "mean_slo_reduction": statistics.mean(paired) if paired else 0.0,
                "ci95_low": low,
                "ci95_high": high,
                "rescue_work_rate_median": rescue_work_median,
                "baseline_work_rate_median": baseline_work_median,
                "significant_rescue_win": int(low > 0.0),
                "no_more_migrated_work": int(rescue_work_median <= baseline_work_median + 1e-12),
            })

    with (args.out_dir / "paired_comparisons.csv").open(
            "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=comparison_fields)
        writer.writeheader()
        writer.writerows(comparisons)

    w3_by_rho: dict[float, list[dict[str, object]]] = {}
    for row in comparisons:
        if row["workload"] == "W3" and float(row["rho"]) <= 0.90:
            w3_by_rho.setdefault(float(row["rho"]), []).append(row)
    passing_points = [rho for rho, current in w3_by_rho.items()
                      if len(current) == len(BASELINES)
                      and all(int(row["significant_rescue_win"])
                              and int(row["no_more_migrated_work"])
                              for row in current)]

    status = "HOLDOUT_PILOT_ONLY" if args.tier == "holdout" else "PILOT_ONLY"
    if args.tier == "full":
        status = "PASS" if passing_points else "FAIL"
    with (args.out_dir / "go_no_go.md").open("w", encoding="utf-8") as handle:
        handle.write("# RescueSched corrected evaluation gate\n\n")
        handle.write(f"- Tier: `{args.tier}`\n")
        handle.write(f"- Status: **{status}**\n")
        handle.write(f"- W3 candidate passing rho points: `{passing_points}`\n")
        handle.write("- Positive `mean_slo_reduction` means RescueSched has fewer misses.\n")
        if args.tier in {"pilot", "holdout"}:
            handle.write("- Pilot results are directional and cannot pass the paper gate.\n")
        elif not passing_points:
            handle.write("- The pre-registered main claim fails under the corrected full matrix.\n")

    print(f"analyzed {len(rows)} rows -> {args.out_dir}; gate={status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
