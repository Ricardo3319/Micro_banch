# RescueSched INFOCOM 2027 Writing Plan

**Working title:** *RescueSched: Deadline-Feasible Descriptor Migration for RSS-Sharded RPC Servers*

**Phase:** 1 - Claim-Evidence Map and Paper Outline
**Audit date:** 2026-07-15
**Current evidence boundary:** corrected discrete-event simulation only
**Submission-rule boundary:** INFOCOM 2027 confirms the technical-paper deadline date as 2026-07-31, but the exact time zone, paper length, anonymity mode, reference-page treatment, supplement policy, and submission-system details remain `UNRESOLVED` in `docs/INFOCOM_2027_SUBMISSION_GUIDE.md`.

This plan does not draft the Introduction or any later paper section. It defines the order and evidentiary conditions under which those sections may be written.

## 1. Section Goal

Phase 1 must answer the following reviewer-facing question before prose drafting begins:

> What is the narrowest technically meaningful RescueSched paper whose problem statement, mechanism description, quantitative results, limitations, and novelty positioning can each be traced to auditable evidence?

The phase therefore freezes:

1. the problem and hypothesis;
2. no more than three contribution candidates;
3. the section and page budget;
4. the placement of simulation and future physical evidence;
5. the claims that are supported, simulation-only, planned, citation-dependent, or prohibited; and
6. the sequence for drafting the remaining sections, with the Abstract written last.

## 2. Allowed Claims

The following claims may guide later drafting, subject to the wording and restrictions in `paper/CLAIM_EVIDENCE_MATRIX.md`.

1. RescueSched studies transient per-core FIFO imbalance under fixed RSS-style or random initial placement in a single-host, 16-core RPC-server model.
2. The implemented policy considers only queued, non-running request descriptors.
3. A RescueSched move requires a predicted local deadline miss and a predicted remote deadline meet after simulated handoff delay, an epsilon margin, target queued work, and in-flight reservations.
4. The primary configuration uses a deployable-estimator design: an RPC-method-keyed EWMA that does not read the current request's hidden service time and is updated after completions.
5. The four primary methods consume the same immutable trace at each workload/rho/seed point; every successful descriptor move by the two strong baselines and RescueSched pays the configured simulated handoff delay.
6. The corrected ten-seed full simulation gate passes for W3 at `rho=0.85` and `rho=0.90` against paid polling work stealing and paid ALTO-style threshold migration, while moving less work.
7. W3 at `rho=0.70` is a required negative boundary: RescueSched loses to polling work stealing and also fails the no-more-migrated-work condition against the ALTO-style baseline.
8. W2 at `rho=0.85` reduces deadline violations but severely increases unconditional P99 and P999 server-side completion latency relative to both strong baselines.
9. The evidence supports neither universal tail-latency improvement nor a universal workload/load claim.
10. The repository currently supports a corrected simulator, validation tests, and a physical-host preflight plan; it does not yet support a real RPC runtime, physical descriptor migration results, production readiness, or a measured physical migration-overhead claim.
11. The implemented target filter may be described as preferring and accepting zero-predicted-risk targets within a bounded pre-migration scan and accounting for target reservations. It may not be described as proving no incremental target-side harm.
12. BMR/UMR and related counterfactual counters may be reported only as simulator diagnostics, not as physically observable or causal outcomes.

## 3. Required Evidence

