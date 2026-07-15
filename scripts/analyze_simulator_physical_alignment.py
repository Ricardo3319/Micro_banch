#!/usr/bin/env python3
"""Build an evidence-separated simulator/runtime alignment table."""

import argparse
import csv
import pathlib
import statistics


SIMULATOR_FIELDS = {
    "workload", "rho", "method", "seeds", "slo_violation_median",
    "slo_goodput_per_us_median", "P99_us_median", "P999_us_median",
    "migrated_work_rate_median", "handoffs_per_request_median",
}
RUNTIME_FIELDS = {
    "evidence_scope", "trace_embedded_sha256", "trace_input_file_sha256",
    "workload", "rho", "seed", "repetition", "policy",
    "total_requests", "measurement_requests", "deadline_violation_rate", "goodput_rps",
    "P99_server_completion_us", "P999_server_completion_us", "migration_count",
    "invariants_pass",
}
RUNTIME_METHODS = {
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
}


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"{path} has no header")
        return list(reader)


def require_fields(path: pathlib.Path, rows: list[dict[str, str]], required: set[str]) -> None:
    if not rows:
        raise ValueError(f"{path} has no rows")
    missing = required - set(rows[0])
    if missing:
        raise ValueError(f"{path} missing fields: {sorted(missing)}")


