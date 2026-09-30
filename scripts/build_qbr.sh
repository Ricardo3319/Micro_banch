#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-qbr
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude src/qbr/main.cpp src/qbr/simulator.cpp -o build-qbr/batchsched
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude tests/qbr_tests.cpp src/qbr/simulator.cpp -o build-qbr/qbr_tests
./build-qbr/qbr_tests