| Evidence class | Required source | What it can support | Use restriction |
| --- | --- | --- | --- |
| Submission rules | `docs/INFOCOM_2027_SUBMISSION_GUIDE.md` | Confirmed 2026-07-31 deadline date, scope fit, unresolved rule list, internal 2026-07-30 12:00 PDT deadline | Do not infer page count, anonymity, references, supplement, system, or exact deadline time |
| Paper boundary | `docs/PAPER_CONTRACT_INFOCOM2027.md` | Problem, hypothesis, primary metric, negative-result obligation | The older phrase "measured descriptor-handoff time" is not currently supported as a physical result |
| Evaluation protocol | `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md` | Methods, fairness controls, workload matrix, metric hierarchy | The final Step-21 analysis pools ten frozen seeds; do not call its intervals holdout-only |
| Architecture and modules | `docs/ARCHITECTURE.md`; `docs/MODULE_SPEC.md` | Single-host per-core FIFO model, queued-only movement, module boundaries, physical exclusions | Descriptive documentation must agree with source code before use |
| Provenance | `docs/ARTIFACT_PROVENANCE.md`; `artifacts/step-21-corrected-full/manifest.md` | Active evidence chain, source revision, cohorts, seeds, checksums, run count | Step-20 and Step-20b are directional only; Step 00-18 and AQB/DQB are excluded |
| Raw simulation rows | `artifacts/step-21-corrected-full/w1.csv`; `w2.csv`; `w3.csv` | Per-seed policy outcomes, trace hashes, configurations, handoff accounting | Numerical claims must be regenerated or read from these CSVs, not copied from rounded prose |
| Derived simulation results | `artifacts/step-21-corrected-full/summary.csv`; `paired_comparisons.csv`; `go_no_go.md` | Ten-seed medians, paired mean differences, deterministic bootstrap intervals, gate outcome | `summary.csv` contains medians; `paired_comparisons.csv` contains paired means and ratios; do not conflate them |
| Analysis implementation | `scripts/corrected_eval_analysis.py` | Deterministic 10,000-draw paired bootstrap and gate logic | The script combines all ten full-tier seeds |
| Reproduction entry point | `scripts/run_corrected_eval.sh`; `config/rescuesched.yaml` | Frozen workload/rho/seed/cohort invocation | CLI defaults and full-run overrides must be distinguished |
| Mechanism code | `src/core/simulator.cpp`; `src/core/workload_trace.cpp`; `include/sim/common/constants.h`; `include/sim/model/task.h`; `include/sim/model/core.h` | Local-miss/remote-meet filter, bounded scans, EWMA, queued-only movement, delayed arrival, reservations, FIFO insertion | The code does not implement a complete incremental target-harm guarantee |
| Validation | `tests/unit/simulator_validity_tests.cpp`; `tests/integration/validate_rescue_csv_schema.py` | Trace determinism, estimator non-leakage, load calibration, exact percentiles, cohort drain, elapsed simulated handoff, shared trace identity | These tests validate simulator invariants, not real-machine behavior |
| Physical status and plan | `docs/INFOCOM_READINESS.md`; `docs/NEXT_PHASE_AFTER_CORRECTED_EVAL.md`; `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md`; `docs/PHYSICAL_MACHINE_RUNBOOK.md` | Implementation gaps, planned anchors, machine metadata, physical methodology | Every result sentence remains `[PHYSICAL RESULT REQUIRED]` until an auditable runtime and result artifact exist |
| Closest known work | Jiechen Zhao et al., "ALTOCUMULUS: Scalable Scheduling for Nanosecond-Scale Remote Procedure Calls," MICRO 2022, DOI `10.1109/MICRO56248.2022.00040` | Verified bibliographic identity only | Mechanism-level comparison remains `[CITATION REQUIRED]` until the full paper is read and checked |
| Other related work | Formal papers on RSS, RPC scheduling, work stealing, deadline scheduling, and queued descriptor migration | Motivation and novelty comparison | `[CITATION REQUIRED]`; no placeholder BibTeX from `paper/refs.bib` may be reused |

### Authoritative Step-21 identity

- Source revision: `ba81d825eaf1e0b6701e21dbb6462c2a801da0b9`
- Schema/trace: `rescuesched-v2` / `rescuesched-trace-v2`
- Seeds: `11,23,37,47,59,71,83,97,109,127`
- Cohorts per run: 200,000 warmup plus 1,000,000 measurement requests
- Primary methods: `L0_RandomCore`, `L1_WorkStealingPolling`, `M0_AltoThreshold`, and `M1_RescueSched`
- Total runs: 280
- Analysis: deterministic 10,000-draw paired bootstrap
- Current CSV checksums match `artifacts/step-21-corrected-full/manifest.md`

## 4. Draft

This Phase-1 draft is a writing blueprint, not paper body text.

### One-Sentence Problem Statement

In an RSS-sharded multicore RPC server with non-preemptive per-core FIFO queues, fixed request placement can leave a queued request predicted to miss its server-side deadline locally even when a bounded, paid descriptor handoff to another core is predicted to complete it before that deadline.

The real-system assertion that RSS creates this condition requires formal support and, preferably, physical traces: `[CITATION REQUIRED]` and `[PHYSICAL RESULT REQUIRED]`. The current simulator directly establishes only the modeled fixed/random-placement condition.

### One-Sentence Hypothesis

Under a fixed offered load, trace, and descriptor-migration cost, selecting queued requests only when their predicted deadline outcome changes from a local miss to a remote meet can reduce server-side deadline violations relative to load-oriented queue repair without requiring more migrated work at the W3 moderate/high-load operating points.

