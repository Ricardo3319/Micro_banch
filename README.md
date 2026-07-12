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
