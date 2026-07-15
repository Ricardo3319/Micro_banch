# RescueSched Claim-Evidence Matrix

**Working title:** *RescueSched: Deadline-Feasible Descriptor Migration for RSS-Sharded RPC Servers*
**Phase:** 1 - Claim-Evidence Map
**Audit date:** 2026-07-15
**Companion plan:** `paper/WRITING_PLAN.md`

## 1. Section Goal

This matrix is the claim authority for later paper phases. Its purpose is to prevent a statement from entering the paper at a strength greater than its evidence permits.

Each candidate claim is classified by:

- claim strength;
- exact evidence source;
- current evidence status;
- eligibility for the Abstract and Introduction;
- dependency on physical evidence;
- corresponding limitation; and
- likely reviewer attack.

The matrix covers current candidates, planned claims, citation-dependent claims, and explicitly prohibited claims. It does not authorize drafting the Introduction in Phase 1.

## 2. Status and Strength Vocabulary

### Evidence status

| Status | Meaning |
| --- | --- |
| `SUPPORTED` | Directly established by current code, tests, configuration, or auditable artifact for the stated scope |
| `SUPPORTED IN SIMULATION ONLY` | Established only in the corrected discrete-event simulator; wording must name or make the simulation scope unambiguous |
| `DIRECTIONAL ONLY` | Supported by Step-20/20b pilot evidence but not eligible as a final quantitative paper result |
| `IMPLEMENTED BUT NOT A PAPER CLAIM` | Code or diagnostic exists, but its semantics are not appropriate for a main paper conclusion |
| `SUPPORTED STATUS CLAIM` | The repository state directly supports a statement about what is or is not implemented/evidenced |
| `SUPPORTED LIMITATION` | Direct evidence establishes a boundary, failure case, or validity limitation |
| `PLANNED` | Defined by a plan or runbook but not implemented or measured |
| `CITATION REQUIRED` | Needs a verified formal source before use |
| `PROHIBITED` | Contradicted by current evidence, outside the evidence boundary, or too strong to use |

### Claim strength

| Strength | Meaning |
| --- | --- |
| `DEFINITIONAL` | A scoped definition or model assumption |
| `IMPLEMENTATION FACT` | A directly inspectable behavior of the current simulator/code |
| `VALIDATED INVARIANT` | An implementation fact covered by a focused test or schema check |
| `QUANTITATIVE RESULT` | A result tied to authoritative CSV rows and statistical analysis |
| `NEGATIVE RESULT` | A supported counterexample or regression that bounds the claim |
| `LIMITATION` | A statement restricting external validity, causality, or implementation maturity |
| `HYPOTHESIS` | A testable statement that is not itself a completed result |
| `POSITIONING` | A comparison with prior work that requires formal literature evidence |
| `PHYSICAL RESULT` | A real-machine outcome; none is currently supported |
| `UNSUPPORTED GENERALIZATION` | Language exceeding current evidence and therefore prohibited |

### Abstract and Introduction codes

- `YES`: may appear with the allowed wording and scope.
- `CONDITIONAL`: may appear only if the corresponding caveat or negative boundary is also present.
- `NO`: must not appear in that section under current evidence.
- `AFTER EVIDENCE`: blocked until the named physical or citation evidence exists.

## 3. Allowed Claims

Claims marked `SUPPORTED`, `SUPPORTED IN SIMULATION ONLY`, `SUPPORTED STATUS CLAIM`, or `SUPPORTED LIMITATION` may enter later phases only with their allowed wording. Claims marked `PLANNED`, `CITATION REQUIRED`, or `PROHIBITED` are not completed paper findings.

The Abstract remains unwritten until Phase 12. A `YES` in the Abstract column means only that the claim is eligible after the full paper and its limitations are frozen.

## 4. Required Evidence

### Evidence source keys

| Key | Source |
| --- | --- |
| `RULES` | `docs/INFOCOM_2027_SUBMISSION_GUIDE.md` |
| `CONTRACT` | `docs/PAPER_CONTRACT_INFOCOM2027.md` |
| `EVAL-CONTRACT` | `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md` |
| `ARCH` | `docs/ARCHITECTURE.md` |
| `MODULE` | `docs/MODULE_SPEC.md` |
| `PROV` | `docs/ARTIFACT_PROVENANCE.md` |
| `READINESS` | `docs/INFOCOM_READINESS.md` |
| `NEXT` | `docs/NEXT_PHASE_AFTER_CORRECTED_EVAL.md` |
| `PHYS-PLAN` | `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md` |
| `RUNBOOK` | `docs/PHYSICAL_MACHINE_RUNBOOK.md` |
| `MANIFEST` | `artifacts/step-21-corrected-full/manifest.md` |
| `RAW-W1` | `artifacts/step-21-corrected-full/w1.csv` |
| `RAW-W2` | `artifacts/step-21-corrected-full/w2.csv` |
| `RAW-W3` | `artifacts/step-21-corrected-full/w3.csv` |
| `SUMMARY` | `artifacts/step-21-corrected-full/summary.csv` |
| `PAIRED` | `artifacts/step-21-corrected-full/paired_comparisons.csv` |
| `GATE` | `artifacts/step-21-corrected-full/go_no_go.md` |
| `RUN-SCRIPT` | `scripts/run_corrected_eval.sh` |
| `ANALYSIS` | `scripts/corrected_eval_analysis.py` |
| `SIM` | `src/core/simulator.cpp` |
| `TRACE` | `src/core/workload_trace.cpp`; `include/sim/workloads/trace.h` |
| `MODEL` | `include/sim/model/task.h`; `include/sim/model/core.h`; `include/sim/common/constants.h` |
| `APP` | `src/app/main.cpp`; `config/rescuesched.yaml` |
| `UNIT` | `tests/unit/simulator_validity_tests.cpp` |
| `SCHEMA-TEST` | `tests/integration/validate_rescue_csv_schema.py` |
| `ALTO-BIB` | Jiechen Zhao et al., "ALTOCUMULUS: Scalable Scheduling for Nanosecond-Scale Remote Procedure Calls," MICRO 2022, DOI `10.1109/MICRO56248.2022.00040` |

### Statistical scope

- Step-21 uses ten frozen seeds: `11,23,37,47,59,71,83,97,109,127`.
- Each run contains 200,000 warmup and 1,000,000 measurement requests.
- `SUMMARY` reports medians across seeds.
- `PAIRED` reports the mean paired RescueSched-minus-baseline miss reduction, deterministic 10,000-draw paired-bootstrap 95% intervals, relative miss reduction, moved-work reduction, and P99/P999 median ratios.
- A positive `mean_slo_reduction` means fewer deadline violations under RescueSched.
- The final analysis pools all ten full-tier seeds. Allowed wording is "corrected ten-seed full gate" or "paired intervals across ten frozen seeds," not "holdout-only gate."

