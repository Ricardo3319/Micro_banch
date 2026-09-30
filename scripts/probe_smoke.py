#!/usr/bin/env python3
"""Loopback protocol smoke check, not a network performance result."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = str(ROOT / "build-qbr/net_probe")

for train in (1, 8):
    total = (100 + 20) * train
    server = subprocess.Popen([PROBE, "server", "--max-packets", str(total), "--port", "19071"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready = server.stderr.readline()
        if "ready" not in ready:
            raise RuntimeError("server failed: " + ready + server.stderr.read())
        p = subprocess.run([PROBE, "client", "--port", "19071", "--iterations", "100", "--warmup", "20", "--train", str(train)],
                           capture_output=True, text=True, timeout=35, check=True)
        result = json.loads(p.stdout)
        output, errors = server.communicate(timeout=5)
        assert server.returncode == 0, errors
        assert json.loads(output)["received"] == total
        assert result["received"] == 100 * train and result["lost"] == 0
        print(f"UDP loopback train={train}: PASS; performance numbers intentionally not used as cluster evidence")
    finally:
        if server.poll() is None:
            server.terminate(); server.wait(timeout=5)
