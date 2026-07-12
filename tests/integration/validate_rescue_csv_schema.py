#!/usr/bin/env python3
"""Strict validator for RescueSched v2 CSV files."""

import argparse
import csv
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


def fail(message: str) -> None:
    print(f"schema validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def as_int(row: dict[str, str], name: str, path: pathlib.Path, line: int) -> int:
    try:
        value = float(row[name])
        integer = int(value)
    except (KeyError, ValueError) as exc:
        fail(f"{path}:{line} invalid integer {name}: {exc}")
    if value != integer:
        fail(f"{path}:{line} non-integral {name}={value}")
    return integer


def as_float(row: dict[str, str], name: str, path: pathlib.Path, line: int) -> float:
    try:
        return float(row[name])
    except (KeyError, ValueError) as exc:
        fail(f"{path}:{line} invalid numeric {name}: {exc}")


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

        rows = 0
        trace_identity: dict[tuple[str, str, str], str] = {}
        for line, row in enumerate(reader, start=2):
            rows += 1
            if row["schema_version"] != SCHEMA_VERSION:
                fail(f"{path}:{line} schema_version={row['schema_version']!r}")
            if not row["trace_version"].startswith("rescuesched-trace-v"):
                fail(f"{path}:{line} invalid trace_version={row['trace_version']!r}")
            if not SHA256_RE.fullmatch(row["trace_sha256"]):
                fail(f"{path}:{line} invalid trace_sha256")
            for name in ("rpc_method_model", "deadline_model", "offered_load_definition",
                         "scenario", "workload", "method", "service_estimate_mode"):
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