## 5. Draft Claim-Evidence Matrix

### 5.1 Problem, Scope, and Model

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-PROB-01 | The paper studies transient per-core FIFO imbalance under fixed RSS-style/random initial request placement. | `DEFINITIONAL` | `CONTRACT`; `ARCH`; `TRACE`; `SIM` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No for the model; yes for claims about deployed RSS prevalence | Current evidence models random/fixed initial-core placement; it does not observe a real NIC/RSS data path. | High | "We study transient imbalance among non-preemptive per-core FIFO queues under fixed random initial-core placement, as an RSS-sharded server model." |
| C-PROB-02 | Actual RSS-sharded RPC servers exhibit the modeled rescue opportunities often enough to matter. | `HYPOTHESIS` | Formal RSS/RPC literature; physical queue traces | `CITATION REQUIRED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | Yes | No physical traces currently establish frequency, duration, or causes of rescue windows. | Critical | `[CITATION REQUIRED] [PHYSICAL RESULT REQUIRED]` |
| C-MODEL-01 | The primary paper path is a single host with 16 homogeneous worker cores. | `IMPLEMENTATION FACT` | `MODEL` (`CORES_PER_HOST=16`); `SIM` (`active_host_count_=1` for paper methods); `ARCH` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | Single-host homogeneous simulation omits multi-socket and heterogeneous runtime effects. | Medium | "Our corrected simulator models one host with 16 homogeneous worker cores." |
| C-MODEL-02 | Each core executes requests non-preemptively in FIFO order. | `IMPLEMENTATION FACT` | `ARCH`; `MODEL`; `SIM::start_execution` and FIFO queue operations | `SUPPORTED IN SIMULATION ONLY` | `NO` | `YES` | No | Runtime queue discipline is not physically implemented. | Low/medium | "Each modeled worker owns a non-preemptive FIFO queue." |
| C-MODEL-03 | Only queued-but-not-running request descriptors are eligible for movement. | `IMPLEMENTATION FACT` | `SIM::run_rescue_sched_check`; `SIM::move_rescue_task_intra_host`; `MODEL`; `MODULE` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No | Legality and synchronization of descriptor movement remain unvalidated in a real transport/runtime. | Medium | "RescueSched considers only descriptors in a source wait queue; it never migrates the running request." |
| C-MODEL-04 | Deadlines are explicit method-dependent server-side budgets: 40 us for short and 200 us for long RPC methods in the corrected model. | `IMPLEMENTATION FACT` | `MODEL`; `TRACE`; `UNIT`; raw CSV `deadline_model=server-side-method-budget` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | These are modeled server-side budgets, not measured end-to-end SLOs. | Medium | "The simulator assigns explicit 40-us and 200-us server-side budgets to its short and long method classes." |
| C-MODEL-05 | The reported latency is generation-to-simulator-completion time and must not be called end-to-end RPC latency. | `LIMITATION` | `SIM::handle_task_finish`; `MODULE`; `READINESS`; raw CSV deadline model | `SUPPORTED LIMITATION` | `NO` | `CONDITIONAL` | No | Network RTT, transport serialization, packet processing, drops, and timeouts are outside the simulator. | Critical if mislabeled | "We report modeled server-side completion latency, not end-to-end RPC latency." |
| C-MODEL-06 | Offered load is calibrated from actual generated service plus modeled host overhead and effective core capacity. | `VALIDATED INVARIANT` | `TRACE`; `UNIT::test_load_calibration`; raw CSV `offered_load_definition=actual-service-plus-host` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | Physical offered load must be recalibrated from measured service and overhead. | Low/medium | "The trace generator calibrates rho using generated service work plus modeled host overhead." |
| C-METRIC-01 | Deadline violation rate is the primary outcome; SLO goodput, unconditional P99, and unconditional P999 are distinct metrics. | `DEFINITIONAL` | `CONTRACT`; `EVAL-CONTRACT`; metrics implementation; `SUMMARY` | `SUPPORTED` | `YES` | `YES` | No | The exact goodput unit must be stated; tails summarize all completed measurement requests. | Medium | "We treat deadline violations as primary and report goodput and unconditional P99/P999 separately." |

### 5.2 RescueSched Mechanism

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-MECH-01 | A normal RescueSched candidate must be predicted to miss on its current core. | `IMPLEMENTATION FACT` | `SIM::run_rescue_sched_check`; `EVAL-CONTRACT`; `CONTRACT` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No | Prediction depends on bounded queue visibility and EWMA estimates. | Low/medium | "RescueSched first filters for a predicted local deadline miss." |
| C-MECH-02 | A normal RescueSched move also requires predicted remote completion plus epsilon to fit the request's deadline. | `IMPLEMENTATION FACT` | `SIM::run_rescue_sched_check`; `MODEL` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No | Feasibility is predicted, not guaranteed; the current request's actual service is hidden from the estimator. | Medium | "It commits only when estimated remote completion, after handoff and a 2-us margin, remains within the deadline." |
| C-MECH-03 | Remote completion prediction accounts for target queued/running work, in-flight reserved work, request estimate, and configured handoff delay. | `IMPLEMENTATION FACT` | `SIM`; `MODEL`; `UNIT::test_measurement_cohort_and_delayed_migration` | `SUPPORTED IN SIMULATION ONLY` | `CONDITIONAL` | `YES` | No | Scalar predictions omit cache, NUMA, packet processing, scheduler CPU time, and physical cost variance. | Medium/high | "The simulator includes current target work, outstanding reservations, estimated request work, and the configured handoff delay in its remote-feasibility test." |
| C-MECH-04 | Candidate and target inspection and committed moves are bounded. | `IMPLEMENTATION FACT` | `MODEL`: scan depth 64, `K=16`, `H=4`, budget 1; `SIM`; Step-21 raw configuration columns | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | A bounded scan is not equivalent to measured low control overhead. | Medium | "The corrected configuration scans at most 64 queued positions per source, keeps at most 16 candidates per source, considers four targets per candidate, and commits at most one move per 1-us check." |
| C-MECH-05 | A paid moved descriptor is removed from the source queue, remains unavailable in flight, and joins the target FIFO only at a migration-arrival event. | `VALIDATED INVARIANT` | `SIM::move_*`; `SIM::handle_task_migration_arrive`; `MODEL`; `UNIT` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | This is simulated event semantics, not a measured runtime synchronization path. | Low/medium | "A committed descriptor is unavailable during the configured simulated handoff and is appended to the target FIFO on arrival." |
| C-MECH-06 | In-flight target reservations are visible to later decisions. | `VALIDATED INVARIANT` | `SIM`; `UNIT` target-reservation check; `ARCH` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | Reservation accuracy depends on service estimates and has not been validated under real concurrency. | Medium | "The simulator reserves estimated incoming work so concurrent checks do not treat an in-flight destination as empty." |
| C-MECH-07 | Default target insertion is append-to-tail. | `IMPLEMENTATION FACT` | `MODEL`; `SIM::handle_task_migration_arrive`; raw Step-21 `target_insert_policy=append_tail` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | Tail insertion contributes to possible rescued-request or target-request delays and must be discussed with W2 results. | Low | "The corrected configuration appends migrated descriptors to the destination FIFO tail." |
| C-MECH-08 | RescueSched guarantees no predicted increase in target-side deadline violations. | `UNSUPPORTED GENERALIZATION` | Contradicted by detailed `SIM` semantics; `READINESS`; `MODULE` | `PROHIBITED` | `NO` | `NO` | A stronger implementation and validation would be required | Current code prioritizes `risk_before==0`; its `delta_risk` does not compute the full incremental effect on existing target requests. | Critical | Do not use. Replace with C-MECH-09 and C-LIM-03. |
| C-MECH-09 | Target selection prioritizes zero-predicted-risk destinations and accounts for reservations. | `IMPLEMENTATION FACT` | `SIM::estimate_risk_before`, target sorting, `strict_target_safe`; reservation code | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | Bounded pre-migration risk is not a causal incremental-harm guarantee. | High | "Within its bounded scan, RescueSched prioritizes targets with no predicted pre-existing misses and includes in-flight reservations." |
| C-MECH-10 | RescueSched is low-overhead or deployable on commodity RPC servers. | `UNSUPPORTED GENERALIZATION` | No real runtime or physical cost artifact; `READINESS`; `PHYS-PLAN` | `PROHIBITED` | `NO` | `NO` | Yes | Bounded algorithmic work alone does not establish deployability or acceptable CPU/cache/NUMA cost. | Critical | `[IMPLEMENTATION DETAIL REQUIRED] [PHYSICAL RESULT REQUIRED]` |

### 5.3 Estimator and Observability

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-EST-01 | The primary corrected configuration uses method-keyed EWMA service estimates. | `IMPLEMENTATION FACT` | `SIM`; `MODEL`; raw Step-21 `service_estimate_mode=ewma`, `service_estimate_ewma_alpha=0.05` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No | Only two observable method buckets are modeled. | Low/medium | "The primary policy uses separate short- and long-method EWMAs with alpha 0.05." |
| C-EST-02 | The EWMA estimate for the current request does not depend on that request's hidden actual service time. | `VALIDATED INVARIANT` | `SIM::estimate_service_time`; `UNIT::test_estimator_does_not_read_hidden_service` | `SUPPORTED` | `YES` | `YES` | No | This is a simulator invariant; a real runtime must expose an equivalent method key and completion history. | Low | "The estimator is keyed only by the observable RPC method; changing the current hidden service time does not change its estimate." |
| C-EST-03 | EWMA state is updated after request completion. | `IMPLEMENTATION FACT` | `SIM::handle_task_finish`; `SIM::update_service_estimator` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | Completion-only updates can lag phase changes and are not yet evaluated under method drift. | Medium | "The estimator updates from completed requests after their service time becomes observable." |
| C-EST-04 | Oracle estimation establishes deployable performance. | `UNSUPPORTED GENERALIZATION` | `CONTRACT`; `MODULE`; primary Step-21 mode is EWMA | `PROHIBITED` | `NO` | `NO` | No | Oracle modes are diagnostic upper bounds and cannot support deployability. | High | Do not use. |
| C-EST-05 | EWMA remains accurate under production workload drift and fine-grained RPC methods. | `PHYSICAL RESULT` | None | `PLANNED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | Yes | No estimator-error or concept-drift physical study exists. | High | `[PHYSICAL RESULT REQUIRED]` |