This is a tested, bounded hypothesis, not a universal claim; W3 `rho=0.70` and W2 tail behavior are explicit counterexamples to broader wording.

### Candidate Contributions

1. **Request-specific rescue criterion.** A descriptor-selection rule that requires a predicted local deadline miss and predicted remote deadline feasibility after handoff and reservation accounting, rather than using queue pressure alone as sufficient evidence for movement.
2. **Bounded simulator realization.** An implementation of queued-only movement with bounded candidate/target scans, a per-check move budget, elapsed simulated handoff delay, in-flight unavailability, target reservations, and a non-leaking method-keyed EWMA. This contribution is simulation-only until `[IMPLEMENTATION DETAIL REQUIRED]` and `[PHYSICAL RESULT REQUIRED]` are resolved for a real RPC runtime.
3. **Auditable paired evaluation method.** A versioned, policy-independent trace and paired corrected-evaluation workflow that applies a common descriptor handoff path to the strong baselines and retains both positive operating points and negative workload/load boundaries.

No contribution will use "first," "novel," "state-of-the-art," "production-ready," or an equivalent priority claim before the related-work and physical-evidence audits are complete.

### Provisional Paper Outline and Page Budget

The following is a **provisional 10-page body budget**, not a confirmed INFOCOM 2027 rule. Abstract/title space is carried on the first page and may force further compression. The budget must be revised immediately when the official 2027 author instructions become readable.

| Section | Subsections | Reviewer question answered | Target pages | Planned figures/tables | Evidence placement |
| --- | --- | --- | ---: | --- | --- |
| 1. Introduction | Problem; gap; narrow approach; contributions; result boundaries | Why is this an INFOCOM networking-systems problem, and what exactly is claimed? | 0.9 | Fig. 1 reference; one compact result sentence only after matrix audit | Narrow W3 simulation result; W3 low-load and W2-tail boundary named; no physical result |
| 2. Background and Motivation | RSS/per-core dispatch; head-of-line effects; deadline versus tail metrics; motivating example | Why can fixed per-core placement create a request-specific rescue opportunity that load-only repair misses? | 0.8 | Fig. 1 system path; optional motivating trace panel only if physical data exists | Code/model facts now; RSS and prior-system facts `[CITATION REQUIRED]`; motivating physical trace `[PHYSICAL RESULT REQUIRED]` |
| 3. Problem Formulation | Server and queue model; request/deadline notation; observability; feasible migration; objectives and non-goals | What information is available, what move is legal, and what metric is optimized? | 0.7 | Table 1 notation/model | `Task`, trace, FIFO, deadline, estimator, and metric definitions from source and tests |
| 4. RescueSched Design | Rescue window; bounded source scan; target selection; reservation/handoff; estimator; complexity; safety boundary | How does the policy decide, what does it cost, and what does it deliberately not guarantee? | 1.5 | Fig. 2 decision timeline/pseudocode; optional compact algorithm listing | `run_rescue_sched_check`, estimator, delayed migration, constants; explicit target-safety limitation |
| 5. Implementation | Simulator modules; event path; configuration; instrumentation; physical-runtime status | What is implemented today, and which parts are still plans? | 0.7 | Table 2 implementation/configuration | Simulator and tests are completed; real RPC runtime is `[IMPLEMENTATION DETAIL REQUIRED]` |
| 6. Evaluation Methodology | Questions; workloads; baselines; fairness; metrics; statistical protocol; provenance | Is the comparison paired, fair, reproducible, and statistically interpretable? | 1.0 | Table 3 workload/method matrix | Step-21 manifest, raw CSV schema, analysis script, test suite; no result interpretation beyond protocol |
| 7. Simulation Evaluation | W3 main result; migrated work; low-load boundary; W2 deadline-tail tradeoff; W1 sanity; summary | Where does the mechanism help, where does it lose, and what metric tradeoff occurs? | 1.5 | Fig. 3 W3 miss results; Fig. 4 migrated work; Fig. 5 W2 tradeoff; Table 4 paired comparisons | Numeric claims exclusively from Step-21 `summary.csv` and `paired_comparisons.csv`, cross-checked against raw CSVs |
| 8. Physical Evaluation | Runtime design/status; setup; handoff and decision overhead; paired CloudLab anchors; simulation/physical alignment | Does the effect survive real queueing, cache/NUMA, polling, packet processing, and physical handoff costs? | 1.0 | Table 5 setup; Fig. 6 anchor results; Table 6 overhead/alignment | Current text is plan/status only; all result cells are `[PHYSICAL RESULT REQUIRED]` |
| 9. Discussion and Limitations | Applicability; estimator error; tail regressions; low-load loss; target-risk semantics; simulator/physical gap; threats | What invalidates or bounds the conclusions? | 0.7 | No required figure; optional limitation table if space permits | Negative Step-21 results and explicit implementation/model gaps |
| 10. Related Work | RSS and per-core RPC; work stealing; deadline/SLO-aware RPC scheduling; ALTOCUMULUS; positioning | Is the mechanism distinct from prior queued-RPC migration and deadline scheduling? | 0.8 | Table 7 mechanism comparison, only after source audit | ALTO bibliographic fact verified; all mechanism comparisons `[CITATION REQUIRED]` |
| 11. Conclusion | Narrow answer; operating boundary; next validation requirement | What has actually been established? | 0.4 | None | Simulation-only conclusion unless physical evidence becomes available |
| **Total** |  |  | **10.0** |  | Provisional until INFOCOM 2027 page rules are confirmed |

