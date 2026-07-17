# Physical Runtime Contract

## Evidence Scopes

The repository has two execution modes that share the same descriptor,
scheduler, policy, handoff, and output implementation:

- `LOCAL_TRACE_REPLAY` submits trace rows from a local monotonic clock. It is
  implementation and concurrency evidence, not a network result.
- `NETWORK_INGRESS` accepts real UDP requests. It records server-side timing
  and returns a UDP response so the client can record end-to-end RTT.

A loopback or short two-node smoke proves implementation readiness only.
Paper-facing physical evidence requires the frozen CloudLab matrix, host
metadata, paired repetitions, and archived raw outputs.

## Descriptor Lifecycle

Every trace row owns one stable descriptor:

```text
PENDING -> QUEUED -> RUNNING -> DONE
                    ^
                    |
          IN_FLIGHT-+

PENDING/QUEUED/IN_FLIGHT -> CANCELLED
```

Only `QUEUED` descriptors may migrate. Service is non-preemptive. Deadline
expiry is an outcome rather than implicit cancellation, so accepted requests
drain to a recorded terminal state.

Queue membership, descriptor state, running ownership, destination
reservations, and terminal counters are protected by the runtime coordination
mutex. The completion-updated method-keyed EWMA has a separate short-lived
lock. Policy code cannot inspect the current request's hidden service demand.

The common handoff primitive removes a revalidated queued descriptor, marks it
`IN_FLIGHT`, reserves estimated destination work, executes the shared handoff
path, then appends it to the destination FIFO tail. All migrating policies use
this path.

## Network Ingress

The server creates one UDP `SO_REUSEPORT` socket per worker/ingress shard. The
kernel-selected receiving socket defines the request's initial worker. A
request is accepted only when both `request_id` and `flow_id` match the loaded
frozen trace; duplicate IDs, malformed datagrams, unknown IDs, and flow
mismatches are counted and fail the status gate.

Each wire request contains:

- request ID;
- flow ID;
- client send timestamp.

Each response echoes those fields and adds server receive/start/finish times,
ingress shard, final worker, migration count, and deadline status. The client
validates request ID, flow ID, send timestamp, and flow partition before
accepting a response.

Clients issue requests open-loop from trace arrival timestamps. Stable sockets
bind deterministic source ports. With multiple clients, trace rows are split by
`flow_id % client_count`. In the two-node topology, both client processes run
on the load-generator host and receive the same `source_port_base`; each client
adds `client_index * flow_sockets`, producing disjoint source-port ranges. A
shared `--start-at-unix-ns` aligns submission.

## Frozen Trace

`rescuesched_trace_generator` retains only the simulation semantics needed on
physical machines: W2 MMPP/bimodal and W3 Poisson/lognormal arrivals, rho and
seed, service demand, deadline, flow identity, RSS-like hash, and canonical
trace SHA-256. The physical loader separately records the embedded canonical
hash and SHA-256 of the exact CSV bytes.

The loader rejects unknown headers, malformed or non-finite values, duplicate
or non-increasing IDs, non-monotonic arrivals, inconsistent embedded hashes,
invalid workers, and unknown methods or placement modes.

## Policy Surface

- `L0_RandomCore`: keep the kernel-selected ingress worker.
- `L1_WorkStealingPolling`: idle workers pull queued work through the common
  handoff primitive.
- `M0_AltoThreshold`: move bounded-prefix candidates when queue work exceeds
  the threshold and predicted completion improves.
- `M1_RescueSched`: move a queued request only when it is predicted locally
  late, remotely feasible, and safe for the bounded destination prefix.

All policies share execution, queues, estimator, handoff, logging, worker
count, CPU allocation, sockets, trace, and request protocol.

## Pass Conditions

`RPC_SERVER_STATUS.txt` and `RPC_CLIENT_STATUS.txt` must both report
`status=PASS`. A valid run also requires:

- every expected request reaches exactly one terminal outcome;
- no duplicate execution or completion;
- no unknown, malformed, mismatched, or duplicate network request;
- no missing, duplicate, invalid, or timed-out client response;
- all destination reservations drain to zero;
- strict worker affinity succeeds for formal physical runs;
- trace identities and output invariants pass.

Server completion and client RTT remain separate measurements. Local replay,
loopback, and microbenchmark outputs must retain their narrower evidence labels.