### 5.4 Comparison Fairness and Provenance

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-FAIR-01 | At each workload/rho/seed point, all four primary methods consume one immutable versioned trace. | `VALIDATED INVARIANT` | `TRACE`; `APP::make_shared_trace`; raw trace hashes; `UNIT`; `SCHEMA-TEST` | `SUPPORTED` | `CONDITIONAL` | `YES` | No | This is a simulator invariant; shared traces do not equal identical physical executions because runtime interference can differ. | Low | "All four policies consume the same `rescuesched-trace-v2` object at each workload/rho/seed point, verified by SHA-256 identity." |
| C-FAIR-02 | Polling work stealing, ALTO-style threshold migration, and RescueSched all pay the same configured simulated descriptor-handoff delay on successful moves. | `VALIDATED INVARIANT` | `SIM`; `UNIT::test_strong_baselines_pay_common_handoff`; raw Step-21 `migration_cost_us=0.5` | `SUPPORTED IN SIMULATION ONLY` | `CONDITIONAL` | `YES` | No | The scalar delay is configured, not a measured physical migration cost; control-plane CPU operations differ by policy. | Medium | "Every successful descriptor move by the two strong baselines and RescueSched incurs the same 0.5-us simulated handoff delay." |
| C-FAIR-03 | RescueSched and ALTO-style migration share the same periodic check, scan, target, and move bounds. | `IMPLEMENTATION FACT` | `EVAL-CONTRACT`; `MODEL`; raw Step-21 configuration | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | Work stealing is a pull policy with different attempt opportunities; exact CPU work is not equalized. | Medium | "The two push-based policies use the same 1-us check period and bounded search/move parameters." |
| C-FAIR-04 | Polling work stealing is a strong paid baseline, allowed to poll each eligible idle core every 1 us. | `IMPLEMENTATION FACT` | `EVAL-CONTRACT`; `MODEL`; `SIM`; raw Step-21 scheduler attempt and poll fields | `SUPPORTED IN SIMULATION ONLY` | `NO` | `YES` | No | Physical polling cost and cache traffic are not measured. | Medium | "Our polling work-stealing baseline performs periodic idle-core pull and pays the common handoff delay on a successful steal." |
| C-FAIR-05 | `M0_AltoThreshold` is ALTOCUMULUS. | `POSITIONING` | No fidelity validation; `EVAL-CONTRACT` defines only an ALTO-style threshold policy | `PROHIBITED` | `NO` | `NO` | No | The local baseline is not established as a reproduction of the MICRO 2022 system. | Critical | Never call it "ALTO" without "-style threshold baseline" and a definition. |
| C-FAIR-06 | Warmup requests are excluded and all one million measurement requests drain into the metric denominator. | `VALIDATED INVARIANT` | `UNIT`; `SCHEMA-TEST`; raw CSV cohort columns and counts | `SUPPORTED` | `NO` | `NO` | No | The simulator models no timeout/drop censoring; physical runs must define these cases. | Low | "Each run excludes 200,000 warmup requests and drains all 1,000,000 measurement requests before reporting metrics." |
| C-FAIR-07 | P99/P999 are exact percentiles over completed measurement requests and are not clipped above 10 ms. | `VALIDATED INVARIANT` | histogram implementation; `UNIT::test_exact_percentile_above_ten_ms`; raw sample counts | `SUPPORTED` | `NO` | `NO` | No | Tail stability still depends on workload realism and seed count. | Low | "P99 and P999 are exact order-statistic percentiles over the completed measurement cohort." |
| C-PROV-01 | Step-21 is the authoritative simulation artifact; Step-20/20b are directional only and Step 00-18/AQB/DQB are excluded. | `SUPPORTED STATUS CLAIM` | `PROV`; `MANIFEST`; `CONTRACT`; `ARCH` | `SUPPORTED` | `NO` | `NO` | No | Existing paper drafts and README material still contain legacy debt and must be audited. | Low if enforced | Use only Step-21 for final quantitative claims. |
| C-PROV-02 | The Step-21 artifact is tied to source revision `ba81d825eaf1e0b6701e21dbb6462c2a801da0b9`, 280 runs, ten seeds, and matching file checksums. | `SUPPORTED STATUS CLAIM` | `MANIFEST`; current SHA-256 check; raw/derived files | `SUPPORTED` | `NO` | `NO` | No | The current worktree is dirty; the artifact identity is the manifest revision and checksums, not current HEAD state. | Low | "The authoritative artifact records revision ..., 280 runs, ten seeds, and checksummed raw and derived CSVs." |
| C-PROV-03 | Step-20 and Step-20b may document the development and holdout-pilot sequence but may not supply final performance numbers. | `LIMITATION` | `PROV`; `artifacts/step-20-corrected-pilot/`; `artifacts/step-20b-corrected-holdout-pilot/` | `DIRECTIONAL ONLY` | `NO` | `NO` | No | Short pilot cohorts are not substitutes for the Step-21 full evaluation. | Low if enforced | "Short development and holdout pilots were used only as directional checks; all reported performance numbers come from Step-21." |
| C-STAT-01 | The reported paired intervals use deterministic 10,000-draw bootstrap resampling across ten frozen seeds. | `VALIDATED INVARIANT` | `ANALYSIS`; `MANIFEST`; `PAIRED` (`paired_seeds=10`) | `SUPPORTED` | `CONDITIONAL` | `NO` | No | Ten seeds provide limited distributional resolution; multiple workload/rho comparisons are not corrected as a family. | Medium | "We report deterministic 10,000-draw paired-bootstrap 95% intervals across ten frozen seeds." |
| C-STAT-02 | The final W3 gate is holdout-only. | `UNSUPPORTED GENERALIZATION` | Contradicted by `RUN-SCRIPT` full seed list and `ANALYSIS` pooling | `PROHIBITED` | `NO` | `NO` | No | The evaluation contract originally describes holdout logic, but the authoritative full analysis combines development and holdout seeds. | Critical | Say "corrected ten-seed full gate," not "holdout-only gate." |

