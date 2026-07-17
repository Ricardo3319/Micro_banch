# CloudLab Runbook

## Topology

Reserve these two Linux nodes on one experiment LAN:

| CloudLab node | Host | Purpose |
| --- | --- | --- |
| `node0` | `amd140.utah.cloudlab.us` | Dedicated server and main experiment machine |
| `node1` | `amd136.utah.cloudlab.us` | Load generator, observer, and coordinator |

Use the experiment-network IPv4 address, not the CloudLab control-network
address or `*.utah.cloudlab.us` hostname, for RPC traffic. Both machines are
`c6525-25g`; keep their OS image fixed for all final repetitions. Do not run a
client on `node0`, because client CPU and network activity would contaminate
the scheduler and NUMA measurements on the main experiment machine.

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

Run preflight on both machines. Supply each machine's experiment interface to
capture NIC/RSS information:

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

From `node1`:

```bash
ping -c 3 <server-experiment-ip>
```

Verify that `node1` can control `node0` non-interactively. The login below is
the CloudLab control path, not the RPC path:

```bash
ssh -o BatchMode=yes Mingyang@amd140.utah.cloudlab.us hostname
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

## Run The Two-Node Gate

Run this on `node1 / amd136`. It generates one trace locally, starts two local
client processes with `--client-index 0|1 --client-count 2`, and starts only
the RPC server on `node0 / amd140`:

```bash
cd ~/Micro_banch
bash scripts/run_two_node_rpc_smoke.sh \
  --server-host Mingyang@amd140.utah.cloudlab.us \
  --server-ip <node0-experiment-ip> \
  --server-repo-dir '~/Micro_banch' \
  --build-dir build-cloudlab \
  --workers 16 \
  --cpus 0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30 \
  --flow-sockets 256 \
  --workload W3 --rho 0.85 --seed 11 \
  --warmup 500 --requests 5000
```

The coordinator refuses a dirty checkout or mismatched commits. It creates one
flow-affine trace, checks its SHA-256 after transfer, runs all four policies
with a shared absolute client start time, collects all outputs, and executes
`validate_rpc_two_node_run.py`. The client processes split requests by
`flow_id % 2`; their deterministic source-port ranges are automatically offset
by client index, so they can run concurrently on one host without overlap.

The gate is complete only when `TWO_NODE_RPC_STATUS.txt` says `status=PASS`.

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

The two-node helper is deliberately a smoke coordinator. For the final
matrix, invoke it per frozen block or wrap the same server/client commands with
the archived schedule; do not weaken its trace, status, or ingress-map checks.

## Result Checklist

Archive together:

- exact commit and clean status from every node;
- preflight and host-state directories;
- generated trace, embedded hash, file hash, and run-order CSV;
- server/client command lines and console logs;
- request, decision, migration, summary, status, and manifest files;
- `TWO_NODE_RPC_STATUS.txt` and `SHA256SUMS`;
- all failed infrastructure attempts and the reason for rerun.