### Planned Figure and Table Manifest

No figure or table is generated in Phase 1.

| ID | Planned content | Required evidence | Status and fallback |
| --- | --- | --- | --- |
| Fig. 1 | NIC/RSS to per-core FIFO path, with a queued descriptor moving between worker queues while the running request remains fixed | Architecture/source code plus RSS/runtime citations; physical runtime details if the figure claims concrete implementation | `PLANNED`; use a model diagram if physical implementation remains absent |
| Fig. 2 | Rescue decision timeline: predicted local miss, target work plus reservation, simulated handoff, epsilon, predicted remote meet | `src/core/simulator.cpp`; constants; tests | `SUPPORTED IN SIMULATION ONLY`; must not depict a proven incremental target-harm guarantee |
| Table 1 | Symbols, observability, deadlines, legal movement, and optimization metrics | Source code, module spec, metric definitions | `SUPPORTED IN SIMULATION ONLY` |
| Table 2 | Implemented policy parameters and module mapping | Constants, CLI/config, source revision | `SUPPORTED IN SIMULATION ONLY`; separate configured scalar handoff from any future measured physical distribution |
| Table 3 | Workloads, rho points, seeds, methods, cohorts, metrics, and pairing protocol | Step-21 manifest and evaluation contract | `SUPPORTED` |
| Fig. 3 | W3 deadline violation by method at `rho=0.70,0.85,0.90`, with paired uncertainty where visually valid | Step-21 raw/paired CSVs | `SUPPORTED`; must include the `rho=0.70` loss |
| Fig. 4 | W3 migrated-work rate or relative moved-work comparison | Step-21 raw/paired CSVs | `SUPPORTED`; must show failure against ALTO-style at `rho=0.70` |
| Fig. 5 | W2 deadline-violation versus unconditional P99/P999 tradeoff | Step-21 W2 and derived CSVs | `SUPPORTED`; cannot be captioned as a tail improvement |
| Table 4 | Exact paired W3/W2 comparisons and 95% intervals | `paired_comparisons.csv`; `summary.csv` | `SUPPORTED`; distinguish mean paired differences from medians |
| Table 5 | Physical hardware, NIC/RSS, CPU/NUMA, software, affinity, offered-load, and trace metadata | Frozen physical manifests | `[PHYSICAL RESULT REQUIRED]` |
| Fig. 6 | Paired physical anchor results for W3 `rho=0.70,0.85,0.90` and W2 `rho=0.85` | Real runtime, frozen traces, at least ten paired repetitions, raw logs and analysis | `[PHYSICAL RESULT REQUIRED]` |
| Table 6 | Physical decision/handoff/polling cost and simulator-to-physical alignment | Runtime instrumentation and calibrated simulation sensitivity | `[PHYSICAL RESULT REQUIRED]` |
| Table 7 | Mechanism comparison with ALTOCUMULUS and other RPC schedulers | Full, verified formal references | `[CITATION REQUIRED]`; omit rather than speculate if space or source verification is insufficient |

### Simulation Evidence Placement