### 5.5 Corrected W3 Simulation Results

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-W3-085-WS | At W3 `rho=0.85`, RescueSched reduces deadline violations versus polling work stealing. | `QUANTITATIVE RESULT` | `PAIRED`: `(W3,0.85,L1_WorkStealingPolling)`: mean reduction `0.0088782`, 95% CI `[0.0085661,0.0091871]`, relative reduction `0.15287446771507926`; `SUMMARY` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation; yes to claim real-system effect | RescueSched's median P99/P999 are higher than this baseline at the same point. | Medium/high | "At W3 `rho=0.85`, RescueSched reduces the deadline-violation rate by 15.29% relative to polling work stealing (paired mean absolute reduction 0.0088782; 95% CI [0.0085661, 0.0091871])." |
| C-W3-085-ALTO | At W3 `rho=0.85`, RescueSched reduces deadline violations versus the ALTO-style threshold baseline. | `QUANTITATIVE RESULT` | `PAIRED`: `(W3,0.85,M0_AltoThreshold)`: mean reduction `0.0088851`, CI `[0.0085203,0.0092604]`, relative `0.1529751041630798`; `SUMMARY` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation; yes to claim real-system effect | The baseline is ALTO-style, not ALTOCUMULUS; tail ratios exceed 1. | Medium/high | "At W3 `rho=0.85`, RescueSched reduces deadline violations by 15.30% relative to the ALTO-style threshold baseline (95% CI for the paired absolute reduction [0.0085203, 0.0092604])." |
| C-W3-085-WORK | At W3 `rho=0.85`, RescueSched moves less work than both strong baselines. | `QUANTITATIVE RESULT` | `PAIRED`: moved-work reductions `0.6997852298261522` and `0.03192261812114128`; `SUMMARY`: RescueSched median moved-work rate `0.19935350036521438` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation | Moved-work rate is estimated service work moved per generated measured work, not CPU overhead or network bytes. | Medium | "It moves 69.98% less estimated work than polling work stealing and 3.19% less than the ALTO-style baseline." |
| C-W3-085-MED | At W3 `rho=0.85`, RescueSched's median deadline violation is `0.049018`, median P99 is `214 us`, and median P999 is `384 us`. | `QUANTITATIVE RESULT` | `SUMMARY`: `(W3,0.85,M1_RescueSched)` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | Medians must not be mixed with the paired mean differences/intervals. | Low | Use in result table/text with explicit "median across ten seeds." |
| C-W3-090-WS | At W3 `rho=0.90`, RescueSched reduces deadline violations versus polling work stealing. | `QUANTITATIVE RESULT` | `PAIRED`: mean `0.0202545`, CI `[0.0196823,0.0208270]`, relative `0.17217460491519415` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation; yes to claim real-system effect | Unconditional P99/P999 are higher under RescueSched. | Medium/high | "At W3 `rho=0.90`, RescueSched reduces deadline violations by 17.22% relative to polling work stealing (95% CI for the paired absolute reduction [0.0196823, 0.0208270])." |
| C-W3-090-ALTO | At W3 `rho=0.90`, RescueSched reduces deadline violations versus the ALTO-style baseline. | `QUANTITATIVE RESULT` | `PAIRED`: mean `0.0196444`, CI `[0.0191315,0.0201015]`, relative `0.16785896169502995` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation; yes to claim real-system effect | Baseline-fidelity and physical-validation caveats remain. | Medium/high | "At W3 `rho=0.90`, RescueSched reduces deadline violations by 16.79% relative to the ALTO-style threshold baseline." |
| C-W3-090-WORK | At W3 `rho=0.90`, RescueSched moves less work than both strong baselines. | `QUANTITATIVE RESULT` | `PAIRED`: reductions `0.6267947004504179` and `0.10455468326750761`; `SUMMARY`: RescueSched median moved-work rate `0.22104668961012286` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation | Moved work is not equivalent to migration count, decision CPU, or physical overhead. | Medium | "It moves 62.68% less estimated work than polling work stealing and 10.46% less than the ALTO-style baseline." |
| C-W3-GATE | W3 `rho=0.85` and `rho=0.90` pass the corrected full simulation gate against both strong baselines without more moved work. | `QUANTITATIVE RESULT` | `GATE`; `PAIRED`; `ANALYSIS` | `SUPPORTED IN SIMULATION ONLY` | `YES` | `YES` | No to report simulation | This is a gate under the frozen simulator/configuration, not proof of general or physical benefit. | Medium | "The corrected ten-seed full simulation gate passes at W3 `rho=0.85` and `rho=0.90`." |
| C-W3-TAIL | RescueSched improves W3 tail latency at the passing points. | `UNSUPPORTED GENERALIZATION` | Contradicted by `PAIRED`: P99/P999 ratios greater than 1 at both points | `PROHIBITED` | `NO` | `NO` | Physical evidence cannot repair the false simulation statement; a separate physical claim could later be tested | Passing is based on deadline violation and moved work, not P99/P999 improvement. | Critical | Do not use. State that unconditional tails are secondary and not improved at these points. |

