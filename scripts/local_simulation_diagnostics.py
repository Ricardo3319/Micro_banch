#!/usr/bin/env python3
"""Validate and summarize post-freeze RescueSched local diagnostics."""

import argparse
import csv
import math
import pathlib
import platform
import random
import statistics
from collections import defaultdict


PRIMARY_METHODS = [
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
]
BASELINES = ["L1_WorkStealingPolling", "M0_AltoThreshold"]
CONTROL_COUNT_FIELDS = [
    "control_check_count",
    "control_queue_entry_count",
    "control_candidate_count",
    "control_target_count",
    "control_estimator_update_count",
    "control_poll_operation_count",
]
CONTROL_COST_FIELDS = [
    "control_check_cost_us",
    "control_queue_entry_cost_us",
    "control_candidate_cost_us",
    "control_target_cost_us",
    "control_estimator_update_cost_us",
    "control_poll_cost_us",
]
TEXT_FIELDS = {
    "schema_version",
    "trace_version",
    "trace_sha256",
    "rpc_method_model",
    "deadline_model",
    "offered_load_definition",
    "scenario",
    "workload",
    "method",
    "service_estimate_mode",
    "placement_mode",
    "target_insert_policy",
    "control_cost_mode",
}
REQUIRED_DIAGNOSTICS = [
    "placement_mode",
    "flow_count",
    "flow_zipf_alpha",
    "flow_hash_seed",
    "scan_depth",
    "rescue_queue_entries_inspected_count",
    "rescue_accepted_candidate_count",
    "rescue_target_evaluation_count",
    "rescue_source_revalidation_reject_count",
    "rescue_remote_revalidation_reject_count",
    "rescue_short_move_count",
    "rescue_long_move_count",
    "rescue_burst_move_count",
    "rescue_nonburst_move_count",
    "rescue_migrated_finished",
    "rescue_nonmigrated_finished",
    "estimator_observation_count",
    "estimator_underestimate_count",
    "estimator_overestimate_count",
    "estimator_exact_count",
    "estimator_cold_start_count",
    "estimator_short_observation_count",
    "estimator_long_observation_count",
    "estimator_mae_us",
    "estimator_rmse_us",
    "control_cost_mode",
    "configured_control_cost_sum_us",
] + CONTROL_COUNT_FIELDS + CONTROL_COST_FIELDS

DEFAULT_PARAMETERS: dict[str, str | float] = {
    "placement_mode": "request_random",
    "flow_count": 4096.0,
    "flow_zipf_alpha": 0.0,
    "flow_hash_seed": 1381192497.0,
    "check_period_us": 1.0,
    "scan_depth": 64.0,
    "k_candidates": 16.0,
    "h_targets": 4.0,
    "budget_per_check": 1.0,
    "epsilon_us": 2.0,
    "migration_cost_us": 0.5,
    "service_estimate_ewma_alpha": 0.05,
    "control_check_cost_us": 0.0,
    "control_queue_entry_cost_us": 0.0,
    "control_candidate_cost_us": 0.0,
    "control_target_cost_us": 0.0,
    "control_estimator_update_cost_us": 0.0,
    "control_poll_cost_us": 0.0,
}

