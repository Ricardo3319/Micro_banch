#!/usr/bin/env python3
"""Paired frozen-trace experiments. Resume only against the same model and generator."""
import argparse
import concurrent.futures
import csv
import gzip
import hashlib
import json
import os
import platform
import struct
import subprocess
import time
from pathlib import Path

from qbr_trace import generate, load_requests

ROOT = Path(__file__).resolve().parents[1]
OUTCOME = struct.Struct("<dB")
METHODS = ["jsw", "p2c", "jbsq", "steal", "threshold", "single", "qbr", "work", "bwc"]
SOURCES = ["include/qbr/model.h", "src/qbr/main.cpp", "src/qbr/simulator.cpp", "scripts/qbr_trace.py"]


def source_hashes():
    return {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in SOURCES}


def cases(suite):
    if suite == "pilot":
        return [dict(group=f"{s}-{r}-{seed}", scenario=s, rho=r, seed=seed, options={},
                     variants=[(m, m, {}) for m in METHODS])
                for s in ("steady", "capacity") for r in (.7, .85) for seed in (101, 102)]
    if suite == "main":
        return [dict(group=f"{s}-{r}-{seed}", scenario=s, rho=r, seed=seed, options={},
                     variants=[(m, m, {}) for m in METHODS])
                for s in ("steady", "capacity", "stall", "burst", "heavy") for r in (.5, .7, .85, .95)
                for seed in range(11, 21)]
    if suite == "sensitivity":
        result = []
        axes = {"network-us": [1, 3.15, 6, 12, 20], "report-us": [5, 20, 80],
                "decision-cpu-us": [0, .25, 1, 3], "max-batch": [1, 4, 8, 16, 32],
                "hosts": [8, 16, 32], "bandwidth-gbps": [1, 10, 25],
                "ingress-cpu-us": [.02, .1, .25]}
        for axis, values in axes.items():
            for value in values:
                for seed in range(11, 21):
                    for scenario in ("capacity", "stall"):
                        result.append(dict(group=f"{scenario}-{axis}-{value}-{seed}", scenario=scenario, rho=.85,
                            seed=seed, axis=axis, value=value, options={axis: value},
                            variants=[(m, m, {}) for m in ("jsw", "jbsq", "steal", "threshold", "qbr")]))
        for scenario in ("steady", "capacity", "stall"):
            for seed in range(11, 21):
                result.append(dict(group=f"ablation-{scenario}-{seed}", scenario=scenario, rho=.85,
                    seed=seed, axis="ablation", value=scenario, options={}, variants=[
                        ("jsw", "jsw", {}), ("qbr", "qbr", {}),
                        ("no_reserve", "qbr", {"reserve": 0}),
                        ("fixed_batch", "qbr", {"fixed-batch": 1}),
                        ("jbsq_depth1", "jbsq", {"jbsq-depth": 1}),
                        ("jbsq_depth2", "jbsq", {"jbsq-depth": 2}),
                        ("jbsq_depth4", "jbsq", {"jbsq-depth": 4}),
                        ("jbsq_depth8", "jbsq", {"jbsq-depth": 8})]))
        return result
    raise ValueError(suite)


