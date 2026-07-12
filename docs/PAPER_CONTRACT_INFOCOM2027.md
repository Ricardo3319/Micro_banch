# RescueSched INFOCOM 2027 Paper Contract

**Working title.** *RescueSched: Deadline-Feasible Descriptor Migration for
RSS-Sharded RPC Servers*

## Problem and hypothesis

RSS or random request placement can create transient per-core FIFO imbalance in
a multicore RPC server, especially under heavy-tailed service times. A queued
request may miss its server-side deadline on its current core even though a
different core could complete it on time after a bounded descriptor handoff.

The paper tests one hypothesis: **under a fixed offered load and migration
budget, migrating only requests that are predicted to miss locally and remain
deadline-feasible remotely improves deadline goodput over load-only queue
repair.** The deployable policy observes an RPC method and completion history;
it never observes the current request's hidden service time.

## Intended contributions

1. A request-specific *rescue window* that requires local miss and remote meet,
   rather than treating queue length or aggregate load as sufficient evidence.
2. A bounded commodity-multicore design that pays measured descriptor-handoff
   time and accounts for target reservations and control-plane work.
3. A policy-independent, versioned trace methodology for paired comparison
   against random placement, work stealing, and threshold-style proactive
   repair, including workload boundaries and estimator error.

## Claim boundary

The primary outcome is deadline violation rate / SLO goodput. P99 and P999 are
secondary outcomes and will not be described as improved unless corrected
experiments support that statement. Oracle estimation is an explicitly labeled
upper bound, not a deployable configuration. Target safety, BMR/UMR causality,
universal workload benefit, and real-machine deployment are not current claims.
No historical AQB/DQB result is evidence for this paper.

ALTOCUMULUS (MICRO 2022) is the closest prior system and already covers
proactive SLO-aware queued-RPC descriptor migration. The novelty claim is
therefore restricted to request-specific outcome-change selection under an
explicit deadline and paid handoff cost. This boundary must appear in the
introduction and related work.

## Evidence gates

- Main results use a non-leaking method-keyed estimator and identical trace for
  every compared policy.
- Migration and scheduling costs are paid or measured, not only used as score
  penalties.
- Results report paired uncertainty across frozen holdout seeds and expose W2,
  overload, and estimator-error boundaries.
- A claim enters the paper only when it maps to a versioned CSV, generating
  command, configuration, trace hash, analysis script, and Git commit.

This contract remains authoritative until a dated revision explicitly replaces
it. It does not pre-commit the paper to a positive result.

## Evidence status after corrected full evaluation

The pre-registered W3 gate passes at rho 0.85 and 0.90 using ten seeds, EWMA,
paid handoff, polling work stealing, and ALTO-style threshold migration. The
claim remains restricted to deadline misses at moderate/high load. W3 rho 0.70
loses to polling work stealing, and W2 exhibits severe P99/P999 regressions even
when deadline misses improve. The paper must present these boundaries and must
not convert the gate result into a universal tail-latency claim.
