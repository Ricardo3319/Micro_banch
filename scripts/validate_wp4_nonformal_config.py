#!/usr/bin/env python3
"""Validate immutable WP4 non-formal RPC identity and forbidden parameters."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import yaml

EXPECTED_NODE0_PROFILE_SHA256 = "50a9f9fe2f2c80f37e8e374b359471bc9afb9c5c8958c905ffec313fa980b09b"
EXPECTED_NODE1_PROFILE_SHA256 = "fc6d03bbda541e921d444252ff74aaf944662f63d6ecff536531f26acd11e89e"
EXPECTED_NONFORMAL_PORT = 19040
FORMAL_RESERVED_PORT = 9000
EXPECTED_PRIMARY_WORKERS = 16
FORBIDDEN_RING = 4096


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_profile(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line_number, raw in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{line_number}: malformed profile line")
        key, value = line.split("=", 1)
        if key in values:
            raise ValueError(f"{path}:{line_number}: duplicate profile key {key}")
        values[key] = value
    return values


def nested(mapping: dict[str, Any], *keys: str) -> Any:
    value: Any = mapping
    for key in keys:
        if not isinstance(value, dict) or key not in value:
            raise KeyError(".".join(keys))
        value = value[key]
    return value


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail-closed WP4 non-formal port/profile/ring validator"
    )
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--config", type=Path, default=root / "config/infocom2027-physical.yaml")
    parser.add_argument(
        "--node0-profile", type=Path,
        default=root / "config/host-profiles/infocom2027-node0.env",
    )
    parser.add_argument(
        "--node1-profile", type=Path,
        default=root / "config/host-profiles/infocom2027-node1.env",
    )
    parser.add_argument("--actual-port", type=int, required=True)
    parser.add_argument("--workers", type=int, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    checks: list[dict[str, Any]] = []

    def check(name: str, passed: bool, actual: Any, expected: Any) -> None:
        checks.append({
            "name": name,
            "status": "PASS" if passed else "FAIL",
            "actual": actual,
            "expected": expected,
        })

    try:
        config_bytes = args.config.read_bytes()
        config = yaml.safe_load(config_bytes)
        if not isinstance(config, dict):
            raise ValueError("configuration root is not a mapping")
        profiles = {
            "node0": (args.node0_profile, EXPECTED_NODE0_PROFILE_SHA256),
            "node1": (args.node1_profile, EXPECTED_NODE1_PROFILE_SHA256),
        }
        profile_artifacts: dict[str, Any] = {}

        formal_port = nested(config, "network", "server_port")
        nonformal_port = nested(config, "network", "non_formal_infrastructure_server_port")
        primary_workers = nested(config, "nodes", "node0", "worker_count_primary")
        ring_default = nested(config, "nic_irq_profile", "ring_entries_default")
        ring_used = nested(config, "nic_irq_profile", "ring_4096_used")

        check("formal_port_remains_reserved", formal_port == FORMAL_RESERVED_PORT,
              formal_port, FORMAL_RESERVED_PORT)
        check("nonformal_port_frozen", nonformal_port == EXPECTED_NONFORMAL_PORT,
              nonformal_port, EXPECTED_NONFORMAL_PORT)
        check("actual_port_matches_nonformal", args.actual_port == nonformal_port,
              args.actual_port, nonformal_port)
        check("actual_port_is_not_formal", args.actual_port != FORMAL_RESERVED_PORT,
              args.actual_port, f"not {FORMAL_RESERVED_PORT}")
        check("primary_worker_count_frozen", primary_workers == EXPECTED_PRIMARY_WORKERS,
              primary_workers, EXPECTED_PRIMARY_WORKERS)
        check("actual_worker_count_matches_primary", args.workers == primary_workers,
              args.workers, primary_workers)
        check("config_ring_4096_not_used", ring_used is False, ring_used, False)
        check("default_ring_is_not_forbidden", ring_default != FORBIDDEN_RING,
              ring_default, f"not {FORBIDDEN_RING}")

        for node, (path, expected_sha) in profiles.items():
            actual_sha = sha256(path)
            values = read_profile(path)
            configured_sha = nested(config, "nic_irq_profile", "host_profile_sha256", node)
            rx = values.get("RING_RX")
            tx = values.get("RING_TX")
            check(f"{node}_profile_sha256", actual_sha == expected_sha,
                  actual_sha, expected_sha)
            check(f"{node}_configured_profile_sha256", configured_sha == expected_sha,
                  configured_sha, expected_sha)
            check(f"{node}_ring_rx_present", rx is not None, rx, "integer")
            check(f"{node}_ring_tx_present", tx is not None, tx, "integer")
            if rx is not None:
                check(f"{node}_ring_rx_not_forbidden", rx != str(FORBIDDEN_RING),
                      rx, f"not {FORBIDDEN_RING}")
                check(f"{node}_ring_rx_matches_default", rx == str(ring_default),
                      rx, str(ring_default))
            if tx is not None:
                check(f"{node}_ring_tx_not_forbidden", tx != str(FORBIDDEN_RING),
                      tx, f"not {FORBIDDEN_RING}")
                check(f"{node}_ring_tx_matches_default", tx == str(ring_default),
                      tx, str(ring_default))
            profile_artifacts[node] = {
                "path": str(path.resolve()),
                "sha256": actual_sha,
                "ring_rx": rx,
                "ring_tx": tx,
            }

        failures = [item for item in checks if item["status"] != "PASS"]
        artifact = {
            "format": "rescuesched-wp4-nonformal-config-validation-v1",
            "status": "PASS" if not failures else "FAIL",
            "config": str(args.config.resolve()),
            "config_sha256": hashlib.sha256(config_bytes).hexdigest(),
            "actual_port": args.actual_port,
            "actual_workers": args.workers,
            "formal_port_9000_used": args.actual_port == FORMAL_RESERVED_PORT,
            "ring_4096_used": any(
                item.get("actual") == str(FORBIDDEN_RING)
                for item in checks if "ring_" in item["name"]
            ),
            "profiles": profile_artifacts,
            "checks": checks,
            "failures": [item["name"] for item in failures],
        }
    except (OSError, UnicodeError, ValueError, KeyError, yaml.YAMLError) as error:
        artifact = {
            "format": "rescuesched-wp4-nonformal-config-validation-v1",
            "status": "FAIL",
            "config": str(args.config.resolve()),
            "actual_port": args.actual_port,
            "actual_workers": args.workers,
            "checks": checks,
            "failures": [f"validation_exception: {error}"],
        }

    text = json.dumps(artifact, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        if args.output.exists():
            raise SystemExit(f"refusing to overwrite output: {args.output}")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="ascii")
    print(text, end="")
    return 0 if artifact["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
