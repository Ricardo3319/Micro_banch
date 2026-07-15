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

AQB and DQB remain only as legacy CLI-compatible code paths. Their historical
documents and artifacts have been removed from the active repository surface,
and they are not evidence for the RescueSched paper.

## Build and test

### Linux (Ubuntu 22.04/24.04)

Install the build tools:

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build python3 tmux
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
work, run the host preflight first:

```bash
bash scripts/run_physical_preflight.sh --expected-commit "$(git rev-parse HEAD)"
```

Then continue with `docs/PHYSICAL_MACHINE_RUNBOOK.md` and
`docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md`.

Before CloudLab deployment, the repository also provides an in-process
pinned-worker synthetic request runtime for lifecycle, concurrency, trace
replay, and instrumentation validation:

```bash
bash scripts/run_local_physical_runtime_smoke.sh
bash scripts/run_pinned_handoff_microbench.sh
bash scripts/run_sanitizers.sh
python3 scripts/generate_cloudlab_run_order.py \
  --output physical-results/cloudlab-run-order.csv
```

Its outputs remain under `physical-results/` and are explicitly not RPC,
CloudLab, or paper physical evidence. See
`docs/PHYSICAL_RUNTIME_CONTRACT.md` for the ownership and evidence contract.
The future CloudLab execution protocol is frozen in
`docs/CLOUDLAB_PREREGISTRATION.md`.

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

## Authoritative documents

- `新实验思想指导.md`: problem model and intended RescueSched mechanism.
- `docs/PAPER_CONTRACT_INFOCOM2027.md`: authoritative paper claim boundary.
- `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md`: frozen simulation methodology.
- `docs/ARTIFACT_PROVENANCE.md`: current evidence and reproduction commands.
- `docs/INFOCOM_READINESS.md`: submission blockers and completion criteria.
- `docs/PHYSICAL_MACHINE_RUNBOOK.md`: directly executable Linux host preflight.
- `docs/CLOUDLAB_PREREGISTRATION.md`: frozen paired physical-evaluation plan.

The current simulator evidence is sufficient to freeze the narrow simulation
claim and start writing. It is not, by itself, a completed INFOCOM systems
evaluation; real RPC/runtime validation remains required.
