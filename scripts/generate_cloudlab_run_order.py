#!/usr/bin/env python3
"""Generate or verify the frozen RescueSched CloudLab method schedule."""

import argparse
import csv
import hashlib
import io
import os
import pathlib
import sys
from collections import Counter


SCHEDULE_VERSION = "rescuesched-cloudlab-order-v1"
ANCHORS = (
    ("W3", "0.85"),
    ("W3", "0.90"),
    ("W3", "0.70"),
    ("W2", "0.85"),
)
SEEDS = ("11", "23", "37", "47", "59", "71", "83", "97", "109", "127")
METHODS = (
    "L0_RandomCore",
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
)
FIELDS = (
    "schedule_version",
    "workload",
    "rho",
    "seed",
    "position",
    "method",
    "block_seed_sha256",
    "balance_group",
    "balance_group_sha256",
    "rotation",
    "method_rank_sha256",
)


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def schedule_rows() -> list[dict[str, str]]:
    blocks = []
    for workload, rho in ANCHORS:
        for seed in SEEDS:
            block_seed = f"{SCHEDULE_VERSION}|{workload}|{rho}|{seed}"
            blocks.append({
                "workload": workload,
                "rho": rho,
                "seed": seed,
                "block_seed": block_seed,
                "block_seed_sha256": sha256_text(block_seed),
            })

    assignments = {}
    sorted_blocks = sorted(blocks, key=lambda block: block["block_seed_sha256"])
    for group_index in range(0, len(sorted_blocks), len(METHODS)):
        group = sorted_blocks[group_index:group_index + len(METHODS)]
        group_number = group_index // len(METHODS) + 1
        group_seed = (
            f"{SCHEDULE_VERSION}|balanced-group-{group_number}|"
            + "|".join(block["block_seed"] for block in group)
        )
        group_sha256 = sha256_text(group_seed)
        ranked = sorted(
            ((sha256_text(f"{group_seed}|{method}"), method)
             for method in METHODS),
            key=lambda item: (item[0], item[1]),
        )
        base_methods = [method for _, method in ranked]
        rank_hashes = {method: rank_hash for rank_hash, method in ranked}
        rotation_offset = int(group_sha256, 16) % len(METHODS)
        for block_offset, block in enumerate(group):
            rotation = (rotation_offset + block_offset) % len(METHODS)
            permutation = base_methods[rotation:] + base_methods[:rotation]
            key = (block["workload"], block["rho"], block["seed"])
            assignments[key] = (
                group_number, group_sha256, rotation, permutation, rank_hashes)

    rows = []
    for workload, rho in ANCHORS:
        for seed in SEEDS:
            block_seed = f"{SCHEDULE_VERSION}|{workload}|{rho}|{seed}"
            group_number, group_sha256, rotation, permutation, rank_hashes = (
                assignments[(workload, rho, seed)])
            for position, method in enumerate(permutation, start=1):
                rows.append({
                    "schedule_version": SCHEDULE_VERSION,
                    "workload": workload,
                    "rho": rho,
                    "seed": seed,
                    "position": str(position),
                    "method": method,
                    "block_seed_sha256": sha256_text(block_seed),
                    "balance_group": str(group_number),
                    "balance_group_sha256": group_sha256,
                    "rotation": str(rotation),
                    "method_rank_sha256": rank_hashes[method],
                })
    validate_rows(rows)
    return rows


def validate_rows(rows: list[dict[str, str]]) -> None:
    expected_blocks = len(ANCHORS) * len(SEEDS)
    if len(rows) != expected_blocks * len(METHODS):
        raise ValueError("internal schedule row-count invariant failed")
    blocks: dict[tuple[str, str, str], list[dict[str, str]]] = {}
    for row in rows:
        key = (row["workload"], row["rho"], row["seed"])
        blocks.setdefault(key, []).append(row)
    if len(blocks) != expected_blocks:
        raise ValueError("internal schedule block-count invariant failed")
    position_counts = {method: Counter() for method in METHODS}
    for key, block in blocks.items():
        if {row["method"] for row in block} != set(METHODS):
            raise ValueError(f"internal method membership invariant failed: {key}")
        if {int(row["position"]) for row in block} != set(range(1, 5)):
            raise ValueError(f"internal position invariant failed: {key}")
        for row in block:
            position_counts[row["method"]][int(row["position"])] += 1
    expected_per_position = expected_blocks // len(METHODS)
    for method, counts in position_counts.items():
        if any(counts[position] != expected_per_position for position in range(1, 5)):
            raise ValueError(
                f"internal position-balance invariant failed: {method} {dict(counts)}")


def schedule_bytes() -> bytes:
    buffer = io.StringIO(newline="")
    writer = csv.DictWriter(buffer, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(schedule_rows())
    return buffer.getvalue().encode("ascii")


def write_schedule(path: pathlib.Path, content: bytes, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"output already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    try:
        temporary.write_bytes(content)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()
    digest = hashlib.sha256(content).hexdigest()
    path.with_name(path.name + ".sha256").write_text(
        f"{digest}  {path.name}\n", encoding="ascii")


def verify_schedule(path: pathlib.Path, expected: bytes) -> None:
    actual = path.read_bytes()
    if actual != expected:
        raise ValueError(f"schedule does not match frozen protocol: {path}")
    sidecar = path.with_name(path.name + ".sha256")
    if not sidecar.is_file():
        raise ValueError(f"schedule checksum sidecar is missing: {sidecar}")
    expected_line = f"{hashlib.sha256(actual).hexdigest()}  {path.name}\n"
    if sidecar.read_text(encoding="ascii") != expected_line:
        raise ValueError(f"schedule checksum sidecar mismatch: {sidecar}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Generate the preregistered four-anchor, ten-seed, four-method "
            "CloudLab execution schedule without consulting performance output."
        ))
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=pathlib.Path)
    mode.add_argument("--verify", type=pathlib.Path)
    parser.add_argument(
        "--force", action="store_true",
        help="replace an existing output; intended for deterministic test fixtures")
    args = parser.parse_args()

    expected = schedule_bytes()
    if args.output:
        write_schedule(args.output, expected, args.force)
        action = "wrote"
        path = args.output
    else:
        if args.force:
            parser.error("--force is valid only with --output")
        verify_schedule(args.verify, expected)
        action = "verified"
        path = args.verify

    print(
        f"{action} {len(schedule_rows())} schedule rows: {path} "
        f"sha256={hashlib.sha256(expected).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"cloudlab run-order error: {error}", file=sys.stderr)
        raise SystemExit(2)
