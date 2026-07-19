#!/usr/bin/env python3
"""Read-only validator for RescueSched INFOCOM 2027 WP3 host profiles."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
from typing import Iterable


def run(*args: str, check: bool = True) -> str:
    proc = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and proc.returncode:
        raise RuntimeError(f"command failed ({proc.returncode}): {shlex.join(args)}: {proc.stderr.strip()}")
    return proc.stdout


def load_profile(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    assignment = re.compile(r"^([A-Z][A-Z0-9_]*)=(.*)$")
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = assignment.fullmatch(line)
        if not match:
            raise ValueError(f"{path}:{lineno}: not a declarative assignment")
        key, raw_value = match.groups()
        words = shlex.split(raw_value, comments=False, posix=True)
        if len(words) > 1:
            raise ValueError(f"{path}:{lineno}: value must be one shell word")
        result[key] = words[0] if words else ""
    return result


def cpus(text: str) -> list[int]:
    values: set[int] = set()
    if not text.strip():
        return []
    for item in text.split(","):
        item = item.strip()
        if "-" in item:
            lo, hi = map(int, item.split("-", 1))
            if hi < lo:
                raise ValueError(f"invalid CPU range {item}")
            values.update(range(lo, hi + 1))
        elif item:
            values.add(int(item))
    return sorted(values)


def ethtool_field(iface: str, field: str) -> str:
    for line in run("ethtool", "-i", iface).splitlines():
        key, _, value = line.partition(":")
        if key == field:
            return value.strip()
    return ""


def channel_values(iface: str) -> tuple[int, int]:
    section = ""
    maximum = current = None
    for line in run("ethtool", "-l", iface).splitlines():
        if line.startswith("Pre-set maximums:"):
            section = "maximum"
        elif line.startswith("Current hardware settings:"):
            section = "current"
        elif line.strip().startswith("Combined:"):
            value = int(line.split(":", 1)[1].strip())
            if section == "maximum":
                maximum = value
            elif section == "current":
                current = value
    if maximum is None or current is None:
        raise RuntimeError("could not parse ethtool channel state")
    return maximum, current


def ring_values(iface: str) -> tuple[int, int, int, int]:
    section = ""
    values: dict[tuple[str, str], int] = {}
    for line in run("ethtool", "-g", iface).splitlines():
        if line.startswith("Pre-set maximums:"):
            section = "maximum"
        elif line.startswith("Current hardware settings:"):
            section = "current"
        elif section and re.match(r"^(RX|TX):", line.strip()):
            key, value = line.strip().split(":", 1)
            values[(section, key)] = int(value.strip())
    return values[("maximum", "RX")], values[("maximum", "TX")], values[("current", "RX")], values[("current", "TX")]


def offloads(iface: str) -> dict[str, tuple[str, bool]]:
    result: dict[str, tuple[str, bool]] = {}
    for line in run("ethtool", "-k", iface).splitlines():
        if ": " not in line:
            continue
        key, value = line.split(": ", 1)
        token = value.split()[0]
        result[key] = (token, "[fixed]" in value)
    return result


def route_iface(target: str) -> str:
    words = run("ip", "route", "get", target).split()
    try:
        return words[words.index("dev") + 1]
    except (ValueError, IndexError) as exc:
        raise RuntimeError(f"route to {target} has no device") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--repo-root", default=Path.cwd(), type=Path)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    checks: list[dict[str, object]] = []

    def check(name: str, condition: bool, actual: object, expected: object, detail: str = "") -> None:
        checks.append({"name": name, "pass": bool(condition), "actual": actual, "expected": expected, "detail": detail})

    try:
        profile = load_profile(args.profile.resolve())
        required = {
            "PROFILE_VERSION", "NODE_NAME", "EXPECTED_HOSTNAME", "EXPERIMENT_IFACE", "CONTROL_IFACE",
            "EXPECTED_EXPERIMENT_IPV4", "EXPECTED_CONTROL_IPV4", "EXPERIMENT_PEER_IPV4", "CONTROL_PEER_IPV4",
            "EXPECTED_NIC_DRIVER", "EXPECTED_NIC_PCI", "EXPECTED_NIC_FIRMWARE", "EXPECTED_NIC_NUMA_NODE",
            "EXPECTED_PHYSICAL_CORES", "EXPECTED_LOGICAL_CPUS", "EXPECTED_COMBINED_MAX", "COMBINED_QUEUES",
            "EXPERIMENT_IRQ_CPUS", "ASYNC_IRQ_CPUS", "XPS_CPUS", "GOVERNOR_MODE", "RING_RX", "RING_TX",
            "RX_CHECKSUM", "TX_CHECKSUM", "TSO", "GSO", "GRO", "LRO", "NET_CORE_RMEM_MAX",
            "NET_CORE_WMEM_MAX", "NET_CORE_NETDEV_MAX_BACKLOG", "SOCKET_BUFFER_REQUEST_BYTES", "OPEN_FILE_LIMIT",
            "EXPECTED_BRANCH", "EXPECTED_TAG", "EXPECTED_TAG_OBJECT", "EXPECTED_TAG_TARGET",
        }
        missing = sorted(required - profile.keys())
        if missing:
            raise ValueError(f"missing required profile values: {', '.join(missing)}")

        node = profile["NODE_NAME"]
        if node == "node0":
            role_names = ["WORKER_CPUS", "RECEIVER_CPUS", "RESPONSE_SENDER_CPUS", "SCHEDULER_CPUS", "SERVER_MAIN_CPUS", "HOUSEKEEPING_CPUS", "EXPERIMENT_IRQ_CPUS"]
            expected_counts = {"WORKER_CPUS": 16, "RECEIVER_CPUS": 2, "RESPONSE_SENDER_CPUS": 2, "SCHEDULER_CPUS": 1, "SERVER_MAIN_CPUS": 1, "HOUSEKEEPING_CPUS": 2, "EXPERIMENT_IRQ_CPUS": 8}
            worker_name = "WORKER_CPUS"
            control_names = ["RECEIVER_CPUS", "RESPONSE_SENDER_CPUS", "SCHEDULER_CPUS", "SERVER_MAIN_CPUS", "HOUSEKEEPING_CPUS"]
        elif node == "node1":
            role_names = ["CLIENT0_SENDER_CPUS", "CLIENT0_RECEIVER_CPUS", "CLIENT1_SENDER_CPUS", "CLIENT1_RECEIVER_CPUS", "COORDINATOR_CPUS", "MONITOR_CPUS", "POST_RUN_ARCHIVE_CPUS", "EXPERIMENT_IRQ_CPUS"]
            expected_counts = {"CLIENT0_SENDER_CPUS": 1, "CLIENT0_RECEIVER_CPUS": 1, "CLIENT1_SENDER_CPUS": 1, "CLIENT1_RECEIVER_CPUS": 1, "COORDINATOR_CPUS": 1, "MONITOR_CPUS": 1, "POST_RUN_ARCHIVE_CPUS": 2, "EXPERIMENT_IRQ_CPUS": 8}
            worker_name = ""
            control_names = role_names[:-1]
        else:
            raise ValueError(f"unsupported NODE_NAME={node}")
        for role in role_names + ["UNUSED_EXPERIMENT_CPUS"]:
            if role not in profile:
                raise ValueError(f"missing role value {role}")

        topology: dict[int, tuple[int, int, int, bool]] = {}
        for line in run("lscpu", "-p=CPU,CORE,SOCKET,NODE,ONLINE").splitlines():
            if line.startswith("#"):
                continue
            cpu, core, package, numa, online = line.split(",")
            topology[int(cpu)] = (int(package), int(core), int(numa), online == "Y")
        online = {cpu for cpu, value in topology.items() if value[3]}
        physical = {(package, core) for cpu, (package, core, _, is_online) in topology.items() if is_online}
        check("logical_cpu_count", len(online) == int(profile["EXPECTED_LOGICAL_CPUS"]), len(online), int(profile["EXPECTED_LOGICAL_CPUS"]))
        check("physical_core_count", len(physical) == int(profile["EXPECTED_PHYSICAL_CORES"]), len(physical), int(profile["EXPECTED_PHYSICAL_CORES"]))

        role_map = {name: cpus(profile[name]) for name in role_names}
        for role, expected_count in expected_counts.items():
            check(f"role_count_{role.lower()}", len(role_map[role]) == expected_count, len(role_map[role]), expected_count)
            check(f"role_online_{role.lower()}", set(role_map[role]) <= online, role_map[role], "all online")

        active_pairs = [(role, cpu) for role, values in role_map.items() for cpu in values]
        logical_values = [cpu for _, cpu in active_pairs]
        check("no_duplicate_logical_cpu_across_roles", len(logical_values) == len(set(logical_values)), active_pairs, "unique logical CPUs")
        irq_cpus = role_map["EXPERIMENT_IRQ_CPUS"]
        check("irq_cpu_count_matches_queues", len(irq_cpus) == int(profile["COMBINED_QUEUES"]), len(irq_cpus), int(profile["COMBINED_QUEUES"]))
        irq_cores = {(topology[c][0], topology[c][1]) for c in irq_cpus}
        check("irq_physical_cores_unique", len(irq_cores) == len(irq_cpus), sorted(irq_cores), "one physical core per IRQ CPU")
        control_cpus = [cpu for name in control_names for cpu in role_map[name]]
        control_cores = {(topology[c][0], topology[c][1]) for c in control_cpus}
        check("control_physical_cores_unique", len(control_cores) == len(control_cpus), sorted(control_cores), "one physical core per control CPU")
        check("control_irq_physical_cores_disjoint", control_cores.isdisjoint(irq_cores), sorted(control_cores & irq_cores), [])

        if worker_name:
            worker_cpus = role_map[worker_name]
            worker_cores = {(topology[c][0], topology[c][1]) for c in worker_cpus}
            check("worker_physical_cores_unique", len(worker_cores) == len(worker_cpus), sorted(worker_cores), "one physical core per worker")
            overlap = worker_cores & (control_cores | irq_cores)
            allow = profile.get("ALLOW_WORKER_AUX_SMT_SIBLINGS", "false") == "true"
            check("worker_aux_smt_overlap_contract", (not overlap) or allow, sorted(overlap), "empty unless explicit contract allows SMT siblings", "node0 explicitly contracts worker/aux SMT sibling overlap")
        else:
            active_cores = [(topology[c][0], topology[c][1]) for c in logical_values]
            check("all_active_physical_cores_unique", len(active_cores) == len(set(active_cores)), active_cores, "unique physical cores")

        unused = set(cpus(profile["UNUSED_EXPERIMENT_CPUS"]))
        check("unused_cpus_online", unused <= online, sorted(unused), "all online")
        check("unused_cpus_not_active", unused.isdisjoint(logical_values), sorted(unused & set(logical_values)), [])
        check("async_irq_cpus_online", set(cpus(profile["ASYNC_IRQ_CPUS"])) <= online, cpus(profile["ASYNC_IRQ_CPUS"]), "all online")
        check("xps_cpus_online", set(cpus(profile["XPS_CPUS"])) <= online, cpus(profile["XPS_CPUS"]), "all online")
        check("xps_excludes_irq_cpus", set(cpus(profile["XPS_CPUS"])).isdisjoint(irq_cpus), sorted(set(cpus(profile["XPS_CPUS"])) & set(irq_cpus)), [])

        hostname = socket.getfqdn()
        check("hostname", hostname == profile["EXPECTED_HOSTNAME"], hostname, profile["EXPECTED_HOSTNAME"])
        exp_iface, ctl_iface = profile["EXPERIMENT_IFACE"], profile["CONTROL_IFACE"]
        check("interfaces_distinct", exp_iface != ctl_iface, [exp_iface, ctl_iface], "different")
        for iface in (exp_iface, ctl_iface):
            check(f"interface_exists_{iface}", Path(f"/sys/class/net/{iface}").exists(), iface, "exists")
        exp_ips = run("ip", "-4", "-o", "addr", "show", "dev", exp_iface, "scope", "global").split()
        ctl_ips = run("ip", "-4", "-o", "addr", "show", "dev", ctl_iface, "scope", "global").split()
        check("experiment_ipv4", profile["EXPECTED_EXPERIMENT_IPV4"] in " ".join(exp_ips), " ".join(exp_ips), profile["EXPECTED_EXPERIMENT_IPV4"])
        check("control_ipv4", profile["EXPECTED_CONTROL_IPV4"] in " ".join(ctl_ips), " ".join(ctl_ips), profile["EXPECTED_CONTROL_IPV4"])
        check("experiment_peer_route", route_iface(profile["EXPERIMENT_PEER_IPV4"]) == exp_iface, route_iface(profile["EXPERIMENT_PEER_IPV4"]), exp_iface)
        check("control_peer_route", route_iface(profile["CONTROL_PEER_IPV4"]) == ctl_iface, route_iface(profile["CONTROL_PEER_IPV4"]), ctl_iface)
        check("default_route_control", run("ip", "route", "show", "default").split()[4] == ctl_iface, run("ip", "route", "show", "default").strip(), f"dev {ctl_iface}")

        driver = ethtool_field(exp_iface, "driver")
        firmware = ethtool_field(exp_iface, "firmware-version")
        pci = Path(f"/sys/class/net/{exp_iface}/device").resolve().name
        numa = Path(f"/sys/class/net/{exp_iface}/device/numa_node").read_text().strip()
        check("nic_driver", driver == profile["EXPECTED_NIC_DRIVER"], driver, profile["EXPECTED_NIC_DRIVER"])
        check("nic_firmware", firmware == profile["EXPECTED_NIC_FIRMWARE"], firmware, profile["EXPECTED_NIC_FIRMWARE"])
        check("nic_pci", pci == profile["EXPECTED_NIC_PCI"], pci, profile["EXPECTED_NIC_PCI"])
        check("nic_numa", numa == profile["EXPECTED_NIC_NUMA_NODE"], numa, profile["EXPECTED_NIC_NUMA_NODE"])

        maximum, current = channel_values(exp_iface)
        check("combined_max", maximum == int(profile["EXPECTED_COMBINED_MAX"]), maximum, int(profile["EXPECTED_COMBINED_MAX"]))
        check("combined_target_supported", 0 < int(profile["COMBINED_QUEUES"]) <= maximum, int(profile["COMBINED_QUEUES"]), f"1..{maximum}")
        rx_max, tx_max, rx_current, tx_current = ring_values(exp_iface)
        check("ring_target_supported", int(profile["RING_RX"]) <= rx_max and int(profile["RING_TX"]) <= tx_max, [int(profile["RING_RX"]), int(profile["RING_TX"])], [rx_max, tx_max])
        check("ring_4096_forbidden", profile["RING_RX"] != "4096" and profile["RING_TX"] != "4096", [profile["RING_RX"], profile["RING_TX"]], "not 4096")

        feature_map = {
            "RX_CHECKSUM": "rx-checksumming", "TX_CHECKSUM": "tx-checksumming",
            "TSO": "tcp-segmentation-offload", "GSO": "generic-segmentation-offload",
            "GRO": "generic-receive-offload", "LRO": "large-receive-offload",
        }
        actual_features = offloads(exp_iface)
        for profile_key, feature in feature_map.items():
            actual, fixed = actual_features[feature]
            desired = profile[profile_key]
            supported = not fixed or actual == desired
            check(f"offload_target_supported_{profile_key.lower()}", supported, {"current": actual, "fixed": fixed}, desired)

        policies = sorted(Path("/sys/devices/system/cpu/cpufreq").glob("policy*/scaling_governor")) if Path("/sys/devices/system/cpu/cpufreq").exists() else []
        if profile["GOVERNOR_MODE"] == "absent":
            check("cpufreq_absent", not policies, len(policies), 0)
        else:
            check("cpufreq_present", bool(policies), len(policies), ">0")
            available_ok = all(profile["GOVERNOR_MODE"] in p.with_name("scaling_available_governors").read_text().split() for p in policies)
            check("governor_target_available", available_ok, profile["GOVERNOR_MODE"], "available on every policy")

        hard_nofile = int(run("bash", "-c", "ulimit -Hn").strip())
        check("open_file_limit_supported", int(profile["OPEN_FILE_LIMIT"]) <= hard_nofile, int(profile["OPEN_FILE_LIMIT"]), f"<= {hard_nofile}")
        check("socket_request_within_kernel_max", int(profile["SOCKET_BUFFER_REQUEST_BYTES"]) <= int(profile["NET_CORE_RMEM_MAX"]) and int(profile["SOCKET_BUFFER_REQUEST_BYTES"]) <= int(profile["NET_CORE_WMEM_MAX"]), int(profile["SOCKET_BUFFER_REQUEST_BYTES"]), "<= target rmem/wmem max")

        repo = args.repo_root.resolve()
        branch = run("git", "-C", str(repo), "branch", "--show-current").strip()
        tag_object = run("git", "-C", str(repo), "rev-parse", profile["EXPECTED_TAG"]).strip()
        tag_target = run("git", "-C", str(repo), "rev-parse", f"{profile['EXPECTED_TAG']}^{{}}").strip()
        check("git_branch", branch == profile["EXPECTED_BRANCH"], branch, profile["EXPECTED_BRANCH"])
        check("tag_object", tag_object == profile["EXPECTED_TAG_OBJECT"], tag_object, profile["EXPECTED_TAG_OBJECT"])
        check("tag_target", tag_target == profile["EXPECTED_TAG_TARGET"], tag_target, profile["EXPECTED_TAG_TARGET"])

        result = {
            "status": "PASS" if all(item["pass"] for item in checks) else "FAIL",
            "profile": str(args.profile.resolve()),
            "profile_sha256": run("sha256sum", str(args.profile.resolve())).split()[0],
            "node": node,
            "topology": {
                "online_logical_cpus": sorted(online),
                "physical_cores": sorted([list(v) for v in physical]),
                "cpu_to_socket_core_numa": {str(cpu): list(value[:3]) for cpu, value in sorted(topology.items()) if value[3]},
            },
            "role_map": role_map,
            "host_discovery": {
                "hostname": hostname, "experiment_iface": exp_iface, "control_iface": ctl_iface,
                "nic_driver": driver, "nic_firmware": firmware, "nic_pci": pci, "nic_numa": numa,
                "channels_current": current, "channels_max": maximum,
                "ring_current_rx": rx_current, "ring_current_tx": tx_current,
            },
            "checks": checks,
        }
    except Exception as exc:  # fail closed and preserve a machine-readable reason
        result = {"status": "ERROR", "error": str(exc), "profile": str(args.profile)}

    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.out_dir:
        args.out_dir.mkdir(parents=True, exist_ok=False)
        (args.out_dir / "validator.json").write_text(text, encoding="utf-8")
        lines = [f"status={result['status']}"]
        if "profile_sha256" in result:
            lines.append(f"profile_sha256={result['profile_sha256']}")
        if result["status"] == "ERROR":
            lines.append(f"error={result['error']}")
        else:
            for item in result["checks"]:
                lines.append(f"{'PASS' if item['pass'] else 'FAIL'}\t{item['name']}\tactual={item['actual']}\texpected={item['expected']}\t{item['detail']}")
        (args.out_dir / "validator.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        for p in args.out_dir.iterdir():
            if p.name != "SHA256SUMS" and p.is_file():
                pass
        sums = run("bash", "-c", "find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum", check=True) if False else ""
        # Produce the list without invoking a shell in the output directory.
        import hashlib
        sum_lines = []
        for p in sorted(args.out_dir.iterdir()):
            if p.is_file() and p.name != "SHA256SUMS":
                sum_lines.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}")
        (args.out_dir / "SHA256SUMS").write_text("\n".join(sum_lines) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
