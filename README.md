# RescueSched Simulator and Dual-Node Physical Runtime

This repository contains the complete RescueSched research line:

- a C++17 discrete-event simulator and corrected simulation artifacts;
- paper sources and claim/evidence documents;
- a pinned-worker Linux runtime;
- deterministic W2/W3 trace generation;
- a real UDP RPC server/client using `SO_REUSEPORT` ingress shards;
- two-node CloudLab orchestration, validation, and host instrumentation.

RescueSched migrates a request only while it is still queued, when local
completion is predicted to miss the server-side deadline and a target worker
is predicted to remain feasible after descriptor handoff.

## Current paper line

- Main method: `M1_RescueSched`.
- Baselines: `L0_RandomCore`, `L1_WorkStealingPolling`, and
  `M0_AltoThreshold`.
- Primary outcomes: server-side deadline violation rate and SLO goodput.
- Secondary outcomes: P50/P99/P999 server completion, client RTT, throughput,
  migration/handoff, and scheduler overhead.
- Required physical boundaries: W3 rho 0.70/0.85/0.90 and W2 rho 0.85, with
  all directions reported.

AQB and DQB remain legacy CLI-compatible code paths and are not evidence for
the active RescueSched claim.

## Authoritative WP0/WP1 control files

- `PROJECT_STATE.md`: current branch, lease evidence, blockers, and next steps.
- `EXPERIMENT_CONTRACT.md`: physical methods, matrix, validity, and failure rules.
- `RUNBOOK.md`: node-specific commands, success criteria, and recovery entries.
- `EVIDENCE_INDEX.md`: claim-to-artifact provenance index.
- `config/infocom2027-physical.yaml`: machine-readable physical contract.
- `INFOCOM2027_DUAL_NODE_EXPERIMENT_MASTER_PLAN.md`: detailed master plan.

The current lease and S3 gates must be read from `PROJECT_STATE.md`; a build or
smoke PASS does not authorize formal experiments.

## Install, build, and test

Ubuntu 22.04/24.04:

```bash
sudo apt-get update
sudo apt-get install -y \
  git build-essential cmake ninja-build python3 python3-venv python3-pip \
  numactl hwloc ethtool iproute2 linux-tools-common \
  "linux-tools-$(uname -r)" jq zstd rclone tmux

cmake -S . -B build-integration -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-integration --parallel
ctest --test-dir build-integration --output-on-failure
```

The integrated CTest gate covers simulator validity/reproducibility, corrected
CSV schema, physical runtime, trace generation, RPC protocol/CLI, and CloudLab
run-order generation/verification.

Create the isolated analysis environment:

```bash
python3 -m venv .venv-analysis
.venv-analysis/bin/python -m pip install -r requirements/analysis-lock.txt
.venv-analysis/bin/python -m pip check
```

## Simulator workflow

Run a deterministic smoke:

```bash
./build-integration/simulator --mode rescue-smoke
```

Run the corrected pilot and full simulation matrix:

```bash
bash scripts/run_corrected_eval.sh pilot
bash scripts/run_corrected_eval.sh full
```

The corrected full artifacts are retained in
`artifacts/step-21-corrected-full/`. See:

- `docs/PAPER_CONTRACT_INFOCOM2027.md`
- `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md`
- `docs/ARTIFACT_PROVENANCE.md`
- `paper/`

## Physical runtime workflow

Local implementation gates:

```bash
bash scripts/run_local_physical_runtime_smoke.sh --build-dir build-integration
bash scripts/run_local_rpc_smoke.sh --build-dir build-integration
bash scripts/run_pinned_handoff_microbench.sh --build-dir build-integration
bash scripts/run_sanitizers.sh
```

Host preflight on each physical node:

```bash
bash scripts/run_physical_preflight.sh \
  --build-dir build-integration \
  --expected-commit "$(git rev-parse HEAD)" \
  --require-clean \
  --interface enp65s0f0np0
```

The two-node coordinator runs on node1 and controls only the RPC server on
node0. RPC traffic must use node0's experiment address, while SSH uses the
control hostname/alias:

```bash
bash scripts/run_two_node_rpc_smoke.sh \
  --server-host node0 \
  --server-ip 10.10.1.1 \
  --server-repo-dir /users/Mingyang/Micro_banch \
  --build-dir build-integration \
  --workers 16 \
  --cpus 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 \
  --flow-sockets 256 \
  --workload W3 --rho 0.85 --seed 11 \
  --warmup 500 --requests 5000
```

On the current 16-core/32-thread hosts, CPUs 16-31 are SMT siblings of CPUs
0-15. Do **not** use the obsolete worker example `0,2,4,...,30`, because it
selects both siblings of only eight physical cores. Formal topology and host
settings must be validated and frozen before any pilot.

A local replay, loopback RPC, short two-node smoke, or handoff microbenchmark
is implementation evidence only. Paper-facing physical evidence requires the
frozen 40-block matrix, host metadata, deterministic archives, and verified
remote hashes.

Physical documentation:

- `docs/CLOUDLAB_PREREGISTRATION.md`
- `docs/CLOUDLAB_RUNBOOK.md`
- `docs/PHYSICAL_RUNTIME_CONTRACT.md`
- `RUNBOOK.md`

## Repository layout

```text
include/sim/, src/core/, src/app/main.cpp    complete simulator
include/physical/, src/physical/             physical runtime and trace loader
src/app/rpc_*                                UDP server/client CLIs
scripts/                                    simulation and physical automation
artifacts/                                  retained corrected simulation evidence
paper/                                      INFOCOM manuscript sources
config/                                     simulation and physical contracts
docs/                                       architecture, methodology, and runbooks
```

Generated builds, virtual environments, and physical outputs are ignored by
Git. Secrets and private S3 credentials must never be committed.