1. **Methodology, not results:** Section 6 owns trace identity, cohorts, seeds, methods, handoff fairness, metrics, and bootstrap procedure.
2. **Positive result:** Section 7.1 owns W3 `rho=0.85` and `rho=0.90`; numerical text comes from `paired_comparisons.csv`, and displayed medians come from `summary.csv`.
3. **Efficiency cost:** Section 7.2 owns migrated-work comparisons and handoff/attempt rates; it must not convert simulated handoff delay into physical overhead.
4. **Negative boundary:** Section 7.3 owns W3 `rho=0.70`, including the loss to polling work stealing and the slight moved-work increase versus ALTO-style migration.
5. **Deadline-tail tradeoff:** Section 7.4 owns W2 `rho=0.85`; deadline violations, goodput, P99, and P999 remain separate metrics.
6. **Sanity workload:** Section 7.5 may report W1 compactly but may not use it to claim universality.
7. **Provenance:** A short reproducibility paragraph in Section 6 cites the Step-21 revision, trace/schema versions, cohorts, seed set, and artifact checksums.

### Physical Evidence Placement

Section 8 is strictly separate from simulation. Until an auditable physical runtime and result directory exist, it contains only:

- implementation status;
- experiment plan;
- hypotheses;
- pre-registered anchors and metrics; and
- `[IMPLEMENTATION DETAIL REQUIRED]` / `[PHYSICAL RESULT REQUIRED]` placeholders.

No result from `rescue-main`, a simulator rerun on a physical host, or the process-internal descriptor-handoff microbenchmark may be described as real RPC migration performance. Server-side completion time and end-to-end RPC latency must remain separate even after physical measurements are added.

### Related-Work Positioning

The positioning target is deliberately narrow:

> RescueSched is not a claim that queued RPC work can be migrated. It is a request-specific decision rule that moves a queued descriptor only when the predicted outcome changes from a local server-side deadline miss to a remote meet after accounting for handoff and target reservations.

This wording is a positioning hypothesis until the following audit is complete:

1. Read ALTOCUMULUS in full and map its trigger, observability, target rule, movement cost, SLO model, and implementation path. `[CITATION REQUIRED]`
2. Verify foundational RSS/per-core queue behavior and flow-affinity citations. `[CITATION REQUIRED]`
3. Verify strong work-stealing and multicore RPC scheduler references. `[CITATION REQUIRED]`
4. Verify deadline-aware RPC scheduling and head-of-line mitigation references, including whether they already use request-specific remote feasibility. `[CITATION REQUIRED]`
5. Refer to `M0_AltoThreshold` only as an **ALTO-style threshold baseline**, not as a faithful reproduction of ALTOCUMULUS, unless a mechanism-by-mechanism validation supports stronger wording.

`paper/refs.bib` currently contains placeholder debt and is not an evidence source. No entry from it may enter a draft without independent bibliographic verification.

### Writing Sequence

Drafting follows the user-approved staged order. Each phase stops for human review.

1. Phase 1: Claim-Evidence Map and outline - this plan and matrix.
2. Phase 2: Introduction.
3. Phase 3: Background, Motivation, and Related Work content; final placement of Related Work remains Section 10.
4. Phase 4: Problem Formulation.
5. Phase 5: RescueSched Design.
6. Phase 6: Implementation.
7. Phase 7: Evaluation Methodology.
8. Phase 8: Corrected Simulation Results.
9. Phase 9: Physical Runtime and CloudLab Results; use status/plan/placeholders if evidence is still absent.
10. Phase 10: Discussion, Limitations, and Threats to Validity.
11. Phase 11: Conclusion.
12. Phase 12: Abstract, written only after all supported results and limitations are frozen.
13. Phase 13: Full-paper consistency and reviewer attack-surface audit.

The existing `paper/main.tex` and section files are not authoritative drafts. They contain legacy title/result/target-safety/reference debt and must not be copied forward without claim-by-claim revalidation. Phase work must be drafted in new reviewable files or patches and must not directly overwrite `paper/main.tex` unless the user later authorizes integration.

### Submission Countdown

The official source currently confirms only the date 2026-07-31. The schedule below uses the project's internal hard deadline of **2026-07-30 12:00 PDT**, which is a risk-control decision rather than an official INFOCOM deadline.

