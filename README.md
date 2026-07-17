# RescueSched CloudLab Physical Runtime

This branch is the focused Linux implementation for reproducing RescueSched on
CloudLab or another bare-metal cluster. It contains the frozen W2/W3 trace
generator, the pinned-worker runtime, a real UDP RPC server/client, host
instrumentation, and three-node experiment orchestration.

Historical simulation outputs, paper sources, plotting code, and the complete
discrete-event simulator are intentionally absent. They remain available from
the repository history and simulation branches, but are not needed on the
physical machines.

## What Is Implemented

- deterministic flow-affine W2/W3 traces with canonical SHA-256 identity;
- four policies in one shared runtime:
  `L0_RandomCore`, `L1_WorkStealingPolling`, `M0_AltoThreshold`, and
  `M1_RescueSched`;
- pinned worker threads and a common descriptor-handoff primitive;
- Linux UDP server with one `SO_REUSEPORT` socket per ingress shard;
- open-loop UDP clients with stable flow source ports and disjoint partitions;
- server-side completion, client RTT, migration, decision, and invariant logs;
- reproducible CloudLab run order, host-state capture, and result validation.

## Install And Build

Use Ubuntu 22.04 or 24.04 on every node:

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build python3 \
  numactl hwloc ethtool iproute2 linux-tools-common tmux

git clone --branch codex/cloudlab-physical \
  https://github.com/Ricardo3319/Micro_banch.git
cd Micro_banch
cmake -S . -B build-cloudlab -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cloudlab
ctest --test-dir build-cloudlab --output-on-failure
```

Run the complete host gate before using a node:

```bash
bash scripts/run_physical_preflight.sh \
  --build-dir build-cloudlab \
  --expected-commit "$(git rev-parse HEAD)" \
  --require-clean
```

## Fast Local Network Check

This starts the actual UDP server and client over loopback and replays a small
frozen trace:

```bash
bash scripts/run_local_rpc_smoke.sh --build-dir build-cloudlab
```

The command passes only when both `RPC_SERVER_STATUS.txt` and
`RPC_CLIENT_STATUS.txt` report `status=PASS` and every request receives one
valid response.

## Three-Node CloudLab Check

Use one server and two load-generator nodes. Run the coordinator from an
observer node that has key-based SSH access to all three:

```bash
bash scripts/run_three_node_rpc_smoke.sh \
  --server-host server \
  --client0-host client0 \
  --client1-host client1 \
  --server-ip 10.10.1.1 \
  --repo-dir '~/Micro_banch' \
  --build-dir build-cloudlab
```

All four policies consume the same trace. The validator requires complete,
non-overlapping client partitions, one response per request, one trace hash,
and identical flow-to-ingress-shard mapping across policies.

See [docs/CLOUDLAB_RUNBOOK.md](docs/CLOUDLAB_RUNBOOK.md) for node preparation,
network checks, CPU selection, execution order, and result collection. The
frozen experiment boundary is in
[docs/CLOUDLAB_PREREGISTRATION.md](docs/CLOUDLAB_PREREGISTRATION.md), and the
runtime ownership and output contract is in
[docs/PHYSICAL_RUNTIME_CONTRACT.md](docs/PHYSICAL_RUNTIME_CONTRACT.md).

## Repository Layout

```text
include/physical/        Runtime and UDP protocol interfaces
include/sim/             Minimal trace model retained from simulation
src/app/                 Trace generator, runtime, RPC, and microbench CLIs
src/physical/            Trace loader and scheduling runtime
src/core/                Deterministic W2/W3 trace generation only
scripts/                 Preflight, orchestration, validation, instrumentation
tests/                   Runtime and protocol tests
docs/                    CloudLab runbook and frozen contracts
```

Generated builds and experiment output are ignored by Git under `build-*`,
`cmake-build-*`, and `physical-results/`.
