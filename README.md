# RescueSched Simulator

This repository contains a C++17 discrete-event simulator for RescueSched, a
request-level queue-repair policy for RSS-sharded multicore RPC servers.
RescueSched migrates a queued request only when it is predicted to miss its
server-side deadline locally and remain feasible after descriptor handoff to a
different core.

## Current paper line

- Main method: `M1_RescueSched`.
- Main baselines: `L0_RandomCore`, `L1_WorkStealingPolling`, and
  `M0_AltoThreshold`. The latter two pay the same descriptor-handoff cost as
  RescueSched.
- Main workload: single-host W3 Poisson/lognormal, with W1/W2 used for boundary
  checks.
- Primary outcome: deadline violation rate / SLO goodput. P99 and P999 are
  secondary outcomes.

AQB and DQB remain in the repository only as historical experiments and legacy
CLI modes. Their historical results are not evidence for the RescueSched paper.

## Build and test

### Linux (Ubuntu 22.04/24.04)

Install the build tools:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build python3 tmux
```

Clone the experiment branch, build, and run the test gate:

```bash
git clone --branch codex/rescuesched-baselines \
  https://github.com/Ricardo3319/Micro_banch.git
cd Micro_banch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/simulator --mode rescue-smoke
```

Run the short corrected evaluation before starting the expensive full matrix:

```bash
bash scripts/run_corrected_eval.sh pilot
```

After checking `artifacts/step-20-corrected-pilot/go_no_go.md`, run the full
matrix in a persistent terminal such as `tmux`:

```bash
tmux new -s rescuesched
bash scripts/run_corrected_eval.sh full 2>&1 | tee corrected-full.log
```

The full run writes to `artifacts/step-21-corrected-full`. Record
`git rev-parse HEAD`, compiler/CMake versions, `uname -a`, `lscpu`, and the CPU
frequency governor with the result manifest. For the planned physical-machine
work, continue with `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md`.

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run a deterministic RescueSched smoke check:

```powershell
.\build\simulator.exe --mode rescue-smoke
```

Run a small configurable RescueSched experiment:

```powershell
.\build\simulator.exe --config config/rescuesched.yaml
```

Start with `docs/01-project-overview.md` for the repository map and
`docs/PAPER_CONTRACT_INFOCOM2027.md` for the paper claim boundary.
