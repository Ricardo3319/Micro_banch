#!/usr/bin/env python3
"""Strict validator for RescueSched v2 CSV files."""

import argparse
import csv
import math
import pathlib
import re
import sys


SCHEMA_VERSION = "rescuesched-v2"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

REQUIRED_COLUMNS = [
    "schema_version",
    "trace_version",
    "trace_sha256",
    "rpc_method_model",
    "deadline_model",
    "offered_load_definition",
    "warmup_requests",
    "measurement_requests",
    "measured_generated_work_us",
    "experiment_duration_us",
    "latency_sample_count",
    "scenario",
    "workload",
    "method",
    "rho",
    "seed",
    "migration_cost_us",
    "service_estimate_mode",
    "work_steal_poll_us",
    "alto_queue_threshold_us",
    "P99_us",
    "P999_us",
    "slo_violation_rate",
    "total_finished",
    "total_generated",
    "migration_rate",
    "intra_move_rate",
    "rescue_success_count",
    "migration_handoff_count",
    "avg_migration_handoff_us",
    "max_migration_handoff_us",
    "max_rescue_commits_per_check",
    "max_target_reservation_work_us",
    "descriptor_handoff_count",
    "avg_descriptor_handoff_us",
    "max_descriptor_handoff_us",
]

DIAGNOSTIC_COLUMNS = [
    "placement_mode",
    "flow_count",
    "flow_zipf_alpha",
    "flow_hash_seed",
    "rescue_queue_entries_inspected_count",
    "rescue_accepted_candidate_count",
    "rescue_target_evaluation_count",
    "estimator_observation_count",
    "estimator_underestimate_count",
    "estimator_overestimate_count",
    "estimator_exact_count",
    "estimator_short_observation_count",
    "estimator_long_observation_count",
    "estimator_mae_us",
    "estimator_rmse_us",
    "control_cost_mode",
    "configured_control_cost_sum_us",
]


def fail(message: str) -> None:
    print(f"schema validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def as_int(row: dict[str, str], name: str, path: pathlib.Path, line: int) -> int:
    try:
        value = as_float(row, name, path, line)
        integer = int(value)
    except (KeyError, ValueError, OverflowError) as exc:
        fail(f"{path}:{line} invalid integer {name}: {exc}")
    if value != integer:
        fail(f"{path}:{line} non-integral {name}={value}")
    return integer


def as_float(row: dict[str, str], name: str, path: pathlib.Path, line: int) -> float:
    try:
        value = float(row[name])
    except (KeyError, ValueError) as exc:
        fail(f"{path}:{line} invalid numeric {name}: {exc}")
    if not math.isfinite(value):
        fail(f"{path}:{line} non-finite numeric {name}={value}")
    return value


def validate_v2(path: pathlib.Path) -> int:
    if not path.exists():
        fail(f"{path} does not exist")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            fail(f"{path} has no header")
        missing = [name for name in REQUIRED_COLUMNS if name not in reader.fieldnames]
        if missing:
            fail(f"{path} missing v2 columns: {', '.join(missing)}")
        diagnostic_present = [
            name for name in DIAGNOSTIC_COLUMNS if name in reader.fieldnames]
        if diagnostic_present and len(diagnostic_present) != len(DIAGNOSTIC_COLUMNS):
            missing_diag = [
                name for name in DIAGNOSTIC_COLUMNS if name not in reader.fieldnames]
            fail(f"{path} has partial diagnostic extension: {', '.join(missing_diag)}")
        has_diagnostics = bool(diagnostic_present)

        rows = 0
        trace_identity: dict[tuple[str, str, str], str] = {}
        for line, row in enumerate(reader, start=2):
            rows += 1
            if None in row or any(value is None for value in row.values()):
                fail(f"{path}:{line} row width does not match header")
            if row["schema_version"] != SCHEMA_VERSION:
                fail(f"{path}:{line} schema_version={row['schema_version']!r}")
            if not row["trace_version"].startswith("rescuesched-trace-v"):
                fail(f"{path}:{line} invalid trace_version={row['trace_version']!r}")
            if not SHA256_RE.fullmatch(row["trace_sha256"]):
                fail(f"{path}:{line} invalid trace_sha256")
            text_fields = [
                "rpc_method_model", "deadline_model", "offered_load_definition",
                "scenario", "workload", "method", "service_estimate_mode",
            ]
            if has_diagnostics:
                text_fields.extend(["placement_mode", "control_cost_mode"])
            for name in text_fields:
                if not row[name].strip():
                    fail(f"{path}:{line} empty {name}")

            warmup = as_int(row, "warmup_requests", path, line)
            measurement = as_int(row, "measurement_requests", path, line)
            finished = as_int(row, "total_finished", path, line)
            generated = as_int(row, "total_generated", path, line)
            samples = as_int(row, "latency_sample_count", path, line)
            if warmup < 0 or measurement <= 0:
                fail(f"{path}:{line} invalid cohort sizes")
            if generated != warmup + measurement:
                fail(f"{path}:{line} total_generated does not equal both cohorts")
            if finished != measurement or samples != measurement:
                fail(f"{path}:{line} measurement cohort did not fully drain")

            if as_float(row, "measured_generated_work_us", path, line) <= 0:
                fail(f"{path}:{line} non-positive measured work")
            if as_float(row, "experiment_duration_us", path, line) <= 0:
                fail(f"{path}:{line} non-positive duration")
            if not 0.0 <= as_float(row, "slo_violation_rate", path, line) <= 1.0:
                fail(f"{path}:{line} SLO violation outside [0,1]")
            if has_diagnostics:
                if row["placement_mode"] not in {"request_random", "flow_affine"}:
                    fail(f"{path}:{line} invalid placement_mode")
                if row["control_cost_mode"] != "accounting_only":
                    fail(f"{path}:{line} invalid control_cost_mode")
                if as_int(row, "estimator_observation_count", path, line) != measurement:
                    fail(f"{path}:{line} estimator diagnostics do not cover cohort")
                estimator_directions = sum(as_int(row, name, path, line) for name in (
                    "estimator_underestimate_count", "estimator_overestimate_count",
                    "estimator_exact_count"))
                if estimator_directions != measurement:
                    fail(f"{path}:{line} estimator direction diagnostics do not cover cohort")
                short_observations = as_int(
                    row, "estimator_short_observation_count", path, line)
                long_observations = as_int(
                    row, "estimator_long_observation_count", path, line)
                if short_observations + long_observations != measurement:
                    fail(f"{path}:{line} estimator class diagnostics do not cover cohort")
                if as_float(row, "configured_control_cost_sum_us", path, line) < 0.0:
                    fail(f"{path}:{line} negative configured control cost")
            for name in ("migration_rate", "intra_move_rate"):
                if not 0.0 <= as_float(row, name, path, line) <= 1.0:
                    fail(f"{path}:{line} {name} outside [0,1]")

            identity = (row["workload"], row["rho"], row["seed"])
            prior = trace_identity.setdefault(identity, row["trace_sha256"])
            if prior != row["trace_sha256"]:
                fail(f"{path}:{line} policies at {identity} use different traces")

        if rows == 0:
            fail(f"{path} contains no rows")
        print(f"{path}: {SCHEMA_VERSION} OK rows={rows}")
        return rows


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="+")
    args = parser.parse_args(argv[1:])
    total = sum(validate_v2(pathlib.Path(item)) for item in args.csv)
    print(f"validated {len(args.csv)} v2 file(s), {total} row(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