| Date | Milestone | Required auditable output | Decision condition |
| --- | --- | --- | --- |
| 2026-07-15 | Freeze Phase-1 claim boundary and outline | `WRITING_PLAN.md`; `CLAIM_EVIDENCE_MATRIX.md` | No prose proceeds until human approval |
| 2026-07-16 | Complete ALTOCUMULUS and closest-work audit | Verified bibliography; mechanism comparison notes | Any unsupported novelty language remains blocked |
| 2026-07-17 | Freeze authorship/COI/prior-publication/AI-use log; recheck official rules | Administrative manifest; official-rule diff | Preserve an anonymous-draft path until anonymity is confirmed |
| 2026-07-18 | Minimum physical trace loader and client/server path | Build/test log; one end-to-end trace | `[IMPLEMENTATION DETAIL REQUIRED]` remains if absent |
| 2026-07-19 | Implement and instrument real descriptor handoff | Raw local/cross-core measurements; event schema | No physical overhead claim without this evidence |
| 2026-07-20 | Four-policy physical smoke test and fairness audit | Shared-runtime smoke logs; policy/resource checklist; official-rule recheck | All policies must share the same runtime and trace semantics |
| 2026-07-21 | W3 `rho=0.85` physical pilot and cost calibration | Pilot manifest; raw logs; simulator calibration inputs | Stop broad physical claims if direction reverses |
| 2026-07-22 | Start frozen physical anchor matrix | Paired run map for W3 and W2 anchors | No tuning on final repetitions |
| 2026-07-23 | Main-track readiness go/no-go | Explicit decision record | Without credible physical evidence, narrow to simulation/boundary study or reconsider venue/track |
| 2026-07-24 | Freeze statistics, tail-sample audit, and negative results | Physical summaries/CIs if available; W2 and low-load tables | Preserve all preregistered negative anchors |
| 2026-07-25 | Technical content freeze | Every number mapped to a CSV row; figure/table manifest | No orphan numeric claim |
| 2026-07-26 | Freeze related work, limitations, ethics, AI, and artifact text | Complete anonymous draft v1 | All citations verified; no placeholder references presented as final |
| 2026-07-27 | Internal reviewer round and official-rule audit | Severity-ranked issue list; compliance diff | Resolve fatal claim/evidence and formatting issues first |
| 2026-07-28 | Experiment lock and clean reproduction archive | Immutable release candidate; checksums; commands | No post-hoc unregistered result substitution |
| 2026-07-29 | PDF, metadata, anonymity, author, and COI audit | Final candidate PDF; submission-system draft | Two-person verification recommended |
| 2026-07-30 12:00 PDT | Internal hard deadline: upload and download-verify | Receipt, downloaded PDF checksum, submission status | Main upload complete before emergency window |
| 2026-07-31 | Official date; emergency only | Correct only unforeseen submission failures | No new experiments or first-time upload planned |

## 5. Claim-Evidence Table

The complete matrix is in `paper/CLAIM_EVIDENCE_MATRIX.md`. The table below lists the claims that determine the paper's viability.