PROFILE_SPECS: dict[str, tuple[str, str, dict[str, str | float]]] = {
    "baseline": ("baseline", "corrected-default", {}),
    "placement-flow-uniform": (
        "placement", "flow-affine-uniform",
        {"placement_mode": "flow_affine", "flow_zipf_alpha": 0.0}),
    "placement-flow-zipf": (
        "placement", "flow-affine-zipf-1.1",
        {"placement_mode": "flow_affine", "flow_zipf_alpha": 1.1}),
    "handoff-0": ("handoff-us", "0", {"migration_cost_us": 0.0}),
    "handoff-2": ("handoff-us", "2", {"migration_cost_us": 2.0}),
    "handoff-5": ("handoff-us", "5", {"migration_cost_us": 5.0}),
    "check-2": ("check-period-us", "2", {"check_period_us": 2.0}),
    "check-5": ("check-period-us", "5", {"check_period_us": 5.0}),
    "scan-32": ("scan-depth", "32", {"scan_depth": 32.0}),
    "scan-128": ("scan-depth", "128", {"scan_depth": 128.0}),
    "candidates-8": ("k-candidates", "8", {"k_candidates": 8.0}),
    "candidates-32": ("k-candidates", "32", {"k_candidates": 32.0}),
    "targets-2": ("h-targets", "2", {"h_targets": 2.0}),
    "targets-8": ("h-targets", "8", {"h_targets": 8.0}),
    "budget-2": ("budget-per-check", "2", {"budget_per_check": 2.0}),
    "budget-4": ("budget-per-check", "4", {"budget_per_check": 4.0}),
    "epsilon-0": ("epsilon-us", "0", {"epsilon_us": 0.0}),
    "epsilon-5": ("epsilon-us", "5", {"epsilon_us": 5.0}),
    "ewma-0.01": (
        "ewma-alpha", "0.01", {"service_estimate_ewma_alpha": 0.01}),
    "ewma-0.20": (
        "ewma-alpha", "0.20", {"service_estimate_ewma_alpha": 0.20}),
    "control-accounting-unit": (
        "control-cost", "normalized-1us-per-operation",
        {field: 1.0 for field in CONTROL_COST_FIELDS}),
}

TIER_COHORTS = {
    "smoke": (20, 200, {11}),
    "pilot": (500, 5000, {11, 23, 37, 47, 59}),
    "full": (200000, 1000000, {11, 23, 37, 47, 59}),
}
ANCHOR_POINTS = {("W3", 0.70), ("W3", 0.85), ("W3", 0.90), ("W2", 0.85)}


def number(row: dict[str, str], field: str) -> float:
    try:
        value = float(row[field])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid or missing {field}: {exc}") from exc
    if not math.isfinite(value):
        raise ValueError(f"non-finite {field}={value}")
    return value


def integer(row: dict[str, str], field: str) -> int:
    value = number(row, field)
    result = int(value)
    if value != result:
        raise ValueError(f"non-integral {field}={value}")
    return result


def close(actual: float, expected: float) -> bool:
    return math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-9)


def divide(numerator: float, denominator: float, context: str) -> float:
    if not math.isfinite(numerator) or not math.isfinite(denominator):
        raise ValueError(f"{context} uses a non-finite operand")
    if denominator <= 0.0:
        raise ValueError(f"{context} has non-positive denominator={denominator}")
    result = numerator / denominator
    if not math.isfinite(result):
        raise ValueError(f"{context} produced a non-finite ratio")
    return result


def optional_ratio(numerator: float, denominator: float, context: str) -> float | str:
    if denominator == 0.0:
        return ""
    return divide(numerator, denominator, context)