def execute_group(case, directory, warmup, duration, keep_outcomes):
    directory = Path(directory)
    trace_path = directory / "traces" / (case["group"] + ".bin")
    trace_info = generate(trace_path, case["scenario"], case["rho"], case["seed"],
                          hosts=case["options"].get("hosts", 16), warmup=warmup, duration=duration)
    requests, warmup, _ = load_requests(trace_path)
    base_latencies = None
    rows = []
    for label, method, overrides in case["variants"]:
        options = {**case["options"], **overrides}
        outcome_path = directory / "raw" / f"{case['group']}-{label}.outcomes"
        raw_path = outcome_path.with_suffix(".json")
        cmd = [str(ROOT / "build-qbr/batchsched"), "--trace", str(trace_path), "--method", method,
               "--seed", str(case["seed"]), "--outcomes", str(outcome_path)]
        for k, v in sorted(options.items()):
            cmd.extend(["--" + k, str(v)])
        started = time.monotonic()
        p = subprocess.run(cmd, text=True, capture_output=True, timeout=180, check=True)
        row = json.loads(p.stdout)
        data = outcome_path.read_bytes()
        if len(data) != OUTCOME.size * len(requests):
            raise RuntimeError("outcome count mismatch")
        outcomes = list(OUTCOME.iter_unpack(data))
        if label == "jsw":
            base_latencies = [x[0] for x in outcomes]
        if base_latencies is None:
            raise RuntimeError("reference must run first")
        migrated = [i for i, (_, moved) in enumerate(outcomes) if moved and requests[i][0] >= warmup]
        row["invalid_migration_rate"] = (sum(outcomes[i][0] >= base_latencies[i] for i in migrated) / len(migrated)
                                           if migrated else None)
        row["measured_migrated"] = len(migrated)
        row["migrated_fraction"] = len(migrated) / row["measured"] if row["measured"] else 0
        row["migrated_latency_delta_mean_us"] = (sum(outcomes[i][0] - base_latencies[i] for i in migrated) / len(migrated)
                                                   if migrated else None)
        row.update(group=case["group"], scenario=case["scenario"], rho=case["rho"], seed=case["seed"],
                   variant=label, axis=case.get("axis", "main"), value=case.get("value", ""),
                   trace_sha256=trace_info["sha256"], wall_seconds=time.monotonic() - started,
                   options=options, command=cmd)
        raw_path.write_text(json.dumps(row, indent=2) + "\n")
        if keep_outcomes and case["seed"] in (11, 101):
            with gzip.open(str(outcome_path) + ".gz", "wb") as f:
                f.write(data)
        outcome_path.unlink()  # Only the exact temporary output created above.
        rows.append(row)
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--suite", choices=("pilot", "main", "sensitivity"), default="pilot")
    p.add_argument("--out", required=True); p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--warmup", type=float, default=5000); p.add_argument("--duration", type=float, default=20000)
    p.add_argument("--keep-outcomes", action="store_true")
    a = p.parse_args(); directory = Path(a.out).resolve()
    directory.mkdir(parents=True, exist_ok=True); (directory / "raw").mkdir(exist_ok=True)
    todo = cases(a.suite)
    identity = dict(suite=a.suite, warmup_us=a.warmup, duration_us=a.duration, source_sha256=source_hashes(),
                    binary_sha256=hashlib.sha256((ROOT / "build-qbr/batchsched").read_bytes()).hexdigest())
    manifest_path = directory / "manifest.json"
    if manifest_path.exists():
        old = json.loads(manifest_path.read_text())
        if old["identity"] != identity:
            raise RuntimeError("model/input changed; use a new output directory to preserve prior results")
    else:
        manifest = dict(identity=identity, cases=todo, python=platform.python_version(), platform=platform.platform(),
                        compiler=subprocess.check_output(["g++", "--version"], text=True).splitlines()[0],
                        runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                        git_status=subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
                        note="Policy-level simulation; CPU/link parameters are assumptions, not hardware measurements.")
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    rows, pending = [], []
    for case in todo:
        paths = [directory / "raw" / f"{case['group']}-{v[0]}.json" for v in case["variants"]]
        if all(f.exists() for f in paths):
            rows.extend(json.loads(f.read_text()) for f in paths)
        else:
            pending.append(case)
    print(f"{a.suite}: {len(todo)} groups, {len(pending)} pending", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures = [pool.submit(execute_group, case, directory, a.warmup, a.duration, a.keep_outcomes) for case in pending]
        for i, f in enumerate(concurrent.futures.as_completed(futures), 1):
            rows.extend(f.result())
            if i % 10 == 0 or i == len(pending):
                print(f"completed {i}/{len(pending)} groups", flush=True)
    rows.sort(key=lambda r: (r["group"], r["variant"]))
    (directory / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    keys = [k for k in rows[0] if k not in ("options", "command")]
    with (directory / "results.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, keys, extrasaction="ignore"); writer.writeheader(); writer.writerows(rows)
    print(f"Wrote {len(rows)} runs to {directory}", flush=True)


if __name__ == "__main__":
    main()