| Claim ID | Claim text | Evidence source | Evidence status | Allowed wording | Risk |
| --- | --- | --- | --- | --- | --- |
| C-MECH-01 | RescueSched requires a predicted local miss and predicted remote meet for a queued descriptor | `src/core/simulator.cpp`; `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md`; unit tests for movement path | `SUPPORTED IN SIMULATION ONLY` | "The simulator moves a queued descriptor only when its estimated local completion exceeds its deadline and its estimated remote completion, including handoff and epsilon, fits the deadline." | Medium: estimator/model error and no physical runtime |
| C-EST-01 | The primary estimator is method-keyed EWMA and does not inspect current hidden service time | `src/core/simulator.cpp`; `tests/unit/simulator_validity_tests.cpp`; Step-21 raw CSV configuration | `SUPPORTED IN SIMULATION ONLY` | "The simulator's deployable-estimator configuration keys an EWMA by observable RPC method and updates it after completion." | Low/medium: real runtime observability still unimplemented |
| C-FAIR-01 | Compared methods share a trace and pay descriptor handoff cost | Step-21 raw CSV trace hashes; validator; movement code; baseline handoff test | `SUPPORTED IN SIMULATION ONLY` | "At each workload/rho/seed point, all policies consume the same immutable trace; all successful descriptor moves pay the configured simulated handoff delay." | Medium: scheduler CPU work is instrumented but not physically cost-equalized |
| C-W3-085 | W3 `rho=0.85` passes against both strong baselines with less moved work | `paired_comparisons.csv`, keys `(W3,0.85,L1_WorkStealingPolling)` and `(W3,0.85,M0_AltoThreshold)` | `SUPPORTED IN SIMULATION ONLY` | "Across ten frozen seeds, RescueSched reduces deadline violations by 15.29% versus polling work stealing and 15.30% versus ALTO-style migration, while moving 69.98% and 3.19% less work, respectively." | Medium/high: simulation-only and unconditional tails are higher |
| C-W3-090 | W3 `rho=0.90` passes against both strong baselines with less moved work | `paired_comparisons.csv`, keys `(W3,0.90,L1_WorkStealingPolling)` and `(W3,0.90,M0_AltoThreshold)` | `SUPPORTED IN SIMULATION ONLY` | "Across ten frozen seeds, RescueSched reduces deadline violations by 17.22% and 16.79% while moving 62.68% and 10.46% less work than the two baselines." | Medium/high: simulation-only and unconditional tails are higher |
| C-W3-070 | W3 `rho=0.70` is a negative boundary | `paired_comparisons.csv`; `summary.csv`, W3 `rho=0.70` rows | `SUPPORTED IN SIMULATION ONLY` | "At W3 `rho=0.70`, RescueSched is about 124.89% worse in deadline-miss rate than polling work stealing; it also moves about 0.98% more work than the ALTO-style baseline." | Low if disclosed; critical if hidden |
| C-W2-085 | W2 `rho=0.85` trades fewer deadline violations for severe P99/P999 regressions | `paired_comparisons.csv`; `summary.csv`, W2 `rho=0.85` rows | `SUPPORTED IN SIMULATION ONLY` | "RescueSched reduces deadline violations by 12.58%-15.36%, but its median P99 is 6.56x-8.55x and P999 is 4.68x-8.81x those of the strong baselines." | Critical to any tail/general-purpose claim |
| C-LIM-01 | Current evidence does not establish physical deployment or production readiness | Readiness, physical plan, runbook, absence of runtime/result artifact | `SUPPORTED STATUS CLAIM` | "The current evidence is simulation-only; a real RPC runtime and paired physical results remain future work." | Critical if omitted or softened |
| C-SAFE-01 | The implementation does not prove no incremental target-side harm | `src/core/simulator.cpp`; `docs/INFOCOM_READINESS.md`; `docs/MODULE_SPEC.md` | `SUPPORTED LIMITATION` | "Target selection uses a bounded predicted-risk filter and reservations; it does not provide a causal no-harm guarantee for existing target requests." | High: current design language can overstate semantics |
| C-RW-01 | RescueSched differs from closest prior RPC migration mechanisms in request-specific outcome-change selection | ALTOCUMULUS full text and other formal literature | `CITATION REQUIRED` | Use only after a mechanism matrix verifies the distinction | Critical novelty risk |

## 6. Missing Evidence

1. `[IMPLEMENTATION DETAIL REQUIRED]` A real single-host multicore RPC/request runtime with pinned workers, RSS or equivalent fixed per-core dispatch, non-preemptive FIFO queues, and legal queued-descriptor movement.
2. `[IMPLEMENTATION DETAIL REQUIRED]` A frozen trace replay or paired workload source shared by all four physical policies.
3. `[IMPLEMENTATION DETAIL REQUIRED]` Physical implementations of `L0_RandomCore`, `L1_WorkStealingPolling`, `M0_AltoThreshold`, and `M1_RescueSched` in one runtime with comparable control budgets.
4. `[PHYSICAL RESULT REQUIRED]` Real descriptor handoff, queue insertion, decision, polling, cache, NUMA, CPU, and packet-processing cost distributions.
5. `[PHYSICAL RESULT REQUIRED]` At least ten paired repetitions for W3 `rho=0.70,0.85,0.90` and W2 `rho=0.85`, with immutable manifests, raw logs, summary CSVs, and confidence intervals.
6. `[PHYSICAL RESULT REQUIRED]` Server-side deadline/goodput results separated from end-to-end RPC latency, loss, timeout, and throughput outcomes.
7. `[PHYSICAL RESULT REQUIRED]` Simulator sensitivity rerun using measured physical handoff distributions rather than only the current scalar `0.5 us` delay.
8. `[IMPLEMENTATION DETAIL REQUIRED]` Either a complete incremental target-risk safeguard and ablation, or final paper text explicitly limited to the current bounded pre-risk filter.
9. `[CITATION REQUIRED]` Full ALTOCUMULUS mechanism audit and verified comparison against other RPC scheduling, RSS, work stealing, and deadline-aware systems.
10. `[CITATION REQUIRED]` Verified BibTeX records; current `paper/refs.bib` placeholders are unusable.
11. INFOCOM 2027 official author instructions covering page count, references, anonymity, template, supplement, submission system, and exact deadline time zone.
12. A final decision on whether the title's "RSS-Sharded RPC Servers" scope remains justified if the physical runtime does not implement or faithfully emulate RSS queue placement.

