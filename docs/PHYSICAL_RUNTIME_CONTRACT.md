# RescueSched Local Physical Runtime Contract

## Status and evidence boundary

This contract governs the in-process, pinned-worker request runtime used to
close implementation blockers before CloudLab deployment. The runtime executes
synthetic service demand from a frozen simulator trace. It is not an RPC
server, does not exercise a NIC or RSS receive path, and does not produce
paper-valid physical results by itself.

Outputs from this runtime may be described as local implementation smoke,
stress, concurrency, or preflight evidence. They must not be described as
CloudLab results, production readiness, end-to-end RPC latency, measured
network behavior, or a completed physical evaluation.

## Descriptor lifecycle

Every accepted trace row owns one stable descriptor for the complete run:

```text
PENDING -> QUEUED -> RUNNING -> DONE
                    ^
                    |
          IN_FLIGHT-+

PENDING/QUEUED/IN_FLIGHT -> CANCELLED
RUNNING + cancel request -> DONE with cancel_requested_after_start=1
```

- `PENDING`: the immutable trace row has been loaded but its open-loop arrival
  time has not been reached.
- `QUEUED`: exactly one worker FIFO owns the descriptor. This is the only state
  from which migration may begin.
- `IN_FLIGHT`: the source FIFO has removed the descriptor and the destination
  has reserved its estimated work. No FIFO owns the descriptor.
- `RUNNING`: exactly one worker owns the descriptor and executes it
  non-preemptively.
- `DONE`: execution completed exactly once and the method-keyed estimator was
  updated exactly once.
- `CANCELLED`: execution never started. A cancellation received during handoff
  is honored before destination insertion.

Deadline expiry is an outcome, not an implicit cancellation. A request that
passes its server-side deadline remains queued or running and drains to a
terminal state unless an explicit cancellation is issued.

## Ownership and buffer lifetime

- A stable runtime-owned descriptor pool retains every descriptor until all
  workers and the scheduler have joined.
- A worker FIFO contains non-owning descriptor pointers. Queue membership and
  descriptor state are changed under the scheduler coordination mutex.
- Synthetic payload service demand is stored outside `DescriptorView` and is
  read only by the execution path.
- Completion callbacks receive an immutable outcome snapshot after terminal
  state has been committed. Each callback fires at most once.

## Synchronization contract

The first implementation uses one explicit scheduler coordination mutex for
descriptor state, FIFO membership, running ownership, target reservations, and
terminal counters. This favors auditable ownership over maximum concurrency.
Workers release the mutex while executing synthetic work.

The shared method-keyed EWMA has its own mutex. Its lock is acquired only for a
short estimate snapshot or completion update. Policy decisions use the
estimate captured when the descriptor is enqueued; they do not receive the
current request's hidden service demand.

The common handoff primitive performs these ordered operations:

1. revalidate `QUEUED` state and source FIFO membership;
2. remove from the source FIFO;
3. set `IN_FLIGHT` and reserve estimated work at the destination;
4. release the coordination mutex and execute the common handoff fence/path;
5. reacquire the mutex, clear the reservation, and append at the destination
   FIFO tail, unless cancellation is pending;
6. set `QUEUED`, notify the destination worker, and record elapsed handoff time.

All four policies use this primitive. A running descriptor is absent from its
FIFO, so a migration attempt cannot pass source revalidation.

## Policy-visible data

The scheduler may inspect only `DescriptorView` fields:

- request ID, method key, planned arrival, server-side deadline;
- current method-keyed service estimate and estimator prior sample count;
- current/initial core, state, and prior migration count.

The scheduler must not inspect synthetic service demand, future completion
time based on actual demand, or any counterfactual outcome. Queue predictions
use estimated work, estimated running residual, and in-flight reservations.

## Frozen trace replay

The loader accepts the simulator's exact request-random v2 and flow-affine v3
CSV schemas. It rejects:

- unknown or reordered headers and mixed trace versions;
- missing, malformed, non-finite, or negative timing values;
- non-unique/non-increasing IDs or non-monotonic arrival times;
- inconsistent embedded trace SHA-256 values;
- initial cores outside the configured worker range;
- unknown method, placement, or Boolean values.

The embedded simulator trace SHA-256 and the SHA-256 of the input CSV bytes are
recorded separately. The CSV alone does not contain every generator parameter,
so the loader does not claim to reconstruct the simulator's canonical trace
hash from rows.

For local worker counts below the simulator's default 16 cores, `trace-generate`
accepts `--trace-core-count N`. Its default remains 16, and corrected evaluation
commands do not use this export-only option.

Replay uses a monotonic clock and planned arrival timestamps. Submission does
not wait for completions or queue capacity, so server slowdown does not reduce
the offered arrival sequence. Generator lag is logged separately. The
server-side completion time is measured from the planned arrival; client RTT is
unavailable in this synthetic runtime.

## Common policy implementations

- `L0_RandomCore`: retains frozen initial placement and performs no handoff.
- `L1_WorkStealingPolling`: an idle worker periodically pulls the front queued
  descriptor from the source with the largest estimated workload.
- `M0_AltoThreshold`: scans bounded source prefixes above a queue-work
  threshold and moves a predicted locally late descriptor when estimated
  completion improves by the configured minimum gain.
- `M1_RescueSched`: moves a queued descriptor only when it is predicted locally
  doomed, remotely deadline-feasible with epsilon, and the bounded destination
  prefix has no predicted deadline risk before insertion.

These are implementation-alignment versions in a shared synthetic runtime.
Their local output is not a replacement for the corrected simulator evidence
or the future CloudLab experiment.

## Event and outcome records

Each run writes separate CSVs for:

- request outcomes: planned arrival, enqueue, service start, finish, deadline,
  initial/final core, migration count, estimator snapshot, server-side
  completion, deadline violation, and client RTT availability;
- decisions: policy check, scanned entries/targets, request/core choice,
  predicted local and remote completion, rejection/commit reason, elapsed
  decision nanoseconds, and hardware cycles when supported;
- migrations: source removal, destination insertion, source/destination core,
  handoff elapsed time, and outcome;
- summary: deadline violation, goodput, P50/P99/P999, migrations, configured
  synthetic work, submission lag, and invariant counters;
- manifest: policy/configuration, trace identities, CPU assignments, affinity
  status, clock, and evidence-scope labels.

Server-side completion and client-observed RTT remain different fields. BMR,
UMR, or other counterfactual diagnostics are not emitted as physical facts.

## Local acceptance gate

A local run passes only if:

- every trace row reaches exactly one terminal state;
- no descriptor executes or completes more than once;
- every successful migration begins from `QUEUED` and inserts at FIFO tail;
- no descriptor is owned by two queues or a queue and a worker;
- target reservations drain to zero;
- every requested worker reports successful affinity when strict affinity is
  enabled;
- all output schemas and trace identities validate.

Passing this gate closes implementation and concurrency blockers only. Paper
physical claims still require the pre-registered CloudLab protocol, paired
repetitions, RPC/network instrumentation, and archived raw evidence.
