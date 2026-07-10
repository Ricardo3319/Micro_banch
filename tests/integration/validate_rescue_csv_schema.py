#!/usr/bin/env python3
"""Validate the minimum RescueSched CSV schema used by analysis scripts."""

import csv
import pathlib
import sys


REQUIRED_COLUMNS = [
    "scenario",
    "workload",
    "method",
    "rho",
    "seed",
    "check_period_us",
    "epsilon_us",
    "budget_per_check",
    "k_candidates",
    "h_targets",
    "target_safety_enabled",
    "rescuable_filter_enabled",
    "P99_us",
    "P999_us",
    "slo_violation_rate",
    "total_finished",
    "total_generated",
    "migration_rate",
    "invalid_migration_ratio",
    "intra_move_rate",
    "intra_move_count",
    "proactive_intra_attempt_count",
    "proactive_intra_success_count",
    "invalid_intra_move_ratio",
    "rescue_attempt_count",
    "rescue_candidate_count",
    "locally_doomed_count",
    "remote_feasible_count",
    "target_safe_count",
    "rescue_success_count",
    "target_unsafe_reject_count",
    "remote_infeasible_reject_count",
    "beneficial_migration_count",
    "harmful_migration_count",
    "beneficial_migration_ratio",
    "useless_migration_ratio",
    "rescue_per_migration",
]

NUMERIC_COLUMNS = [
    "rho",
    "seed",
    "check_period_us",
    "epsilon_us",
    "budget_per_check",
    "k_candidates",
    "h_targets",
    "P99_us",
    "P999_us",
    "slo_violation_rate",
    "total_finished",
    "total_generated",
]

OPTIONAL_CURRENT_COLUMNS = [
    "migration_cost_us",
    "service_estimate_mode",
    "service_estimate_noise_cv",
    "service_estimate_ewma_alpha",
    "target_insert_policy",
    "hybrid_pressure_ratio",
    "hybrid_min_gain_us",
    "relief_attempt_count",
    "relief_success_count",
    "relief_beneficial_count",
    "relief_useless_count",
    "relief_moved_work_us",
    "relief_beneficial_migration_ratio",
    "relief_useless_migration_ratio",
]


def fail(message: str) -> None:
    print(f"schema validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def validate(path: pathlib.Path) -> int:
    if not path.exists():
        fail(f"{path} does not exist")

    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            fail(f"{path} has no header")

        missing = [name for name in REQUIRED_COLUMNS if name not in reader.fieldnames]
        if missing:
            fail(f"{path} missing required columns: {', '.join(missing)}")

        rows = 0
        for row_number, row in enumerate(reader, start=2):
            rows += 1
            for name in ("scenario", "workload", "method"):
                if not row.get(name, "").strip():
                    fail(f"{path}:{row_number} empty {name}")
            for name in NUMERIC_COLUMNS:
                value = row.get(name, "").strip()
                if not value:
                    fail(f"{path}:{row_number} empty {name}")
                try:
                    float(value)
                except ValueError as exc:
                    fail(f"{path}:{row_number} non-numeric {name}={value!r}: {exc}")

        if rows == 0:
            fail(f"{path} contains no data rows")

        missing_optional = [
            name for name in OPTIONAL_CURRENT_COLUMNS if name not in reader.fieldnames
        ]
        if missing_optional:
            print(
                f"{path}: OK rows={rows}; optional current columns absent="
                f"{','.join(missing_optional)}"
            )
        else:
            print(f"{path}: OK rows={rows}; current schema columns present")
        return rows


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        fail("usage: validate_rescue_csv_schema.py CSV [CSV ...]")
    total_rows = 0
    for item in argv[1:]:
        total_rows += validate(pathlib.Path(item))
    print(f"validated {len(argv) - 1} file(s), {total_rows} row(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
