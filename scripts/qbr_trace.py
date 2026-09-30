#!/usr/bin/env python3
"""Freeze exogenous arrivals, service demands and capacity changes independently of policy."""
import argparse
import hashlib
import json
import math
import random
import struct
from pathlib import Path

HEADER = struct.Struct("<8sIIdd")
REQUEST = struct.Struct("<ddBI")
CHANGE = struct.Struct("<did")


def generate(path, scenario="steady", rho=0.85, seed=11, hosts=16, workers=8,
             warmup=5000.0, duration=20000.0):
    if scenario not in ("steady", "capacity", "stall", "burst", "heavy"):
        raise ValueError(scenario)
    end = warmup + duration
    arrival_rng, service_rng, capacity_rng = (random.Random(seed ^ x) for x in (0x1234, 0x5678, 0x9012))
    mean_work = .8 * 5 + .2 * 100 + 2.1
    rate = rho * hosts * workers / mean_work
    segments = [0.0, end]
    if scenario == "burst":
        for start in range(int(warmup), int(end), 4000):
            segments.extend([float(start), min(end, start + 600.0)])
    segments = sorted(set(segments))
    arrivals = []
    for left, right in zip(segments, segments[1:]):
        burst = scenario == "burst" and left >= warmup and (left - warmup) % 4000 < 600
        lam = rate * (1.5 if burst else 1)
        t = left + arrival_rng.expovariate(lam)
        while t < right:
            arrivals.append(t)
            t += arrival_rng.expovariate(lam)
    requests = []
    sigma = 1.0 if scenario == "heavy" else .35
    for t in arrivals:
        kind = int(service_rng.random() >= .8)
        mean = (5.0, 100.0)[kind]
        service = service_rng.lognormvariate(math.log(mean) - sigma * sigma / 2, sigma)
        # Wire payload varies by class, independent of its realized service demand.
        requests.append((t, service, kind, (128, 512)[kind]))
    changes = []
    if scenario == "capacity":
        for start in range(int(warmup), int(end), 4000):
            affected = capacity_rng.sample(range(hosts), max(1, hosts // 4))
            for h in affected:
                changes.extend([(float(start), h, .25), (min(end - 1e-6, start + 1200.0), h, 1.0)])
    if scenario == "stall":
        # One host nearly stalls while aggregate spare capacity remains at rho <= .85.
        # This distinguishes movable imbalance from rack-wide capacity shortage.
        for start in range(int(warmup), int(end), 1000):
            h = capacity_rng.randrange(hosts)
            changes.extend([(float(start), h, .02), (min(end - 1e-6, start + 200.0), h, 1.0)])
    changes.sort()
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(HEADER.pack(b"QBRTRC1\n", len(requests), len(changes), warmup, end))
        for r in requests:
            f.write(REQUEST.pack(*r))
        for v in changes:
            f.write(CHANGE.pack(*v))
    metadata = dict(scenario=scenario, rho=rho, seed=seed, hosts=hosts, workers=workers,
                    warmup_us=warmup, duration_us=duration, requests=len(requests),
                    mean_work_us=mean_work, nominal_arrival_mrps=rate,
                    actual_offered_work=sum(r[1] + 2.1 for r in requests) / (end * hosts * workers),
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    service_sigma=sigma, bytes_by_class=[128, 512])
    path.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def load_requests(path):
    data = Path(path).read_bytes()
    magic, n, _, warmup, end = HEADER.unpack_from(data)
    if magic != b"QBRTRC1\n":
        raise ValueError("bad trace")
    return [REQUEST.unpack_from(data, HEADER.size + i * REQUEST.size) for i in range(n)], warmup, end


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("path"); p.add_argument("--scenario", default="steady")
    p.add_argument("--rho", type=float, default=.85); p.add_argument("--seed", type=int, default=11)
    p.add_argument("--hosts", type=int, default=16); p.add_argument("--workers", type=int, default=8)
    p.add_argument("--warmup", type=float, default=5000); p.add_argument("--duration", type=float, default=20000)
    args = vars(p.parse_args()); print(json.dumps(generate(**args), indent=2))
