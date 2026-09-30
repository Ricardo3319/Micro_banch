#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-qbr
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic src/qbr/net_probe.cpp -o build-qbr/net_probe