### 5.6 Required Negative and Boundary Results

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-W3-070-WS | At W3 `rho=0.70`, RescueSched has a higher deadline-miss rate than polling work stealing. | `NEGATIVE RESULT` | `PAIRED`: mean reduction `-0.0116738`, CI `[-0.0117635,-0.0115911]`, relative `-1.2489354873221352`; `SUMMARY`: medians `0.021062` versus `0.0094375` | `SUPPORTED IN SIMULATION ONLY` | `CONDITIONAL` | `YES` | No | The policy is not load-universal and may over-filter useful opportunistic pulls at low load. | Critical if hidden | "At W3 `rho=0.70`, RescueSched is approximately 124.89% worse in deadline-miss rate than polling work stealing." |
| C-W3-070-ALTO | At W3 `rho=0.70`, RescueSched reduces misses versus the ALTO-style baseline but moves more work, so the gate condition fails. | `NEGATIVE RESULT` | `PAIRED`: relative miss reduction `0.11295284312691209`; relative work reduction `-0.00975360834535377`; `no_more_migrated_work=0` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `CONDITIONAL` | No | A small moved-work reversal prevents claiming efficient dominance. | Medium | "Although RescueSched lowers misses versus the ALTO-style baseline at W3 `rho=0.70`, it moves about 0.98% more work and fails the no-more-work gate." |
| C-W2-085-MISS | At W2 `rho=0.85`, RescueSched reduces deadline violations versus both strong baselines. | `QUANTITATIVE RESULT` | `PAIRED`: relative reductions `0.153618457684048` and `0.12577981009049935`; positive paired CIs | `SUPPORTED IN SIMULATION ONLY` | `CONDITIONAL` | `CONDITIONAL` | No | This statement is incomplete without the severe tail regression. | Critical if isolated | "At W2 `rho=0.85`, RescueSched reduces deadline violations by 15.36% versus polling work stealing and 12.58% versus the ALTO-style baseline, but ..." followed immediately by C-W2-085-TAIL. |
| C-W2-085-TAIL | At W2 `rho=0.85`, RescueSched severely worsens unconditional P99/P999. | `NEGATIVE RESULT` | `PAIRED`: P99 ratios `6.555023923444976`, `8.553590010405827`; P999 ratios `4.6843853820598005`, `8.806995627732666`; `SUMMARY`: RescueSched P99 `4110 us`, P999 `7050 us` | `SUPPORTED IN SIMULATION ONLY` | `CONDITIONAL` | `YES` | No | The result blocks any universal tail-latency or general scheduler superiority claim. | Critical | "Its median P99 is 6.56x-8.55x and P999 is 4.68x-8.81x the strong baselines; the RescueSched medians are 4110 us and 7050 us." |
| C-W2-UNIVERSAL | W2 supports a general latency improvement claim. | `UNSUPPORTED GENERALIZATION` | Contradicted by C-W2-085-TAIL | `PROHIBITED` | `NO` | `NO` | No | Deadline-goodput improvement and unconditional tail regression point in opposite directions. | Critical | Do not use. |
| C-W1-SANITY | W1 is a balanced/bimodal sanity workload and may be reported as secondary evidence. | `QUANTITATIVE RESULT` | `RAW-W1`; `SUMMARY`; `PAIRED`; `EVAL-CONTRACT` | `SUPPORTED IN SIMULATION ONLY` | `NO` | `NO` | No | W1 cannot establish workload universality and its detailed tail tradeoffs must not be omitted if discussed. | Medium | "W1 is reported as a secondary sanity workload under the same paired protocol." Any number requires a keyed CSV citation. |
| C-UNIV-01 | RescueSched universally improves deadline violations across workloads and offered loads. | `UNSUPPORTED GENERALIZATION` | Contradicted by W3 `rho=0.70`; W2 tail boundary | `PROHIBITED` | `NO` | `NO` | No | At least one primary operating point reverses versus work stealing. | Critical | Do not use. |
| C-UNIV-02 | RescueSched universally improves P99/P999 tail latency. | `UNSUPPORTED GENERALIZATION` | Contradicted by W2 and W3 paired tail ratios | `PROHIBITED` | `NO` | `NO` | No | Corrected simulation directly falsifies this claim. | Critical | Do not use. |