def median(rows: list[dict[str, str]], field: str) -> float:
    return statistics.median(float(row[field]) for row in rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulator-summary", type=pathlib.Path, required=True)
    parser.add_argument("--simulator-raw", nargs="*", type=pathlib.Path, default=[])
    parser.add_argument("--runtime-summaries", nargs="*", type=pathlib.Path, default=[])
    parser.add_argument("--handoff-summary", type=pathlib.Path)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()

    simulator = read_csv(args.simulator_summary)
    require_fields(args.simulator_summary, simulator, SIMULATOR_FIELDS)

    runtime_rows = []
    for path in args.runtime_summaries:
        rows = read_csv(path)
        require_fields(path, rows, RUNTIME_FIELDS)
        for row in rows:
            scope = row["evidence_scope"]
            if scope not in {
                "local_synthetic_runtime_implementation_validation",
                "physical_rpc_cloudlab",
            }:
                raise ValueError(f"{path} has unknown evidence_scope={scope}")
            row = dict(row)
            row["source_path"] = str(path)
            runtime_rows.append(row)

    runtime_groups: dict[tuple[str, str, str, str], list[dict[str, str]]] = {}
    for row in runtime_rows:
        key = (row["evidence_scope"], row["workload"], row["rho"], row["policy"])
        runtime_groups.setdefault(key, []).append(row)

    paired_traces: dict[tuple[str, str, str, str, str], set[tuple[str, str]]] = {}
    paired_methods: dict[tuple[str, str, str, str, str], set[str]] = {}
    paired_cohorts: dict[tuple[str, str, str, str, str], set[tuple[str, str]]] = {}
    for row in runtime_rows:
        key = (row["evidence_scope"], row["workload"], row["rho"],
               row["seed"], row["repetition"])
        paired_traces.setdefault(key, set()).add(
            (row["trace_embedded_sha256"], row["trace_input_file_sha256"]))
        paired_cohorts.setdefault(key, set()).add(
            (row["total_requests"], row["measurement_requests"]))
        methods = paired_methods.setdefault(key, set())
        if row["policy"] in methods:
            raise ValueError(f"duplicate policy in paired runtime unit: {key}")
        methods.add(row["policy"])
    for key, identities in paired_traces.items():
        if len(identities) != 1:
            raise ValueError(f"paired policies do not share one frozen trace: {key}")
        if len(paired_cohorts[key]) != 1:
            raise ValueError(
                f"paired policies do not share one request cohort: {key}; "
                f"cohorts={sorted(paired_cohorts[key])}")
        methods = paired_methods[key]
        if methods != RUNTIME_METHODS:
            raise ValueError(
                f"paired runtime unit must contain exactly four frozen methods: {key}; "
                f"missing={sorted(RUNTIME_METHODS - methods)}; "
                f"unexpected={sorted(methods - RUNTIME_METHODS)}")

    fields = [
        "evidence_scope", "workload", "rho", "method", "paired_units",
        "deadline_metric", "deadline_violation_rate_median", "goodput_metric",
        "goodput_median", "latency_metric", "P99_median_us", "P999_median_us",
        "handoff_metric", "handoffs_per_request_median", "client_rtt_status",
        "claim_eligibility",
    ]
    output = []
    for row in simulator:
        output.append({
            "evidence_scope": "corrected_discrete_event_simulation",
            "workload": row["workload"],
            "rho": row["rho"],
            "method": row["method"],
            "paired_units": row["seeds"],
            "deadline_metric": "server_side_deadline_violation",
            "deadline_violation_rate_median": row["slo_violation_median"],
            "goodput_metric": "successful_server_completions_per_us",
            "goodput_median": row["slo_goodput_per_us_median"],
            "latency_metric": "simulated_server_completion_us",
            "P99_median_us": row["P99_us_median"],
            "P999_median_us": row["P999_us_median"],
            "handoff_metric": "configured_simulated_descriptor_handoffs_per_request",
            "handoffs_per_request_median": row["handoffs_per_request_median"],
            "client_rtt_status": "not_applicable_simulation",
            "claim_eligibility": "authoritative_simulation_only",
        })

    for (scope, workload, rho, policy), rows in sorted(runtime_groups.items()):
        if not all(row["invariants_pass"] == "1" for row in rows):
            raise ValueError(f"runtime group failed invariants: {(scope, workload, rho, policy)}")
        trace_pairs = {
            (row["seed"], row["repetition"], row["trace_embedded_sha256"],
             row["trace_input_file_sha256"])
            for row in rows
        }
        if len(trace_pairs) != len(rows):
            raise ValueError(
                f"runtime group has duplicate seed/repetition/trace identities: "
                f"{(scope, workload, rho, policy)}")
        handoffs = [float(row["migration_count"]) / float(row["measurement_requests"])
                    for row in rows]
        physical = scope == "physical_rpc_cloudlab"
        if physical:
            required_physical = {
                "client_rtt_available", "P99_client_rtt_us", "P999_client_rtt_us",
            }
            missing = required_physical - set(rows[0])
            if missing or not all(row["client_rtt_available"] == "1" for row in rows):
                raise ValueError(
                    "physical_rpc_cloudlab input requires separately measured client RTT "
                    f"fields; missing={sorted(missing)}")
        output.append({
            "evidence_scope": scope,
            "workload": workload,
            "rho": rho,
            "method": policy,
            "paired_units": len({(row["seed"], row["repetition"]) for row in rows}),
            "deadline_metric": "server_side_deadline_violation",
            "deadline_violation_rate_median": median(rows, "deadline_violation_rate"),
            "goodput_metric": "successful_server_completions_per_second",
            "goodput_median": median(rows, "goodput_rps"),
            "latency_metric": "server_side_completion_us",
            "P99_median_us": median(rows, "P99_server_completion_us"),
            "P999_median_us": median(rows, "P999_server_completion_us"),
            "handoff_metric": "measured_runtime_handoffs_per_request",
            "handoffs_per_request_median": statistics.median(handoffs),
            "client_rtt_status": "required_separate_field" if physical
                                 else "unavailable_synthetic_runtime",
            "claim_eligibility": "candidate_physical_evidence_requires_protocol_audit"
                                 if physical else "implementation_validation_only",
        })

    handoff_fields = [
        "evidence_scope", "source", "scenario", "contention", "cost_semantics",
        "configured_scalar_us", "mean_us", "P50_us", "P95_us", "P99_us",
        "P999_us", "claim_eligibility",
    ]
    handoff_rows = []
    configured_costs = set()
    for path in args.simulator_raw:
        rows = read_csv(path)
        require_fields(path, rows, {
            "schema_version", "migration_cost_us", "workload", "rho", "method",
        })
        for row in rows:
            if row["schema_version"] != "rescuesched-v2":
                raise ValueError(f"{path} is not rescuesched-v2")
            configured_costs.add(float(row["migration_cost_us"]))
    for cost in sorted(configured_costs):
        handoff_rows.append({
            "evidence_scope": "corrected_discrete_event_simulation",
            "source": "simulator_raw_csv",
            "scenario": "configured_descriptor_handoff",
            "contention": "not_modeled_as_distribution",
            "cost_semantics": "configured_scalar_delay",
            "configured_scalar_us": cost,
            "mean_us": "",
            "P50_us": "",
            "P95_us": "",
            "P99_us": "",
            "P999_us": "",
            "claim_eligibility": "authoritative_simulation_configuration",
        })
    if args.handoff_summary:
        rows = read_csv(args.handoff_summary)
        require_fields(args.handoff_summary, rows, {
            "scenario", "contention", "median_of_run_mean_us",
            "median_of_run_P50_us", "median_of_run_P95_us",
            "median_of_run_P99_us", "median_of_run_P999_us", "status",
        })
        for row in rows:
            if row["status"] != "PASS":
                raise ValueError(f"handoff scenario did not pass: {row}")
            handoff_rows.append({
                "evidence_scope": "local_host_pinned_handoff_microbenchmark_not_rpc_or_network",
                "source": str(args.handoff_summary),
                "scenario": row["scenario"],
                "contention": row["contention"],
                "cost_semantics": "host_local_primitive_distribution",
                "configured_scalar_us": "",
                "mean_us": row["median_of_run_mean_us"],
                "P50_us": row["median_of_run_P50_us"],
                "P95_us": row["median_of_run_P95_us"],
                "P99_us": row["median_of_run_P99_us"],
                "P999_us": row["median_of_run_P999_us"],
                "claim_eligibility": "preflight_calibration_not_physical_rpc_overhead",
            })
    args.out_dir.mkdir(parents=True, exist_ok=False)
    with (args.out_dir / "alignment.csv").open(
            "w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(output)
    with (args.out_dir / "handoff_cost_alignment.csv").open(
            "w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=handoff_fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(handoff_rows)

    with (args.out_dir / "ALIGNMENT_STATUS.txt").open("w", encoding="ascii") as handle:
        handle.write("status=PASS\n")
        handle.write("simulator_scope=corrected_discrete_event_simulation\n")
        handle.write("local_runtime_scope=implementation_validation_only\n")
        handle.write("physical_scope=absent_unless_input_scope_is_physical_rpc_cloudlab\n")
        handle.write(f"simulator_configured_handoff_cost_count={len(configured_costs)}\n")
        handle.write(f"local_handoff_distribution_present={'YES' if args.handoff_summary else 'NO'}\n")
        handle.write("counterfactual_diagnostics_exported=NO\n")
        handle.write("client_rtt_inferred_from_server_completion=NO\n")

    print(f"wrote {len(output)} evidence-separated rows to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
