# CloudLab Runbook

## Topology

Reserve at least four Linux nodes on one experiment LAN:

| Role | Purpose |
| --- | --- |
| `server` | UDP ingress shards, workers, and scheduling policy |
| `client0` | Even flow partition load generator |
| `client1` | Odd flow partition load generator |
| `observer` | SSH coordinator and final result archive |

Use the experiment-network IPv4 address, not the CloudLab control-network
address, for RPC traffic. Keep the same hardware type and OS image for all
final repetitions.

## Prepare Every Node

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build python3 \
  numactl hwloc ethtool iproute2 linux-tools-common tmux

git clone --branch codex/cloudlab-physical \
  https://github.com/Ricardo3319/Micro_banch.git ~/Micro_banch
cd ~/Micro_banch
cmake -S . -B build-cloudlab -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cloudlab
ctest --test-dir build-cloudlab --output-on-failure
```

Record one commit and verify it on every node:

```bash
git rev-parse HEAD
git status --short
```

Run preflight on the server and both clients. Supply the experiment interface
to capture NIC/RSS information:

```bash
bash scripts/run_physical_preflight.sh \
  --build-dir build-cloudlab \
  --expected-commit "$(git rev-parse HEAD)" \
  --require-clean \
  --interface <experiment-interface>
```

Archive each `physical-results/preflight-*` directory before changing host or
NIC settings.

## Check Connectivity

On the server, identify the experiment address and interface:

```bash
ip -brief address
ethtool -i <experiment-interface>
ethtool -x <experiment-interface>
```

From both clients:

```bash
ping -c 3 <server-experiment-ip>
```

Configure key-based SSH aliases on the observer (`server`, `client0`, and
`client1`) and verify non-interactive access:

```bash
ssh -o BatchMode=yes server hostname
ssh -o BatchMode=yes client0 hostname
ssh -o BatchMode=yes client1 hostname
```

## Select Worker CPUs

Inspect `lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE`. Select distinct physical cores;
do not count SMT siblings as separate workers in the primary configuration.
Pass the ordered CPU list with `--cpus`, for example:

```text
--workers 16 --cpus 0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30
```

Formal runs use strict affinity. `--allow-affinity-failure` is for local smoke
only.

## Run The Three-Node Gate

Build the observer checkout too, then run:

```bash
cd ~/Micro_banch
bash scripts/run_three_node_rpc_smoke.sh \
  --server-host server \
  --client0-host client0 \
  --client1-host client1 \
  --server-ip <server-experiment-ip> \
  --repo-dir '~/Micro_banch' \
  --build-dir build-cloudlab \
  --workers 16 \
  --cpus 0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30 \
  --flow-sockets 256 \
  --workload W3 --rho 0.85 --seed 11 \
  --warmup 500 --requests 5000
```

The observer refuses a dirty checkout or mismatched remote commits. It creates
one flow-affine trace, checks its SHA-256 after transfer, runs all four policies
with a shared absolute client start time, collects all outputs, and executes
`validate_rpc_three_node_run.py`.

The gate is complete only when `THREE_NODE_RPC_STATUS.txt` says `status=PASS`.

## Move From Smoke To Final Runs

1. Freeze the exact commit, CloudLab profile, node type, image, CPU list, NIC
   settings, worker count, flow count, timeout, and arrival scaling.
2. Generate and archive the deterministic method schedule:

```bash
python3 scripts/generate_cloudlab_run_order.py \
  --output physical-results/cloudlab-final/run-order.csv
python3 scripts/generate_cloudlab_run_order.py \
  --verify physical-results/cloudlab-final/run-order.csv
```

3. Use `rescuesched_trace_generator` once per workload/rho/seed block. Preserve
   the trace and checksum; every method in the block must consume identical
   bytes.
4. Run methods in the archived order. Start a fresh server for each method and
   never reuse queue or estimator state.
5. Capture host/NIC state before and after each run group with
   `capture_physical_host_state.sh`.
6. Retain failed attempts and apply the preregistered paired rerun rule. Never
   replace an unfavorable valid run.

The three-node helper is deliberately a smoke coordinator. For the final
matrix, invoke it per frozen block or wrap the same server/client commands with
the archived schedule; do not weaken its trace, status, or ingress-map checks.

## Result Checklist

Archive together:

- exact commit and clean status from every node;
- preflight and host-state directories;
- generated trace, embedded hash, file hash, and run-order CSV;
- server/client command lines and console logs;
- request, decision, migration, summary, status, and manifest files;
- `THREE_NODE_RPC_STATUS.txt` and `SHA256SUMS`;
- all failed infrastructure attempts and the reason for rerun.