### 5.7 Diagnostics, Causality, and Limitations

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-DIAG-01 | BMR/UMR counters identify physical migrations that causally helped or harmed requests. | `UNSUPPORTED GENERALIZATION` | `MODULE`; `CONTRACT`; counterfactual simulator fields in `SIM` | `PROHIBITED` | `NO` | `NO` | A credible causal design would be required | Counterfactual local completion is not physically observable and may be model-dependent. | Critical | Do not present BMR/UMR as physical or causal facts. |
| C-DIAG-02 | BMR/UMR may be used as explicitly labeled simulator diagnostics. | `IMPLEMENTED BUT NOT A PAPER CLAIM` | `SIM`; metrics code; `MODULE` | `IMPLEMENTED BUT NOT A PAPER CLAIM` | `NO` | `NO` | No | Diagnostic semantics remain counterfactual and model-dependent. | Medium | "We retain BMR/UMR only as non-causal simulator diagnostics." |
| C-LIM-01 | Current evidence is simulation-only and does not establish real-machine deployment or production readiness. | `LIMITATION` | `READINESS`; `PROV`; `PHYS-PLAN`; `RUNBOOK` | `SUPPORTED STATUS CLAIM` | `CONDITIONAL` | `YES` | No | This limitation materially weakens a main-track systems submission. | Critical | "The present evidence is simulation-only; a real RPC runtime and paired physical evaluation remain incomplete." |
| C-LIM-02 | The simulator's scalar `0.5 us` handoff delay is not measured physical migration overhead. | `LIMITATION` | `MODEL`; raw Step-21 configuration; `READINESS`; `PHYS-PLAN` | `SUPPORTED LIMITATION` | `NO` | `CONDITIONAL` | No | Physical delay has a distribution and includes synchronization/cache/NUMA effects absent from the scalar model. | Critical if mislabeled | "The corrected simulation charges a configured 0.5-us handoff delay; this is not a physical measurement." |
| C-LIM-03 | The current target filter is not a complete incremental target-harm guarantee. | `LIMITATION` | Detailed `SIM` target-risk logic; `READINESS`; `MODULE` | `SUPPORTED LIMITATION` | `NO` | `YES` | No | Existing target requests can experience effects not represented by `risk_before==0` plus incoming-request feasibility. | High | "The filter uses bounded predicted pre-risk and reservations, but does not prove that migration cannot increase target-side misses." |
| C-LIM-04 | Single-host simulation cannot establish cache, NUMA, runtime contention, packet-processing, or end-to-end network effects. | `LIMITATION` | `READINESS`; `MODULE`; `PHYS-PLAN` | `SUPPORTED LIMITATION` | `NO` | `CONDITIONAL` | No | External validity remains untested. | High | Use in Discussion and Physical Evaluation status. |
| C-LIM-05 | The evidence supports a narrow W3 moderate/high-load deadline-violation claim, not a general scheduler superiority claim. | `LIMITATION` | `CONTRACT`; `READINESS`; W3/W2 results | `SUPPORTED LIMITATION` | `YES` | `YES` | No | Benefit depends on workload and load, and unconditional tails can regress. | Low if explicit | "Our supported claim is limited to server-side deadline violations at W3 `rho=0.85` and `rho=0.90` in corrected simulation." |