def percentile(values: list[float], probability: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = int(round((len(ordered) - 1) * probability))
    return ordered[index]


def paired_bootstrap(values: list[float], draws: int = 10000) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    rng = random.Random(20260715)
    means = []
    for _ in range(draws):
        sample = [values[rng.randrange(len(values))] for _ in values]
        means.append(statistics.mean(sample))
    return percentile(means, 0.025), percentile(means, 0.975)


def median(rows: list[dict[str, str]], field: str) -> float:
    return statistics.median(number(row, field) for row in rows)


def profile_from_path(path: pathlib.Path) -> str:
    marker = "__"
    if marker not in path.stem:
        raise ValueError(f"raw filename must be PROFILE__WORKLOAD.csv: {path}")
    return path.stem.split(marker, 1)[0]


def read_profiles(path: pathlib.Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    required = {"profile", "dimension", "value", "notes", "simulator_arguments"}
    if not rows:
        raise ValueError("profiles.csv contains no profiles")
    if not required.issubset(rows[0]):
        raise ValueError(f"profiles.csv lacks columns: {sorted(required - set(rows[0]))}")
    profiles = {row["profile"]: row for row in rows}
    if len(profiles) != len(rows):
        raise ValueError("profiles.csv contains duplicate profile names")
    if set(profiles) != set(PROFILE_SPECS):
        raise ValueError(
            f"profile set mismatch: expected={set(PROFILE_SPECS)} actual={set(profiles)}")
    for name, (dimension, value, _) in PROFILE_SPECS.items():
        row = profiles[name]
        if row["dimension"] != dimension or row["value"] != value:
            raise ValueError(
                f"profile {name} metadata mismatch: "
                f"expected=({dimension},{value}) actual=({row['dimension']},{row['value']})")
    return profiles


def read_and_validate(paths: list[pathlib.Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    result_keys: set[tuple[str, str, str, int, str]] = set()
    shared_trace: dict[tuple[str, str, int, str, int, str, int], str] = {}

    for path in paths:
        profile = profile_from_path(path)
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if not reader.fieldnames:
                raise ValueError(f"{path} has no header")
            missing = [field for field in REQUIRED_DIAGNOSTICS
                       if field not in reader.fieldnames]
            if missing:
                raise ValueError(f"{path} lacks diagnostics: {', '.join(missing)}")
            for line, row in enumerate(reader, start=2):
                context = f"{path}:{line}"
                if None in row or any(value is None for value in row.values()):
                    raise ValueError(f"{context} row width does not match header")
                for field in reader.fieldnames:
                    if field not in TEXT_FIELDS:
                        try:
                            number(row, field)
                        except ValueError as exc:
                            raise ValueError(f"{context} {exc}") from exc
                if row.get("schema_version") != "rescuesched-v2":
                    raise ValueError(f"{context} is not rescuesched-v2")
                if row.get("method") not in PRIMARY_METHODS:
                    raise ValueError(f"{context} unexpected method={row.get('method')}")
                if row.get("control_cost_mode") != "accounting_only":
                    raise ValueError(f"{context} control cost is not accounting-only")
                measurement = integer(row, "measurement_requests")
                if integer(row, "total_finished") != measurement:
                    raise ValueError(f"{context} did not drain the measurement cohort")
                if integer(row, "latency_sample_count") != measurement:
                    raise ValueError(f"{context} latency cohort mismatch")
                if integer(row, "estimator_observation_count") != measurement:
                    raise ValueError(f"{context} estimator cohort mismatch")
                estimator_directions = sum(integer(row, field) for field in (
                    "estimator_underestimate_count", "estimator_overestimate_count",
                    "estimator_exact_count"))
                if estimator_directions != measurement:
                    raise ValueError(f"{context} estimator direction counts mismatch")
                estimator_classes = integer(row, "estimator_short_observation_count") \
                    + integer(row, "estimator_long_observation_count")
                if estimator_classes != measurement:
                    raise ValueError(f"{context} estimator class counts mismatch")
                if integer(row, "estimator_cold_start_count") > measurement:
                    raise ValueError(f"{context} estimator cold starts exceed observations")
                estimator_mae = number(row, "estimator_mae_us")
                estimator_rmse = number(row, "estimator_rmse_us")
                if estimator_mae < 0.0 or estimator_rmse < 0.0:
                    raise ValueError(f"{context} negative estimator error")
                if estimator_mae > estimator_rmse + max(1e-6, estimator_rmse * 1e-5):
                    raise ValueError(f"{context} estimator MAE exceeds RMSE")
                if integer(row, "rescue_migrated_finished") \
                        + integer(row, "rescue_nonmigrated_finished") != measurement:
                    raise ValueError(f"{context} migration cohorts do not cover completions")
                successes = integer(row, "rescue_success_count")
                if integer(row, "rescue_short_move_count") \
                        + integer(row, "rescue_long_move_count") != successes:
                    raise ValueError(f"{context} method migration breakdown mismatch")
                if integer(row, "rescue_burst_move_count") \
                        + integer(row, "rescue_nonburst_move_count") != successes:
                    raise ValueError(f"{context} burst migration breakdown mismatch")
                accepted = integer(row, "rescue_accepted_candidate_count")
                inspected = integer(row, "rescue_candidate_count")
                queue_entries = integer(row, "rescue_queue_entries_inspected_count")
                targets = integer(row, "rescue_target_evaluation_count")
                if inspected > queue_entries:
                    raise ValueError(f"{context} candidate scans exceed queue scans")
                if accepted > inspected:
                    raise ValueError(f"{context} accepted more candidates than inspected")
                if targets > accepted * integer(row, "h_targets"):
                    raise ValueError(f"{context} exceeded the configured target bound")

                method = row["method"]
                if method == "M1_RescueSched":
                    if integer(row, "rescue_migrated_finished") != successes:
                        raise ValueError(f"{context} RescueSched completion/move mismatch")
                    if integer(row, "control_check_count") \
                            != integer(row, "rescue_attempt_count"):
                        raise ValueError(f"{context} RescueSched check accounting mismatch")
                    if integer(row, "control_queue_entry_count") != queue_entries:
                        raise ValueError(f"{context} RescueSched queue accounting mismatch")
                    if integer(row, "control_candidate_count") != accepted:
                        raise ValueError(f"{context} RescueSched candidate accounting mismatch")
                    if integer(row, "control_target_count") != targets:
                        raise ValueError(f"{context} RescueSched target accounting mismatch")
                else:
                    rescue_only = (
                        "rescue_attempt_count", "rescue_candidate_count",
                        "rescue_queue_entries_inspected_count",
                        "rescue_accepted_candidate_count", "rescue_target_evaluation_count",
                        "rescue_source_revalidation_reject_count",
                        "rescue_remote_revalidation_reject_count", "rescue_success_count",
                        "rescue_short_move_count", "rescue_long_move_count",
                        "rescue_burst_move_count", "rescue_nonburst_move_count",
                        "rescue_migrated_finished",
                    )
                    if any(integer(row, field) != 0 for field in rescue_only):
                        raise ValueError(f"{context} non-RescueSched row has rescue cohorts")
                    if integer(row, "rescue_nonmigrated_finished") != measurement:
                        raise ValueError(f"{context} non-RescueSched completion cohort mismatch")

                estimator_updates = integer(row, "control_estimator_update_count")
                total_generated = integer(row, "total_generated")
                if not measurement <= estimator_updates <= total_generated:
                    raise ValueError(
                        f"{context} estimator control count is outside the "
                        "measurement interval bounds")
                expected_cost = sum(
                    integer(row, count_field) * number(row, cost_field)
                    for count_field, cost_field in zip(
                        CONTROL_COUNT_FIELDS, CONTROL_COST_FIELDS))
                actual_cost = number(row, "configured_control_cost_sum_us")
                if not math.isclose(
                        actual_cost, expected_cost, rel_tol=1e-5, abs_tol=1e-6):
                    raise ValueError(
                        f"{context} configured control cost mismatch: "
                        f"expected={expected_cost} actual={actual_cost}")
                placement = row["placement_mode"]
                if placement == "request_random" and row["trace_version"] != "rescuesched-trace-v2":
                    raise ValueError(f"{context} request-random trace identity changed")
                if placement == "flow_affine" and row["trace_version"] != "rescuesched-trace-v3":
                    raise ValueError(f"{context} flow-affine trace is not v3")

                result_key = (profile, row["workload"], row["rho"],
                              integer(row, "seed"), row["method"])
                if result_key in result_keys:
                    raise ValueError(f"{context} duplicate result key={result_key}")
                result_keys.add(result_key)

                trace_key = (
                    row["workload"], row["rho"], integer(row, "seed"), placement,
                    integer(row, "flow_count"), row["flow_zipf_alpha"],
                    integer(row, "flow_hash_seed"),
                )
                prior_hash = shared_trace.setdefault(trace_key, row["trace_sha256"])
                if prior_hash != row["trace_sha256"]:
                    raise ValueError(
                        f"{context} policy parameters changed a placement-equivalent trace")
                row["_profile"] = profile
                row["_source"] = str(path)
                rows.append(row)

    groups: dict[tuple[str, str, str, int], set[str]] = defaultdict(set)
    for row in rows:
        groups[(row["_profile"], row["workload"], row["rho"],
                integer(row, "seed"))].add(row["method"])
    for key, methods in groups.items():
        if methods != set(PRIMARY_METHODS):
            raise ValueError(f"{key} does not contain all primary methods: {methods}")
    return rows


def validate_matrix(rows: list[dict[str, str]], tier: str) -> None:
    warmup, measurement, expected_seeds = TIER_COHORTS[tier]
    actual_points = {(row["workload"], number(row, "rho")) for row in rows}
    actual_seeds = {integer(row, "seed") for row in rows}
    if actual_points != ANCHOR_POINTS:
        raise ValueError(
            f"anchor point mismatch: expected={ANCHOR_POINTS} actual={actual_points}")
    if actual_seeds != expected_seeds:
        raise ValueError(
            f"seed mismatch for tier={tier}: expected={expected_seeds} actual={actual_seeds}")
    expected_rows = (len(PROFILE_SPECS) * len(ANCHOR_POINTS)
                     * len(PRIMARY_METHODS) * len(expected_seeds))
    if len(rows) != expected_rows:
        raise ValueError(f"matrix row count mismatch: expected={expected_rows} actual={len(rows)}")

    for row in rows:
        context = row["_source"]
        if integer(row, "warmup_requests") != warmup \
                or integer(row, "measurement_requests") != measurement:
            raise ValueError(f"{context} cohort does not match tier={tier}")
        expected = dict(DEFAULT_PARAMETERS)
        expected.update(PROFILE_SPECS[row["_profile"]][2])
        for field, value in expected.items():
            if isinstance(value, str):
                if row[field] != value:
                    raise ValueError(
                        f"{context} profile={row['_profile']} expected "
                        f"{field}={value} actual={row[field]}")
            elif not close(number(row, field), value):
                raise ValueError(
                    f"{context} profile={row['_profile']} expected "
                    f"{field}={value} actual={row[field]}")


def write_csv(path: pathlib.Path, fields: list[str], rows: list[dict[str, object]]) -> None:
    for row in rows:
        for field, value in row.items():
            if isinstance(value, float) and not math.isfinite(value):
                raise ValueError(f"{path} non-finite output {field}={value}")
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def summarize(rows: list[dict[str, str]], profiles: dict[str, dict[str, str]],
              out_dir: pathlib.Path) -> None:
    groups: dict[tuple[str, str, float, str], list[dict[str, str]]] = defaultdict(list)
    by_seed: dict[tuple[str, str, float, str, int], dict[str, str]] = {}
    for row in rows:
        key = (row["_profile"], row["workload"], number(row, "rho"), row["method"])
        groups[key].append(row)
        by_seed[(key[0], key[1], key[2], key[3], integer(row, "seed"))] = row

    summary_fields = [
        "profile", "dimension", "value", "workload", "rho", "method", "seeds",
        "slo_violation_median", "slo_goodput_per_us_median", "P99_us_median",
        "P999_us_median", "migrated_work_rate_median", "handoffs_per_request_median",
        "rescue_moves_per_request_median", "estimator_mae_us_median",
        "estimator_rmse_us_median", "control_operations_per_request_median",
        "configured_control_cost_per_request_us_median",
        "rescue_migrated_fraction_median", "rescue_migrated_miss_rate_median",
        "rescue_nonmigrated_miss_rate_median",
    ]
    summary_rows: list[dict[str, object]] = []
    for (profile, workload, rho, method), current in sorted(groups.items()):
        measurements = [number(row, "measurement_requests") for row in current]
        goodput = [
            divide(
                number(row, "total_finished")
                * (1.0 - number(row, "slo_violation_rate")),
                number(row, "experiment_duration_us"),
                "SLO goodput") for row in current
        ]
        work_rate = [
            divide(number(row, "intra_moved_work_us"),
                   number(row, "measured_generated_work_us"),
                   "migrated work rate") for row in current
        ]
        control_ops = [
            divide(sum(number(row, field) for field in CONTROL_COUNT_FIELDS),
                   measurement, "control operations per request")
            for row, measurement in zip(current, measurements)
        ]
        meta = profiles[profile]
        summary_rows.append({
            "profile": profile,
            "dimension": meta["dimension"],
            "value": meta["value"],
            "workload": workload,
            "rho": rho,
            "method": method,
            "seeds": len(current),
            "slo_violation_median": median(current, "slo_violation_rate"),
            "slo_goodput_per_us_median": statistics.median(goodput),
            "P99_us_median": median(current, "P99_us"),
            "P999_us_median": median(current, "P999_us"),
            "migrated_work_rate_median": statistics.median(work_rate),
            "handoffs_per_request_median": statistics.median(
                divide(number(row, "descriptor_handoff_count"), measurement,
                       "handoffs per request")
                for row, measurement in zip(current, measurements)),
            "rescue_moves_per_request_median": statistics.median(
                divide(number(row, "rescue_success_count"), measurement,
                       "rescue moves per request")
                for row, measurement in zip(current, measurements)),
            "estimator_mae_us_median": median(current, "estimator_mae_us"),
            "estimator_rmse_us_median": median(current, "estimator_rmse_us"),
            "control_operations_per_request_median": statistics.median(control_ops),
            "configured_control_cost_per_request_us_median": statistics.median(
                divide(number(row, "configured_control_cost_sum_us"), measurement,
                       "configured control cost per request")
                for row, measurement in zip(current, measurements)),
            "rescue_migrated_fraction_median": statistics.median(
                divide(number(row, "rescue_migrated_finished"), measurement,
                       "migrated completion fraction")
                for row, measurement in zip(current, measurements)),
            "rescue_migrated_miss_rate_median": median(
                current, "rescue_migrated_slo_violation_rate"),
            "rescue_nonmigrated_miss_rate_median": median(
                current, "rescue_nonmigrated_slo_violation_rate"),
        })
    write_csv(out_dir / "summary.csv", summary_fields, summary_rows)

    comparison_fields = [
        "profile", "dimension", "value", "workload", "rho", "baseline",
        "paired_seeds", "mean_slo_reduction", "ci95_low", "ci95_high",
        "relative_slo_reduction", "p99_rescue_over_baseline",
        "p999_rescue_over_baseline", "rescue_work_rate_median",
        "baseline_work_rate_median", "relative_work_reduction",
        "descriptive_only",
    ]
    comparison_rows: list[dict[str, object]] = []
    points = sorted({(row["_profile"], row["workload"], number(row, "rho"))
                     for row in rows})
    for profile, workload, rho in points:
        meta = profiles[profile]
        for baseline in BASELINES:
            seeds = sorted({integer(row, "seed") for row in rows
                            if row["_profile"] == profile
                            and row["workload"] == workload
                            and number(row, "rho") == rho})
            deltas = []
            baseline_slo = []
            rescue_p99 = []
            baseline_p99 = []
            rescue_p999 = []
            baseline_p999 = []
            rescue_work = []
            baseline_work = []
            for seed in seeds:
                rescue = by_seed[(profile, workload, rho, "M1_RescueSched", seed)]
                other = by_seed[(profile, workload, rho, baseline, seed)]
                deltas.append(number(other, "slo_violation_rate")
                              - number(rescue, "slo_violation_rate"))
                baseline_slo.append(number(other, "slo_violation_rate"))
                rescue_p99.append(number(rescue, "P99_us"))
                baseline_p99.append(number(other, "P99_us"))
                rescue_p999.append(number(rescue, "P999_us"))
                baseline_p999.append(number(other, "P999_us"))
                rescue_work.append(divide(
                    number(rescue, "intra_moved_work_us"),
                    number(rescue, "measured_generated_work_us"),
                    "RescueSched migrated work rate"))
                baseline_work.append(divide(
                    number(other, "intra_moved_work_us"),
                    number(other, "measured_generated_work_us"),
                    "baseline migrated work rate"))
            low, high = paired_bootstrap(deltas)
            mean_delta = statistics.mean(deltas)
            baseline_mean = statistics.mean(baseline_slo)
            rescue_work_median = statistics.median(rescue_work)
            baseline_work_median = statistics.median(baseline_work)
            comparison_rows.append({
                "profile": profile,
                "dimension": meta["dimension"],
                "value": meta["value"],
                "workload": workload,
                "rho": rho,
                "baseline": baseline,
                "paired_seeds": len(seeds),
                "mean_slo_reduction": mean_delta,
                "ci95_low": low,
                "ci95_high": high,
                "relative_slo_reduction": optional_ratio(
                    mean_delta, baseline_mean, "relative SLO reduction"),
                "p99_rescue_over_baseline": divide(
                    statistics.median(rescue_p99), statistics.median(baseline_p99),
                    "P99 RescueSched/baseline"),
                "p999_rescue_over_baseline": divide(
                    statistics.median(rescue_p999), statistics.median(baseline_p999),
                    "P999 RescueSched/baseline"),
                "rescue_work_rate_median": rescue_work_median,
                "baseline_work_rate_median": baseline_work_median,
                "relative_work_reduction": "" if baseline_work_median == 0.0 else
                1.0 - divide(rescue_work_median, baseline_work_median,
                             "relative work reduction"),
                "descriptive_only": 1,
            })
    write_csv(out_dir / "paired_comparisons.csv", comparison_fields, comparison_rows)

    sensitivity_fields = [
        "profile", "dimension", "value", "workload", "rho", "paired_seeds",
        "same_trace_identity", "mean_slo_change_vs_default",
        "ci95_low", "ci95_high", "p99_over_default", "p999_over_default",
        "work_rate_over_default", "comparison_scope",
    ]
    sensitivity_rows: list[dict[str, object]] = []
    for profile, workload, rho in points:
        if profile == "baseline":
            continue
        seeds = sorted({integer(row, "seed") for row in rows
                        if row["_profile"] == profile and row["workload"] == workload
                        and number(row, "rho") == rho})
        paired = []
        changed_p99 = []
        default_p99 = []
        changed_p999 = []
        default_p999 = []
        changed_work = []
        default_work = []
        same_hash = True
        usable = 0
        for seed in seeds:
            changed = by_seed.get((profile, workload, rho, "M1_RescueSched", seed))
            default = by_seed.get(("baseline", workload, rho, "M1_RescueSched", seed))
            if not changed or not default:
                continue
            usable += 1
            same_hash = same_hash and changed["trace_sha256"] == default["trace_sha256"]
            paired.append(number(changed, "slo_violation_rate")
                          - number(default, "slo_violation_rate"))
            changed_p99.append(number(changed, "P99_us"))
            default_p99.append(number(default, "P99_us"))
            changed_p999.append(number(changed, "P999_us"))
            default_p999.append(number(default, "P999_us"))
            changed_work.append(divide(
                number(changed, "intra_moved_work_us"),
                number(changed, "measured_generated_work_us"),
                "changed profile migrated work rate"))
            default_work.append(divide(
                number(default, "intra_moved_work_us"),
                number(default, "measured_generated_work_us"),
                "default migrated work rate"))
        if not usable:
            continue
        low, high = paired_bootstrap(paired)
        meta = profiles[profile]
        sensitivity_rows.append({
            "profile": profile,
            "dimension": meta["dimension"],
            "value": meta["value"],
            "workload": workload,
            "rho": rho,
            "paired_seeds": usable,
            "same_trace_identity": int(same_hash),
            "mean_slo_change_vs_default": statistics.mean(paired),
            "ci95_low": low,
            "ci95_high": high,
            "p99_over_default": divide(
                statistics.median(changed_p99), statistics.median(default_p99),
                "P99 profile/default"),
            "p999_over_default": divide(
                statistics.median(changed_p999), statistics.median(default_p999),
                "P999 profile/default"),
            "work_rate_over_default": optional_ratio(
                statistics.median(changed_work), statistics.median(default_work),
                "work rate profile/default"),
            "comparison_scope": "same_trace_parameter_sensitivity" if same_hash
            else "placement_model_sensitivity",
        })
    write_csv(out_dir / "sensitivity_vs_default.csv", sensitivity_fields,
              sensitivity_rows)


def write_manifest(args: argparse.Namespace, rows: list[dict[str, str]],
                   profiles: dict[str, dict[str, str]]) -> None:
    points = sorted({(row["workload"], number(row, "rho")) for row in rows})
    seeds = sorted({integer(row, "seed") for row in rows})
    warmups = sorted({integer(row, "warmup_requests") for row in rows})
    measurements = sorted({integer(row, "measurement_requests") for row in rows})
    with (args.out_dir / "manifest.md").open("w", encoding="utf-8") as handle:
        handle.write("# Step-22 local simulation diagnostics manifest\n\n")
        handle.write(f"- Tier: `{args.tier}`\n")
        handle.write(f"- Source revision at launch: `{args.git_revision}`\n")
        handle.write(f"- Source worktree dirty at launch: `{args.git_dirty}`\n")
        handle.write(f"- Simulator SHA-256 at launch: `{args.simulator_sha256}`\n")
        handle.write(f"- Run start (UTC): `{args.started_utc}`\n")
        handle.write(f"- Python/platform: `{platform.python_version()}` / `{platform.platform()}`\n")
        handle.write(f"- Profiles: `{len(profiles)}`\n")
        handle.write(f"- Raw rows: `{len(rows)}`\n")
        handle.write(f"- Warmup cohorts: `{warmups}`\n")
        handle.write(f"- Measurement cohorts: `{measurements}`\n")
        handle.write(f"- Seeds: `{seeds}`\n")
        handle.write(f"- Anchor points: `{points}`\n\n")
        handle.write("## Evidence boundary\n\n")
        handle.write("- This directory is a post-freeze diagnostic artifact, not Step-21 evidence.\n")
        handle.write("- Flow-affine placement is an explicit simulator model, not measured RSS behavior.\n")
        handle.write("- Configured control costs are accounting-only and do not advance simulated time.\n")
        handle.write("- Bootstrap intervals are descriptive and do not rerun the corrected paper gate.\n")
        handle.write("- No profile in this matrix establishes physical overhead or deployability.\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inputs", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--profiles", type=pathlib.Path, required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--tier", choices=["smoke", "pilot", "full"], required=True)
    parser.add_argument("--git-revision", default="unknown")
    parser.add_argument("--git-dirty", choices=["yes", "no", "unknown"], default="unknown")
    parser.add_argument("--simulator-sha256", default="unknown")
    parser.add_argument("--started-utc", default="unknown")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    profiles = read_profiles(args.profiles)
    rows = read_and_validate(args.inputs)
    found_profiles = {row["_profile"] for row in rows}
    if found_profiles != set(profiles):
        raise ValueError(
            f"profile manifest/raw mismatch: manifest={set(profiles)} raw={found_profiles}")
    validate_matrix(rows, args.tier)
    summarize(rows, profiles, args.out_dir)
    write_manifest(args, rows, profiles)
    print(f"validated and summarized {len(rows)} rows across {len(profiles)} profiles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