## 7. Reviewer Attack Surface

1. **Simulation-only systems claim:** Why should an INFOCOM systems reviewer accept a scheduler whose current evidence omits a real NIC/RSS path, worker runtime, cache/NUMA effects, and transport processing?
2. **Closest-prior-work overlap:** Is the local-miss/remote-meet rule materially different from ALTOCUMULUS or other SLO-aware queued-RPC migration once their actual target-selection and deadline semantics are examined?
3. **Baseline fidelity:** Why is `M0_AltoThreshold` an appropriate strong baseline, and could labeling it "ALTO-style" still imply more fidelity to ALTOCUMULUS than the implementation supports?
4. **Low-load reversal:** What mechanism causes RescueSched to be approximately 124.89% worse than polling work stealing at W3 `rho=0.70`, and can the policy identify that regime online without post-hoc tuning?
5. **Tail harm:** Why should a system be useful when W2 `rho=0.85` reduces deadline misses but raises median P99/P999 by multiples? Which requests improve, which requests are delayed, and is goodput being purchased by starving non-critical traffic?
6. **Target-side safety gap:** The implementation checks bounded pre-existing predicted risk but does not calculate the full incremental effect on existing target requests. Which design claims survive this gap?
7. **Estimator validity:** How sensitive are results to EWMA cold start, method granularity, nonstationarity, and prediction error? Does the two-method model hide practical classification difficulty?
8. **Control overhead fairness:** Although all moves pay the same simulated handoff delay, work stealing and push-based policies execute different numbers and types of control operations. Where are CPU-cycle and polling-cost comparisons?
9. **Statistical protocol drift:** The evaluation contract describes a holdout gate, while the Step-21 analysis pools ten development and holdout seeds. Was the final test altered after observing development results, and how is multiplicity handled across workload/rho points?
10. **Metric semantics:** Are the reported P99/P999 values server-side completion latency, queueing plus service, or end-to-end RPC latency? How are timeouts, drops, and unfinished requests treated?
11. **Workload realism:** Do W1/W2/W3 and method-derived deadlines reflect real RPC services, correlations, fan-out, flow affinity, and burst structure?
12. **Title-to-evidence mismatch:** Does the paper demonstrate behavior under actual RSS-sharded packet processing, or only random initial core assignment in a simulator?

## 8. Revision Checklist

- [ ] Human reviewer approves the one-sentence problem statement and hypothesis without broadening the evidence boundary.
- [ ] Human reviewer approves no more than three contribution candidates.
- [ ] Every planned numeric statement maps to a keyed row in Step-21 `summary.csv` or `paired_comparisons.csv`.
- [ ] W3 `rho=0.70` and W2 `rho=0.85` remain mandatory in the main paper, not only an appendix or artifact.
- [ ] W3 passing-point P99/P999 increases are disclosed wherever a reader could infer general latency improvement.
- [ ] "Deadline violation," "SLO goodput," "P99," and "P999" remain separate metrics throughout.
- [ ] "Server-side completion latency" is never silently renamed "end-to-end RPC latency."
- [ ] "Simulated handoff delay" is never called measured physical migration overhead.
- [ ] Target selection is not described as a causal no-harm or no-incremental-risk guarantee.
- [ ] BMR/UMR remain explicitly counterfactual simulator diagnostics.
- [ ] Step-20/20b are labeled directional only; Step 00-18, AQB, and DQB are absent from the evidence chain.
- [ ] The final evaluation wording says "ten frozen seeds" or "corrected ten-seed full gate," not "holdout-only gate."
- [ ] `M0_AltoThreshold` is called an ALTO-style baseline, not ALTOCUMULUS itself.
- [ ] All related-work mechanism statements have verified formal citations; unresolved items retain `[CITATION REQUIRED]`.
- [ ] All physical paragraphs retain `[IMPLEMENTATION DETAIL REQUIRED]` and `[PHYSICAL RESULT REQUIRED]` until raw, versioned evidence exists.
- [ ] The provisional page budget is revised when official INFOCOM 2027 author rules are available.
- [ ] Existing `paper/main.tex`, section files, `paper/README.md`, and `paper/refs.bib` are audited rather than inherited.
- [ ] Abstract drafting remains blocked until Phase 12.

**Phase-1 stop condition:** after this plan and `paper/CLAIM_EVIDENCE_MATRIX.md` are reviewed, wait for explicit human approval before drafting the Introduction.