### 5.8 Physical Runtime and CloudLab Claims

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-PHYS-01 | The repository contains a real multicore RPC runtime implementing RescueSched. | `PHYSICAL RESULT` | `READINESS`; `PHYS-PLAN`; `RUNBOOK` state the opposite | `PROHIBITED` | `NO` | `NO` | Yes | Current repository contains a simulator and process-internal preflight/microbenchmark support, not a real RPC runtime. | Critical | `[IMPLEMENTATION DETAIL REQUIRED]` |
| C-PHYS-02 | A physical trace replay loader exists. | `PHYSICAL RESULT` | `PHYS-PLAN` states it is absent | `PROHIBITED` | `NO` | `NO` | Yes | Paired physical workload identity is not yet implemented. | Critical | `[IMPLEMENTATION DETAIL REQUIRED]` |
| C-PHYS-03 | Real descriptor handoff overhead has been measured. | `PHYSICAL RESULT` | No authoritative physical result artifact | `PROHIBITED` | `NO` | `NO` | Yes | Built-in condition-variable microbenchmark is an initial preflight upper bound, not the final runtime handoff path. | Critical | `[PHYSICAL RESULT REQUIRED]` |
| C-PHYS-04 | RescueSched's W3 benefit reproduces on CloudLab or bare metal. | `PHYSICAL RESULT` | `PHYS-PLAN`; no results | `PLANNED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | Yes | Direction may reverse after real control, cache, NUMA, and packet-processing costs. | Critical | `[PHYSICAL RESULT REQUIRED]` |
| C-PHYS-05 | Physical W2 runs establish whether the tail regression persists. | `PHYSICAL RESULT` | `NEXT`; `PHYS-PLAN`; no results | `PLANNED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | Yes | The simulator tail regression may change in magnitude or cause under a real runtime. | High | `[PHYSICAL RESULT REQUIRED]` |
| C-PHYS-06 | Physical evaluation will use four anchors: W3 `rho=0.70,0.85,0.90` and W2 `rho=0.85`, with at least ten paired repetitions. | `HYPOTHESIS` / plan | `NEXT`; `READINESS`; `PHYS-PLAN` | `PLANNED` | `NO` | `NO` | Yes for results | Plans can change only through a dated, justified protocol revision. | Medium | "We plan ..." only; never past tense. |
| C-PHYS-07 | The physical setup uses a particular CPU, NIC, CloudLab node type, kernel, governor, or NUMA topology. | `PHYSICAL RESULT` | Runbook contains recommendations only | `PLANNED` | `NO` | `NO` | Yes | Recommended hardware is not actual hardware. | High | `[PHYSICAL RESULT REQUIRED]`; report only from frozen run manifests. |
| C-PHYS-08 | RescueSched is production-ready or ready for deployment. | `UNSUPPORTED GENERALIZATION` | Contradicted by `READINESS` and missing runtime/results | `PROHIBITED` | `NO` | `NO` | Yes | Reliability, compatibility, security, transport semantics, and operational cost are untested. | Critical | Do not use. |

### 5.9 Related Work and Novelty Positioning

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-RW-01 | ALTOCUMULUS is a MICRO 2022 paper titled "ALTOCUMULUS: Scalable Scheduling for Nanosecond-Scale Remote Procedure Calls." | `POSITIONING` | `ALTO-BIB` | `SUPPORTED` | `NO` | `CONDITIONAL` | No | Bibliographic verification does not establish any detailed mechanism comparison. | Low | Cite only after adding verified BibTeX; DOI `10.1109/MICRO56248.2022.00040`. |
| C-RW-02 | ALTOCUMULUS is the closest prior system and already covers proactive SLO-aware queued-RPC descriptor migration. | `POSITIONING` | Full ALTOCUMULUS paper required | `CITATION REQUIRED` | `NO` | `AFTER EVIDENCE` | No | Current repository documents assert this positioning, but mechanism details have not been independently audited from the paper text. | Critical | `[CITATION REQUIRED]` |
| C-RW-03 | RescueSched is the first system to use request-specific local-miss/remote-meet selection. | `POSITIONING` | Exhaustive literature evidence absent | `PROHIBITED` | `NO` | `NO` | No | Priority claims require a broader literature search and physical/system implementation evidence. | Critical | Do not use "first" or "novel" as a factual priority claim. |
| C-RW-04 | Prior work does not account for remote deadline feasibility after handoff. | `POSITIONING` | ALTOCUMULUS and other formal sources | `CITATION REQUIRED` | `NO` | `AFTER EVIDENCE` | No | A false contrast would collapse the novelty argument. | Critical | `[CITATION REQUIRED]`; phrase mechanism-by-mechanism, not categorically. |
| C-RW-05 | Generic work stealing repairs idle capacity but does not condition each move on a predicted deadline outcome change. | `POSITIONING` | Formal work-stealing/RPC scheduler references plus local baseline definition | `CITATION REQUIRED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | No | Work-stealing variants may incorporate priorities or deadlines. | High | `[CITATION REQUIRED]`; scope the contrast to the implemented polling baseline if literature is mixed. |
| C-RW-06 | RSS and flow-affinity mechanisms can produce persistent per-core queue imbalance for variable-service RPCs. | `POSITIONING` | Formal RSS/per-core RPC literature; preferably physical traces | `CITATION REQUIRED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | Preferably | Prevalence, persistence, and severity are not established by the simulator alone. | High | `[CITATION REQUIRED] [PHYSICAL RESULT REQUIRED]` for empirical frequency. |
| C-RW-07 | The defensible positioning is request-specific outcome-change selection, not the existence of queued RPC migration. | `POSITIONING` | `CONTRACT`; local mechanism; full prior-work matrix | `CITATION REQUIRED` | `AFTER EVIDENCE` | `AFTER EVIDENCE` | No | This distinction remains vulnerable until closest systems are read in full. | Critical | "After auditing the closest systems, we position RescueSched around request-specific local-miss/remote-meet selection; we do not claim that queued RPC migration itself is new." |

### 5.10 Submission, Draft, and Artifact Hygiene Claims

| Claim ID | Claim text | Claim strength | Evidence source | Current status | Abstract allowed? | Introduction allowed? | Must wait for physical evidence? | Corresponding limitation | Reviewer risk | Allowed wording |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-SUB-01 | INFOCOM 2027's technical-paper deadline date is 2026-07-31. | `SUPPORTED STATUS CLAIM` | `RULES` official-source audit | `SUPPORTED` | `NO` | `NO` | No | Only the date is supported; exact time, time zone, and abstract/full-paper semantics remain unresolved. | Medium administrative | Use only in project planning, not paper prose. |
| C-SUB-02 | INFOCOM 2027 permits ten body pages, uses a particular anonymity mode, or excludes references from the limit. | `UNSUPPORTED GENERALIZATION` | `RULES` marks these unresolved | `PROHIBITED` | `NO` | `NO` | No | Unsupported format assumptions can cause a desk reject. | Critical administrative | Do not state until official 2027 instructions are captured. |
| C-DRAFT-01 | Existing `paper/main.tex` numerical and target-safety statements are current evidence. | `UNSUPPORTED GENERALIZATION` | Current draft debt versus Step-21/code audit | `PROHIBITED` | `NO` | `NO` | No | The draft contains legacy numbers, stronger safety wording, and a different title. | Critical | Treat existing TeX as non-authoritative until revalidated. |
| C-DRAFT-02 | Existing `paper/refs.bib` entries are valid references. | `UNSUPPORTED GENERALIZATION` | Placeholder bibliography audit | `PROHIBITED` | `NO` | `NO` | No | False or placeholder citations violate scholarly integrity. | Critical | Every bibliography entry requires independent verification. |

## 6. Numerical Evidence Ledger

This ledger identifies the exact Step-21 keys for all currently approved headline numbers. It is not a substitute for reading the CSV during drafting.

| Ledger ID | Workload/rho/baseline or method | Field | Exact value | Source |
| --- | --- | --- | ---: | --- |
| N-01 | W3/0.85/work stealing | `mean_slo_reduction` | `0.0088782` | `PAIRED` |
| N-02 | W3/0.85/work stealing | `ci95_low`, `ci95_high` | `0.0085661`, `0.0091871` | `PAIRED` |
| N-03 | W3/0.85/work stealing | `relative_slo_reduction` | `0.15287446771507926` | `PAIRED` |
| N-04 | W3/0.85/work stealing | `relative_work_reduction` | `0.6997852298261522` | `PAIRED` |
| N-05 | W3/0.85/ALTO-style | `mean_slo_reduction` | `0.008885100000000002` | `PAIRED` |
| N-06 | W3/0.85/ALTO-style | `ci95_low`, `ci95_high` | `0.008520300000000001`, `0.009260400000000002` | `PAIRED` |
| N-07 | W3/0.85/ALTO-style | `relative_slo_reduction` | `0.1529751041630798` | `PAIRED` |
| N-08 | W3/0.85/ALTO-style | `relative_work_reduction` | `0.03192261812114128` | `PAIRED` |
| N-09 | W3/0.85/RescueSched | violation, P99, P999, moved-work medians | `0.049018`, `214`, `384`, `0.19935350036521438` | `SUMMARY` |
| N-10 | W3/0.90/work stealing | mean, CI, relative miss, relative work reduction | `0.020254499999999998`; `[0.0196823,0.020827000000000002]`; `0.17217460491519415`; `0.6267947004504179` | `PAIRED` |
| N-11 | W3/0.90/ALTO-style | mean, CI, relative miss, relative work reduction | `0.0196444`; `[0.0191315,0.0201015]`; `0.16785896169502995`; `0.10455468326750761` | `PAIRED` |
| N-12 | W3/0.70/work stealing | mean, CI, relative miss reduction | `-0.0116738`; `[-0.0117635,-0.0115911]`; `-1.2489354873221352` | `PAIRED` |
| N-13 | W3/0.70/RescueSched and work stealing | violation medians | `0.021061999999999997`, `0.009437500000000001` | `SUMMARY` |
| N-14 | W3/0.70/ALTO-style | relative work reduction | `-0.00975360834535377` | `PAIRED` |
| N-15 | W2/0.85/work stealing | relative miss; P99 ratio; P999 ratio | `0.153618457684048`; `6.555023923444976`; `4.6843853820598005` | `PAIRED` |
| N-16 | W2/0.85/ALTO-style | relative miss; P99 ratio; P999 ratio | `0.12577981009049935`; `8.553590010405827`; `8.806995627732666` | `PAIRED` |
| N-17 | W2/0.85/RescueSched | violation, P99, P999 medians | `0.185138`, `4110`, `7050` | `SUMMARY` |

Rounding policy for prose and captions will be frozen in Phase 7 or Phase 8. Until then, this ledger preserves CSV precision. Percentage prose may round to two decimal places only when the underlying exact value remains linked to this table and the source row.

## 7. Missing Evidence

1. `[CITATION REQUIRED]` Full-text mechanism audit for ALTOCUMULUS and all nearest RPC schedulers.
2. `[CITATION REQUIRED]` Formal RSS, flow-affinity, work-stealing, deadline scheduling, and head-of-line blocking references.
3. `[IMPLEMENTATION DETAIL REQUIRED]` Real RPC/runtime data path, descriptor ownership transfer, transport state, completion callback, buffer lifetime, synchronization, and failure behavior.
4. `[IMPLEMENTATION DETAIL REQUIRED]` Shared physical trace replay and all four policies in one runtime.
5. `[PHYSICAL RESULT REQUIRED]` Frozen hardware/software/NIC/RSS/CPU/NUMA manifests.
6. `[PHYSICAL RESULT REQUIRED]` Real decision, polling, queue handoff, cache, NUMA, CPU-cycle, and end-to-end overhead distributions.
7. `[PHYSICAL RESULT REQUIRED]` Paired physical W3 and W2 anchor outcomes with confidence intervals.
8. `[PHYSICAL RESULT REQUIRED]` Separate server-side and end-to-end RPC latency, plus timeout/drop semantics.
9. `[IMPLEMENTATION DETAIL REQUIRED]` Full incremental target-risk safeguard or a finalized design text that explicitly omits such a guarantee.
10. Official INFOCOM 2027 page, anonymity, reference, supplement, submission-system, and exact-deadline rules.

## 8. Reviewer Attack Surface

1. Does random initial-core assignment adequately represent RSS flow affinity and real receive-side queue behavior, or does the title overstate the modeled system?
2. Is local-miss/remote-meet filtering actually absent from ALTOCUMULUS and other SLO-aware schedulers, or is the novelty margin semantic rather than substantive?
3. Does `M0_AltoThreshold` faithfully represent a competitive threshold scheduler, and which ALTOCUMULUS mechanisms are intentionally omitted?
4. Why does RescueSched lose badly to polling work stealing at W3 `rho=0.70`; can the scheduler detect and disable itself in that regime without tuning on evaluation traces?
5. What causes W2 P99/P999 amplification, and does deadline-goodput improvement merely shift delay onto requests with longer budgets?
6. The target filter does not compute full incremental risk to existing target requests. Can the claimed mechanism still be called deadline-feasible without clarifying that feasibility is for the migrated request only?
7. Are ten seeds sufficient for heavy-tailed behavior, and why does the final gate pool development and holdout seeds after the contract described a holdout test?
8. Does a common scalar handoff delay make policy costs fair when polling frequency, scan work, cache effects, and synchronization differ?
9. How sensitive are results to the 1-us check period, `K=16`, `H=4`, budget 1, epsilon 2 us, EWMA alpha 0.05, and handoff cost 0.5 us?
10. Are the modeled 40-us/200-us method deadlines and W1/W2/W3 service distributions representative of real RPC services?
11. Why should an INFOCOM systems paper be accepted without a real RPC server, actual RSS queues, or CloudLab results?
12. Could the exact-percentile and full-drain methodology understate practical timeout/drop behavior by forcing all simulated requests to complete?
13. Are migrated-work reductions meaningful without physical CPU-cycle, cache, memory-bandwidth, and inter-core synchronization costs?
14. Do the passing W3 points still show higher unconditional P99/P999, and if so, is the paper's headline metric selection sufficiently justified?

## 9. Revision Checklist

- [ ] Human review marks each claim as accepted, narrowed, deferred, or removed.
- [ ] No Phase-2 prose uses a `PLANNED`, `CITATION REQUIRED`, or `PROHIBITED` claim as a completed fact.
- [ ] Abstract candidates remain limited to C-MECH-01/02, C-EST-01/02, C-W3-GATE and its exact passing-point results, C-W3-070-WS, C-W2-085-TAIL, and C-LIM-01/05 as space permits.
- [ ] Introduction includes both the W3 low-load boundary and W2 tail boundary, at least in compact form.
- [ ] Every result percentage preserves the correct denominator and baseline.
- [ ] Every confidence interval is described as an interval for the paired **absolute miss-rate reduction**, not for the relative percentage.
- [ ] Medians from `SUMMARY` are not presented as means, and means from `PAIRED` are not presented as medians.
- [ ] Goodput is defined before use and not substituted for throughput.
- [ ] P99/P999 are explicitly unconditional server-side completion tails over completed measurement requests.
- [ ] W3 `rho=0.70` is not hidden in an appendix.
- [ ] W2 `rho=0.85` miss reduction is never stated without the tail regression in the same paragraph or adjacent sentence.
- [ ] The passing W3 result is not called a tail-latency improvement.
- [ ] The term "deadline-feasible" is scoped to the migrated request's prediction, not to a no-harm guarantee for all target-queue requests.
- [ ] Simulated `0.5 us` handoff is never called a measurement.
- [ ] No physical machine model, CPU, NIC, CloudLab node, or performance number is written before a frozen manifest exists.
- [ ] No existing placeholder citation is reused.
- [ ] The final paper wording uses "ALTO-style threshold baseline" consistently.
- [ ] Step 00-18, AQB, and DQB remain excluded.
- [ ] Official INFOCOM 2027 rules are rechecked before formatting and again before submission.

**Phase-1 stop condition:** do not draft the Introduction until a human reviewer confirms this matrix or records explicit claim-level revisions.
