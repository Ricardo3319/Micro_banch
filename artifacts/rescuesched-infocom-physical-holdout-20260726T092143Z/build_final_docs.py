#!/usr/bin/env python3
"""Build static final Markdown documentation from already-produced final CSVs.

No benchmark execution, parameter scan, filtering, or post-hoc run exclusion is
performed here.  The script only formats the complete retained results.
"""
from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path

ART = Path(__file__).resolve().parent


def rows(name: str):
    with (ART / name).open(newline="") as f:
        return list(csv.DictReader(f))


def fnum(value: str, digits: int = 3) -> str:
    if value is None or value == "":
        return "—"
    x = float(value)
    if abs(x) >= 100000:
        return f"{x:,.0f}"
    if abs(x) >= 1000:
        return f"{x:,.1f}"
    return f"{x:.{digits}f}"


def pp(value: str) -> str:
    return f"{100.0 * float(value):.2f}%" if value else "—"


setup_summary = rows("setup_variant_summary.csv")
boot = rows("bootstrap_95ci.csv")
mechanism_summary = rows("mechanism_summary.csv")
mechanism_cost = rows("mechanism_cost_aggregate.csv")
rpc_paths = rows("rpc_path_breakdown.csv")
tail_regs = rows("tail_regressions.csv")
general = rows("generalization_directional_summary.csv")

setup_order = [
    "W2-rho070-w16", "W2-rho085-w16", "W3-rho070-w16",
    "W3-rho085-w8", "W3-rho085-w12", "W3-rho085-w16", "W3-rho090-w16",
]
variant_order = ["FIFO", "M0", "L1Only", "CentralOnly", "FullHybrid"]
metric_order = ["deadline_violation_rate", "slo_goodput_rps", "server_p99_us", "server_p999_us"]
metric_short = {
    "deadline_violation_rate": "miss Δ(pp)",
    "slo_goodput_rps": "goodput Δ(rps)",
    "server_p99_us": "P99 Δ(us)",
    "server_p999_us": "P999 Δ(us)",
}


def paired_table(comparison: str, setups: list[str]) -> str:
    lookup = {(r["setup"], r["metric"]): r for r in boot
              if r["scope"] == "local_synthetic_runtime" and r["comparison"] == comparison}
    out = ["| setup | " + " | ".join(metric_short[m] for m in metric_order) + " |",
           "|---|---:|---:|---:|---:|"]
    for setup in setups:
        cells = []
        for metric in metric_order:
            r = lookup[(setup, metric)]
            delta = fnum(r["paired_mean_delta"])
            lo = fnum(r["bootstrap95_mean_delta_low"])
            hi = fnum(r["bootstrap95_mean_delta_high"])
            cells.append(f"{delta} [{lo}, {hi}]; {r['wins']}/{r['losses']}; p={r['exact_paired_signflip_p_two_sided']}")
        out.append(f"| {setup} | " + " | ".join(cells) + " |")
    return "\n".join(out)


def outcome_median_table(scope: str) -> str:
    selected = [r for r in setup_summary if r["scope"] == scope]
    selected.sort(key=lambda r: ((setup_order + ["RPC-W3-rho085-w8-scale10"]).index(r["setup"]),
                                 variant_order.index(r["variant"])))
    out = ["| setup | variant | n | miss | SLO goodput rps | server P99 us | server P999 us | client P99 RTT us | client P999 RTT us |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for r in selected:
        out.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            r["setup"], r["variant"], r["n_seeds"], pp(r["deadline_violation_rate_median"]),
            fnum(r["slo_goodput_rps_median"], 1), fnum(r["server_p99_us_median"]),
            fnum(r["server_p999_us_median"]), fnum(r["client_p99_rtt_us_median"]),
            fnum(r["client_p999_rtt_us_median"])))
    return "\n".join(out)


mech = defaultdict(dict)
for r in mechanism_summary:
    mech[(r["scope"], r["setup"], r["variant"])][r["metric"]] = r


def med(scope: str, setup: str, variant: str, metric: str) -> str:
    r = mech.get((scope, setup, variant), {}).get(metric)
    return "" if r is None else r["median"]


def mechanism_table(scope: str, variants: set[str]) -> str:
    keys = sorted((key for key in mech if key[0] == scope and key[2] in variants),
                  key=lambda key: ((setup_order + ["RPC-W3-rho085-w8-scale10"]).index(key[1]),
                                   variant_order.index(key[2])))
    out = ["| setup | variant | migrations | repeats | max migrations/request | central commits | central success | control ns/request | scheduler missed | max lag us | handoff P99 ns | scans/request |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for _, setup, variant in keys:
        sr = med(scope, setup, variant, "central_rescue_success_rate")
        out.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            setup, variant, fnum(med(scope, setup, variant, "migration_count"), 0),
            fnum(med(scope, setup, variant, "repeat_migrations"), 0),
            fnum(med(scope, setup, variant, "max_migrations_per_request"), 0),
            fnum(med(scope, setup, variant, "central_commits"), 0),
            fnum(sr, 3), fnum(med(scope, setup, variant, "control_path_ns_per_request"), 1),
            fnum(med(scope, setup, variant, "scheduler_missed_fraction"), 5),
            fnum(med(scope, setup, variant, "scheduler_max_epoch_lag_us"), 2),
            fnum(med(scope, setup, variant, "handoff_p99_ns"), 1),
            fnum(med(scope, setup, variant, "scanned_entries_per_request"), 3)))
    return "\n".join(out)


cost_lookup = {(r["scope"], r["setup"], r["metric"]): r for r in mechanism_cost
               if r["comparison"] == "FullHybrid_vs_FIFO"}
cost_table = ["| setup | net avoided misses | migrations / net avoided miss | central commits / net avoided miss |",
              "|---|---:|---:|---:|"]
for setup in setup_order:
    n = cost_lookup[("local_synthetic_runtime", setup, "net_avoided_misses")]
    m = cost_lookup[("local_synthetic_runtime", setup, "candidate_migrations_per_net_avoided_miss")]
    c = cost_lookup[("local_synthetic_runtime", setup, "central_commits_per_net_avoided_miss")]
    cost_table.append(f"| {setup} | {fnum(n['median'], 0)} | {fnum(m['median'])} | {fnum(c['median'])} |")
cost_table = "\n".join(cost_table)

# RPC path residual medians.
rpc_path_group = defaultdict(lambda: defaultdict(list))
for r in rpc_paths:
    for m in ["non_runtime_residual_p99_us", "non_runtime_residual_p999_us", "response_queue_high_watermark"]:
        rpc_path_group[r["variant"]][m].append(float(r[m]))


def median(values: list[float]) -> float:
    values = sorted(values)
    n = len(values)
    return values[n // 2] if n % 2 else (values[n // 2 - 1] + values[n // 2]) / 2

rpc_path_table = ["| variant | residual P99 us | residual P999 us | response queue HWM |",
                  "|---|---:|---:|---:|"]
for variant in variant_order:
    g = rpc_path_group[variant]
    rpc_path_table.append(f"| {variant} | {median(g['non_runtime_residual_p99_us']):.3f} | "
                          f"{median(g['non_runtime_residual_p999_us']):.3f} | "
                          f"{median(g['response_queue_high_watermark']):.0f} |")
rpc_path_table = "\n".join(rpc_path_table)

# Same-class tail regressions whose bootstrap CI is wholly in the regression direction.
tail_rows = []
for r in tail_regs:
    if (r["scope"] == "local_synthetic_runtime" and r["comparison"] == "FullHybrid_vs_L1Only"
            and r["dimension"] == "method" and float(r["bootstrap95_mean_high"]) < 0):
        tail_rows.append(r)
tail_rows.sort(key=lambda r: (setup_order.index(r["setup"]), r["group"], r["quantile"]))
tail_table = ["| setup | class | quantile | FullHybrid improvement vs L1Only us | bootstrap 95% CI | exact p | dz | W/L |",
              "|---|---|---|---:|---|---:|---:|---:|"]
for r in tail_rows:
    tail_table.append(f"| {r['setup']} | {r['group']} | {r['quantile']} | {fnum(r['mean'])} | "
                      f"[{fnum(r['bootstrap95_mean_low'])}, {fnum(r['bootstrap95_mean_high'])}] | "
                      f"{r['exact_paired_signflip_p_two_sided']} | {fnum(r['cohen_dz'])} | {r['wins']}/{r['losses']} |")
tail_table = "\n".join(tail_table)

# RPC paired outcome table, with client RTT in addition to server outcomes.
rpc_metrics = ["deadline_violation_rate", "slo_goodput_rps", "server_p99_us", "server_p999_us",
               "client_p99_rtt_us", "client_p999_rtt_us"]
rpc_comparisons = ["FullHybrid_vs_FIFO", "FullHybrid_vs_M0", "FullHybrid_vs_L1Only",
                   "FullHybrid_vs_CentralOnly", "CentralOnly_vs_FIFO"]
rpc_paired = ["| comparison | metric | mean candidate improvement | bootstrap 95% CI | exact p | dz | rank-biserial | W/L |",
              "|---|---|---:|---|---:|---:|---:|---:|"]
for comparison in rpc_comparisons:
    for metric in rpc_metrics:
        matches = [r for r in boot if r["scope"] == "loopback_udp_rpc" and
                   r["comparison"] == comparison and r["metric"] == metric]
        if not matches:
            continue
        r = matches[0]
        rpc_paired.append(f"| {comparison} | {metric} | {fnum(r['paired_mean_delta'])} | "
                          f"[{fnum(r['bootstrap95_mean_delta_low'])}, {fnum(r['bootstrap95_mean_delta_high'])}] | "
                          f"{r['exact_paired_signflip_p_two_sided']} | {fnum(r['cohen_dz'])} | "
                          f"{fnum(r['paired_rank_biserial'])} | {r['wins']}/{r['losses']} |")
rpc_paired = "\n".join(rpc_paired)

# Directional generalization summary.
general_table = ["| comparison | metric | setups better / worse / overlap | seed-pair W/L/T |",
                 "|---|---|---:|---:|"]
for r in general:
    if r["scope"] != "local_synthetic_runtime" or r["comparison"] not in {
        "FullHybrid_vs_FIFO", "FullHybrid_vs_M0", "FullHybrid_vs_L1Only"
    }:
        continue
    general_table.append(f"| {r['comparison']} | {r['metric']} | "
                         f"{r['bootstrap_ci_candidate_better_setups']}/{r['bootstrap_ci_candidate_worse_setups']}/{r['bootstrap_ci_overlap_setups']} | "
                         f"{r['total_seed_pair_wins']}/{r['total_seed_pair_losses']}/{r['total_seed_pair_ties']} |")
general_table = "\n".join(general_table)

report = f"""# RescueSched INFOCOM local physical holdout report

**Artifact:** `{ART}`  
**Experiment start:** 2026-07-26 09:21:43 UTC  
**Final validation date:** 2026-07-26  
**Branch:** `codex/infocom2027-integration`  
**Commit:** `9fdaf5679dfecfd98b573f4ab9b14085258b7bf3`  
**Source state:** dirty, uncommitted, unstaged; all runs record `dirty=1`  
**Scope:** local synthetic physical/runtime plus single-host loopback UDP RPC only

## 1. Bottom line

The holdout data support a **workload- and environment-qualified** conclusion:

* On all seven synthetic setups, FullHybrid improves deadline-violation rate, SLO goodput, server P99, and server P999 relative to FIFO in all 35 paired holdout-seed comparisons. Every setup-level paired-bootstrap 95% CI excludes zero in the improvement direction.
* Relative to M0, FullHybrid improves miss rate and goodput on all seven setups and improves P99 on all seven at the bootstrap-CI level, but W2 P999 is unresolved/slightly regressive: W2-rho070 mean P999 regression 18.885 us, CI [-40.951, 5.546] in candidate-improvement convention; W2-rho085 regression 11.634 us, CI [-55.235, 25.934].
* Relative to L1Only, FullHybrid improves miss rate/goodput on six of seven setups, but regresses W3-rho070 miss rate by 0.472 percentage points and goodput by 1,958.5 rps. FullHybrid also frequently regresses P99/P999 versus L1Only. Therefore the data do **not** support a blanket tail-latency advantage for the full hybrid.
* In the four strict ablation setups, FullHybrid beats CentralOnly on miss rate, goodput, P99, and P999 in all 20 seed pairs, but pays substantially more migrations, repeated migrations, control-path work, and scheduler lag.
* The real loopback RPC result is negative for distributed L1/full hybrid at this configuration. FullHybrid versus FIFO regresses miss rate by 8.48 percentage points, server goodput by 2,208.1 rps, client P99 RTT by 4,350.6 us, and client P999 RTT by 5,594.6 us on average. These results are retained and are not filtered.
* Scheduler CPU time was **not directly measured**. Decision wall duration, cycles, scan counts, control-path duration, missed epochs, and scheduler lag are reported only as proxies/overhead indicators.

With only five paired seeds per setup, the exact two-sided sign-flip test has minimum attainable p=0.0625. Thus even 5/5 aligned pairs do not reach alpha=0.05. Statements below distinguish a bootstrap CI excluding zero from exact-test significance.

## 2. Protocol and provenance

### 2.1 Holdout and fixed parameters

* Holdout seeds: `71,83,97,109,127`.
* Development/tuning seeds: `11,23,37,47,59`; the sets are disjoint.
* No final parameter scan was run.
* Fixed M1 tuple in every final row and manifest: `moves=4`, `epsilon=6 us`, `lookahead=20 us`, `idle-pulls=0`.
* Bootstrap: paired by holdout seed, 10,000 draws, seed `2026072603`.
* Delta convention in `aggregate.csv` and `bootstrap_95ci.csv`: positive means the candidate is better. For misses and latency this is baseline minus candidate; for goodput it is candidate minus baseline.

### 2.2 Environments

Synthetic physical runtime:

* 5,500 requests/run, 500 warmup, 5,000 measured.
* Strict worker affinity, `time_scale=20`, logical scheduler period 5 us.
* W2 at rho 0.70/0.85 with 16 workers.
* W3 at rho 0.70/0.85/0.90 with 16 workers, plus rho 0.85 with 8/12 workers.

Loopback RPC:

* Real UDP client/server on one host, 8 worker CPUs, two receiver CPUs, two response-sender CPUs, separate scheduler/server-main/client sender/client receiver CPUs.
* `arrival_scale=10`, `time_scale=1`, scheduler period 100 us.
* This is low-load implementation/path evidence, **not** a formal rho=0.85 deployment result and not CloudLab/two-host evidence.
* All 25 runs exceed the original client send-lag P99 4-us warning threshold; the warning is retained, non-exclusionary, and visible in every RPC row.

No S3, CloudLab, remote-host, host tuning, kernel tuning, or deployment tuning was performed.

## 3. Run completeness and correctness

* 160/160 synthetic runs and 25/25 RPC runs are `VALID_COMPLETE` and correctness-valid: 185/185 total.
* Forty setup/seed cells have identical embedded and input trace hashes across all compared variants.
* All runs: 5,500/5,500 completed, 0 cancelled, 0 duplicate execution, 0 duplicate completion, 0 lost descriptors, 0 nonzero reservations, 0 affinity failures, and 0 dropped decision records.
* Every RPC run: 5,500 accepted/enqueued/sent/received, 0 timeout, send failure, invalid/duplicate response, invalid/duplicate packet, unknown request, flow mismatch, enqueue/send failure, or receiver internal failure. All affinity checks pass.
* No outlier or failed attempt is silently removed. Failed protocols and harness mistakes are preserved separately and excluded from final estimates for stated protocol reasons.

Primary machine-readable sources:

* `{ART / 'raw_metrics.csv'}`
* `{ART / 'paired_metrics.csv'}`
* `{ART / 'aggregate.csv'}`
* `{ART / 'bootstrap_95ci.csv'}`
* `{ART / 'correctness_invariant_detail.csv'}`
* `{ART / 'holdout_trace_audit.csv'}`

## 4. Same-environment variant medians

These are medians across the five holdout seeds; paired deltas and uncertainty follow in later sections.

{outcome_median_table('local_synthetic_runtime')}

## 5. Paired synthetic outcomes

Each cell is `paired mean candidate improvement [paired-bootstrap 95% CI]; wins/losses; exact two-sided sign-flip p`. Positive is better for the candidate. Full effect sizes (`Cohen dz` and paired rank-biserial) for every row are in `{ART / 'bootstrap_95ci.csv'}`.

### 5.1 FullHybrid versus FIFO

{paired_table('FullHybrid_vs_FIFO', setup_order)}

All 28 setup/metric CIs exclude zero in FullHybrid's favor; all four outcome metrics are 35/0 over seed pairs. Exact p is 0.0625 for each 5/5 row, so alpha=0.05 is not reached.

### 5.2 FullHybrid versus M0

{paired_table('FullHybrid_vs_M0', setup_order)}

The two W2 P999 rows are unresolved and have negative mean candidate improvement. All other M0 outcome rows shown above have positive mean improvement; several CIs, especially W2 tails, overlap zero.

### 5.3 FullHybrid versus L1Only

{paired_table('FullHybrid_vs_L1Only', setup_order)}

This comparison exposes the principal synthetic tradeoff: central rescue usually reduces misses but often worsens high quantiles relative to distributed L1 alone. W3-rho070 is a direct outcome regression, not merely an overhead increase.

### 5.4 Mechanism ablation: FullHybrid versus CentralOnly

{paired_table('FullHybrid_vs_CentralOnly', ['W2-rho085-w16', 'W3-rho070-w16', 'W3-rho085-w16', 'W3-rho090-w16'])}

FullHybrid is directionally better on all four outcomes and all 20 seed pairs, while using the distributed L1 path in addition to central rescue.

### 5.5 Mechanism ablation: CentralOnly versus L1Only

{paired_table('CentralOnly_vs_L1Only', ['W2-rho085-w16', 'W3-rho070-w16', 'W3-rho085-w16', 'W3-rho090-w16'])}

CentralOnly improves W2-rho085 misses/goodput but regresses P99/P999. On W3, its tails are consistently worse, and miss/goodput are usually worse or unresolved. CentralOnly has 0/20 P99 and 0/20 P999 wins against L1Only across these four setups.

## 6. Holdout generalization across worker/load setups

{general_table}

Coverage includes W3 rho=0.85 with 8/12/16 workers, W3 with rho=0.70/0.85/0.90 at 16 workers, and W2 with rho=0.70/0.85 at 16 workers. FullHybrid-versus-FIFO direction is stable over this tested matrix. The effects are heterogeneous and were intentionally not pooled into a universal effect; no claim is made beyond the tested host, traces, worker counts, and loads.

Detailed files:

* `{ART / 'worker_load_summary.csv'}`
* `{ART / 'generalization_directional_summary.csv'}`

## 7. Mechanism cost and scheduler overhead

### 7.1 Synthetic ablation medians

{mechanism_table('local_synthetic_runtime', {'L1Only', 'CentralOnly', 'FullHybrid'})}

FullHybrid medians span 3,346–4,035 migrations/run, 96–457 repeats/run, and 635–1,524 central commits/run. Central rescue success is about 1.0 for W2 and 0.867–0.945 for W3. FullHybrid control-path duration is about 5.0–11.6 us/request, scheduler missed fraction 0.00258–0.01176, scheduler maximum-lag median 21.7–79.3 us, and handoff P99 median about 6.3–15.1 us.

These values do not support a low-migration or low-overhead claim.

### 7.2 Cost per net avoided miss versus FIFO

{cost_table}

The migration cost is 1.068–2.023 migrations per net avoided miss across synthetic setups; central-commit cost is 0.331–0.539 per net avoided miss. `net avoided miss` is paired request-level accounting versus FIFO. In RPC, FullHybrid has **negative** net avoided misses (median -404), so a positive migrations-per-avoided-miss ratio is undefined and is not reported.

Complete mechanism data:

* `{ART / 'mechanism_metrics.csv'}`
* `{ART / 'mechanism_summary.csv'}`
* `{ART / 'mechanism_cost_aggregate.csv'}`

## 8. Tail decomposition and regressions

### 8.1 Same request class: FullHybrid versus L1Only

Rows below are the same-trace short/long request classes whose paired-bootstrap CI lies wholly in the regression direction. The candidate-improvement number is therefore negative.

{tail_table}

This locates, rather than hides, tail regressions: short-request P99 at W3-rho070, long-request P99/P999 at W2-rho085, short-request P999 at W3-rho085-w12, long-request P999 at W3-rho085-w8, and long-P99/short-P999 at W3-rho090.

### 8.2 Rescued/non-rescued and migrated/non-migrated groups

`tail_group_summary.csv` includes all, short/long, central-rescued/non-rescued, migrated/non-migrated, and method×rescue groups with P99, P999, miss rate, counts, bootstrap CIs, and small-group warnings. `within_variant_tail_contrasts.csv` marks every rescue/migration contrast as policy-selected and descriptive, not causal.

Descriptively, W2 central-rescued groups often have near-zero miss rates and migrated groups often have lower tails than non-migrated groups. This does not generalize to W3: for W3-rho085-w16 FullHybrid, median miss rate is 9.40% in the rescued group versus 2.87% in the non-rescued group. The scheduler selects high-risk requests, so these are selection-biased subpopulations and cannot establish that rescue caused the contrast.

Files:

* `{ART / 'tail_group_summary.csv'}`
* `{ART / 'tail_policy_comparisons.csv'}`
* `{ART / 'tail_regressions.csv'}`
* `{ART / 'within_variant_tail_contrasts.csv'}`

## 9. Real loopback RPC end-to-end results

### 9.1 Variant medians

{outcome_median_table('loopback_udp_rpc')}

Mechanism medians for the RPC variants:

{mechanism_table('loopback_udp_rpc', {'L1Only', 'CentralOnly', 'FullHybrid'})}

M0 has median 42 migrations, 1.344 us/request control-path duration, 468.5 us max scheduler lag, and 0.00257 scheduler missed fraction. FIFO has no scheduler/migration overhead.

### 9.2 Paired RPC deltas, uncertainty, exact tests, and effect sizes

{rpc_paired}

For FullHybrid versus FIFO, the exact p is 0.0625 for the 0/5 aligned miss, goodput, server P99, client P99, and client P999 regressions; server P999 is 1/4 with CI crossing zero. Bootstrap CIs exclude zero for miss, goodput, server P99, client P99, and client P999. The exact n=5 limitation still applies.

### 9.3 Per-request non-runtime residual

`non_runtime_residual = client RTT - server completion` is calculated per request before quantiles; it is **not** quantile subtraction and not a direct mutex-wait measurement.

{rpc_path_table}

Response queue high-water marks are only 4–15 across all runs, with zero response enqueue/send failures. The large L1Only/FullHybrid residual, low response-queue occupancy, and code path jointly support the following inference:

* distributed L1 idle workers poll every 100 us;
* the poll path holds the runtime global mutex while scanning (`src/physical/runtime.cpp`, approximately lines 911–950 in the final dirty tree);
* network submission also needs that mutex (`submit_network_request`, approximately line 1289), called by the RPC receiver (`src/app/rpc_server_main.cpp`, approximately line 546);
* therefore L1 polling likely delays ingress before successful submit. Server completion starts after submit and can hide this delay, while client RTT includes it.

This is an evidence-supported inference, not a direct measurement of mutex wait. Direct lock-wait instrumentation remains future work.

The retained RPC anomalies include seed 97 FullHybrid (server P99 about 7,592.4 us, client P99 about 8,002.1 us, scheduler max lag 10,924.219 us) and seed 71 M0 (scheduler max lag above 5,393 us and a P999 outlier). They remain in all means, CIs, and tests.

Files:

* `{ART / 'rpc_path_breakdown.csv'}`
* `{ART / 'rpc_path_breakdown_by_class.csv'}`
* `{ART / 'rpc_variant_summary.csv'}`

## 10. Statistical interpretation

* Pairing unit: holdout seed; traces are hash-identical across compared variants.
* Bootstrap: 10,000 paired resamples of seed-level differences; percentile 95% CI.
* Paired significance: exact two-sided sign-flip test; exact sign test is also recorded.
* Effect sizes: Cohen's dz and paired rank-biserial for every aggregate row.
* At n=5, all-aligned exact two-sided p=0.0625. A bootstrap CI excluding zero is evidence of directional stability in these five pairs, but it is not equivalent to an exact alpha=0.05 rejection.
* No multiple-comparison-adjusted confirmatory family was predeclared. The many tail/class analyses should be treated as diagnostic/exploratory.
* Heterogeneous setups are not pooled into a universal effect.

Complete statistical table: `{ART / 'bootstrap_95ci.csv'}`.

## 11. Failures retained and diagnosed

1. Synthetic attempt 1 hung at W3-rho070-w16 seed 127 M0 after 24 valid runs. Thread snapshots showed the scheduler thread spinning while workers waited. A terminal scheduler handshake/liveness fix was added; the exact configuration then passed 100/100 five-second hard-timeout Release regressions. All attempt-1 outputs are preserved and excluded from final metrics; the entire 160-run paired matrix was restarted cleanly.
2. RPC attempt 1 used a 5-us physical scheduler period. It missed 87.85% of epochs even though decision P50 was 0.712 us, showing host wake/scheduling granularity—not decision compute—dominated. The attempt was invalid under its timing gate and is preserved. Final RPC uses 100 us without changing the M1 tuple.
3. Analyzer attempt 1 incorrectly treated the predeclared 4-us client send-lag performance gate as a correctness failure. All 25 RPC runs had complete delivery and correct invariants. The analyzer was corrected to retain this as a warning, then all 185 final runs were reanalyzed.
4. Initial targeted TSan runs reported a GCC 11/glibc `pthread_cond_clockwait` double-lock false positive. The steady-clock accounting was preserved while blocking waits were translated through the TSan-visible realtime condition-variable path. Final targeted TSan is 2/2 PASS.

Detailed diagnosis: `{ART / 'FAILURE_DIAGNOSIS.md'}`.

## 12. Final tests and validation

* Final Release build + complete CTest: 32/32 PASS.  
  `{ART / 'validation-logs/final-release-build-and-full-ctest.log'}`
* Final targeted TSan: 2/2 PASS.  
  `{ART / 'validation-logs/final-tsan-build-and-targeted-ctest.log'}`
* Independent synthetic validation: PASS.  
  `{ART / 'validation-logs/synthetic-attempt2-independent-validation.log'}`
* Independent RPC validation: PASS_WITH_SEND_TIMING_WARNINGS.  
  `{ART / 'validation-logs/rpc-attempt2-independent-validation.log'}`
* Final artifact validator: `{ART / 'validate_final_artifact.py'}`

## 13. Remaining risks and claims explicitly not made

* Only five holdout seeds per setup; exact alpha=0.05 significance is unattainable for a 5/5 sign pattern.
* One local host and one loopback RPC topology; no two-host/network-contention/CloudLab evidence.
* RPC is low-load `arrival_scale=10` implementation evidence and carries send-timing warnings in all 25 runs.
* Synthetic CPU-service execution and time scaling do not reproduce every OS/network deployment effect.
* Scheduler CPU time and mutex wait are not directly instrumented.
* P999 estimates use 5,000 measured requests/run and are therefore noisy; several comparisons cross zero or regress.
* Rescued/migrated subgroup contrasts are selection-biased.
* Migration counts, repeated migrations, and control overhead are high; no low-overhead claim is supported.
* FullHybrid is not universally superior to L1Only, especially for W3-rho070 outcomes and several P99/P999/class tails.
* The data support holdout generalization only over the tested W2/W3 traces, loads, and worker counts, not universal generality.

## 14. Reproducibility index

* Run plans: `{ART / 'synthetic_run_plan.json'}`, `{ART / 'rpc_run_plan.json'}`
* Commands: `{ART / 'commands.jsonl'}`
* Raw/paired/aggregate: `{ART / 'raw_metrics.csv'}`, `{ART / 'paired_metrics.csv'}`, `{ART / 'aggregate.csv'}`
* Analysis scripts: `{ART / 'analyze_results.py'}`, `{ART / 'build_audit_outputs.py'}`
* Analysis manifests: `{ART / 'analysis_manifest.json'}`, `{ART / 'derived_analysis_manifest.json'}`
* Validation summary: `{ART / 'VALIDATION_SUMMARY.md'}`
* Failure diagnosis: `{ART / 'FAILURE_DIAGNOSIS.md'}`
"""

validation = f"""# Final validation summary

**Artifact:** `{ART}`  
**Commit/state:** `9fdaf5679dfecfd98b573f4ab9b14085258b7bf3`, dirty, uncommitted, unstaged  
**Scope:** local synthetic runtime and single-host loopback UDP RPC only

## Completion matrix

| Requirement | Status | Evidence |
|---|---|---|
| Holdout seeds/traces not used for tuning | PASS within recorded protocol | Seeds 71/83/97/109/127, disjoint from development 11/23/37/47/59; `{ART / 'holdout_trace_audit.csv'}` |
| Same-environment FIFO/M0/L1/M1 | PASS | 7 synthetic setups; FIFO/M0/L1Only/FullHybrid all paired on each setup/seed; `{ART / 'synthetic_run_plan.json'}` |
| L1-only / central-only / full-hybrid ablation | PASS | 4 primary synthetic setups plus RPC; CentralOnly uniquely has distributed L1 disabled |
| Real RPC RTT, miss, SLO goodput | PASS_WITH_TIMING_WARNINGS | 25/25 complete loopback UDP runs; `{ART / 'rpc_variant_summary.csv'}` |
| Worker count, W2/W3, multi-load | PASS for tested matrix | W3 workers 8/12/16; W2/W3; rho 0.70/0.85/0.90 |
| Paired bootstrap, paired tests, effect sizes | PASS | 10,000 draws; exact sign-flip/sign test; dz; rank-biserial; `{ART / 'bootstrap_95ci.csv'}` |
| Rescued/non-rescued and request-class P99/P999 | PASS, descriptive | `{ART / 'tail_group_summary.csv'}`, `{ART / 'tail_regressions.csv'}` |
| Migration/rescue/control/scheduler overhead | PASS except direct scheduler CPU | Counts, repeats, commits, success, cost/miss, duration/cycles, scans, lag; scheduler CPU explicitly not directly sampled |
| Correctness invariants and full CTest | PASS | 185/185 correctness; Release 32/32; targeted TSan 2/2 |

## Final run counts

* Synthetic: 160/160 valid.
* Loopback RPC: 25/25 correctness-valid, all with retained client send-lag timing warning.
* Total: 185/185 correctness-valid.
* Setup/seed paired trace cells: 40/40 hash-identical across variants.
* Fixed tuple values observed: exactly `4/6/20/0`.
* Decision capture: 185/185 complete, 0 dropped records.

## Correctness invariants

Across all final runs:

* completed = total = 5,500; measured = 5,000;
* cancelled = 0;
* duplicate execution/completion = 0/0;
* lost descriptors = 0;
* nonzero reservations = 0;
* affinity failures = 0;
* all decision records captured.

Across every RPC run:

* accepted/enqueued/sent/client responses = 5,500 each;
* timeouts, send failures, invalid/duplicate responses = 0;
* invalid/duplicate packets, unknown requests, flow mismatches = 0;
* response enqueue/send failures and receiver internal failures = 0;
* client sender/receiver, server receiver/sender, scheduler, server-main affinity all pass.

Machine-readable audit: `{ART / 'correctness_invariant_detail.csv'}`.

## Test logs

* Release complete CTest: **32/32 PASS**, return code 0.  
  `{ART / 'validation-logs/final-release-build-and-full-ctest.log'}`
* Targeted TSan: **2/2 PASS**, return code 0.  
  `{ART / 'validation-logs/final-tsan-build-and-targeted-ctest.log'}`
* Synthetic independent validation: **PASS**.  
  `{ART / 'validation-logs/synthetic-attempt2-independent-validation.log'}`
* RPC independent validation: **PASS_WITH_SEND_TIMING_WARNINGS**.  
  `{ART / 'validation-logs/rpc-attempt2-independent-validation.log'}`

## Non-fatal warnings and retained outliers

* 25/25 RPC runs: client send-lag P99 > 4 us. No run was removed for this warning.
* RPC seed 97 FullHybrid: server/client high-tail and 10,924.219-us scheduler-lag anomaly retained.
* RPC seed 71 M0: >5,393-us scheduler-lag/P999 anomaly retained.
* Failed 5-us RPC period, failed M0 liveness attempt, analyzer classification bug, and pre-fix TSan logs remain in the artifact.

## Validation command

```bash
python3 {ART / 'validate_final_artifact.py'}
```

The final saved output is `{ART / 'validation-logs/final-artifact-validation.log'}`.

## Scope exclusions

No S3, CloudLab, remote host, host/kernel tuning, or new parameter scan was performed. No code was staged or committed. Scheduler CPU time and mutex wait were not directly measured.
"""

failure = f"""# Failure diagnosis and retained abnormal results

This document records every failed/invalid attempt relevant to the final local physical holdout artifact. None of these results was silently deleted, cherry-picked, or mixed into the final paired estimates.

## 1. Synthetic attempt 1: M0 terminal liveness hang

**Observed:** the first 160-run matrix completed 24 valid runs, then W3-rho070-w16 seed 127 M0 ran for more than ten minutes. Worker threads were mostly blocked while one scheduler thread consumed approximately one CPU. The partial attempt is preserved at:

`{ART / 'failed-attempt1-before-liveness-fix'}`

**Diagnosis:** shutdown/terminal completion did not provide a robust terminal handshake to the scheduler wait path. A timing-sensitive scheduler could continue after all requests were terminal while the main/worker path waited for orderly termination.

**Fix:** terminal completion is now published through `terminal_run_complete` with release/acquire semantics and wakes the scheduler wait condition. Terminal accounting is centralized so completion and submission-done transitions update the handshake consistently.

**Verification:**

* GDB timing did not reproduce the original hang; this negative reproduction is preserved and was not treated as proof of correctness.
* The post-fix exact configuration passed 100/100 Release iterations with a 5-second hard timeout, all `VALID_COMPLETE` with invariants passing:  
  `{ART / 'failure-diagnosis/m0-W3-rho070-w16-seed127-attempt3-postfix-release/STATUS.txt'}`
* A diagnostic harness initially looked for `summary_metrics.csv` instead of `summary.csv`; that harness failure is separately recorded and the full 100-iteration sequence was restarted.
* The complete 160-run final matrix was restarted from scratch; no attempt-1 metrics entered the final CSVs.

## 2. RPC attempt 1: 5-us physical scheduler period invalid

Preserved at:

`{ART / 'failed-rpc-attempt1-checkperiod5'}`

The first CentralOnly run was correctness-valid but failed the predeclared scheduler timing gate:

* scheduled epochs: 122,046;
* executed epochs: 14,823;
* missed fraction: 0.8785458;
* decision P50: 0.712 us;
* observed execution cadence: roughly one epoch per 41 us over a 611-ms run;
* client send-lag P99: 6.115 us, above the 4-us warning gate.

The root cause is host wake/scheduling granularity at a real 5-us period, not central decision compute. Network ingress requires `time_scale=1`, so the synthetic logical 5-us period cannot be used as a literal loopback physical wake period on this host.

**Corrective protocol:** restart all RPC pairs with `check_period_us=100`, keep M1 fixed at 4/6/20/0, preserve the failed attempt, and retain send-lag exceedance as a warning rather than a correctness exclusion. No host/kernel tuning was performed.

## 3. Analyzer attempt 1: send-lag warning misclassified as fatal

Preserved at:

`{ART / 'failed-analysis-attempt1-send-lag-classification'}`

The first analyzer treated `client_send_lag_p99_us > 4` as a correctness failure despite the attempt-2 protocol designating it as a retained timing warning. This rejected all 25 RPC runs even though they had:

* complete request/response delivery;
* zero timeouts/network errors/duplicates;
* valid affinity;
* `VALID_COMPLETE` runtime status;
* all runtime invariants passing.

**Fix:** keep correctness and scheduler-miss gates fail-closed, require the recorded 100-us RPC period, classify send lag as `validity_warnings`, and reanalyze all 185 runs. The original analyzer outputs remain preserved.

## 4. TSan false positive and wait-path adjustment

The initial targeted TSan logs reported a double lock through GCC 11/glibc's `pthread_cond_clockwait` path when multiple condition variables shared a mutex:

* `{ART / 'validation-logs/tsan-targeted-ctest.log'}`
* `{ART / 'validation-logs/tsan-targeted-ctest-after-lifetime-fix.log'}`

The runtime keeps steady-clock epoch accounting but translates each blocking deadline to a realtime `condition_variable::wait_until`, which GCC 11 TSan models correctly. A callback-lifetime issue in the concurrency test was also fixed so captured synchronization objects outlive the runtime callback owner.

Final targeted TSan passes 2/2:

`{ART / 'validation-logs/final-tsan-build-and-targeted-ctest.log'}`

This is described as a tool-model compatibility adjustment, not evidence that TSan can prove absence of all races.

## 5. Retained final anomalies and regressions

### RPC seed 97 FullHybrid

* server P99 approximately 7,592.4 us;
* client P99 approximately 8,002.1 us;
* scheduler max lag 10,924.219 us.

### RPC seed 71 M0

* scheduler max lag above 5,393 us;
* accompanying P999 abnormality.

Both remain in raw metrics, means, CIs, exact tests, and effect sizes.

### Synthetic regressions

* FullHybrid versus L1Only regresses W3-rho070 miss rate, goodput, and P99.
* Multiple request-class P99/P999 rows regress versus L1Only.
* W2 P999 versus M0 is unresolved and slightly regressive in mean.
* CentralOnly has substantial tail regressions versus L1Only on all four primary ablation setups.

These are enumerated in `{ART / 'FINAL_REPORT.md'}` and `{ART / 'tail_regressions.csv'}`.

## 6. Exclusion policy

Only runs from protocols declared invalid before analysis or harness outputs that did not constitute a completed run are excluded. The reasons and raw files are preserved. Within the final attempt-2 matrices, all 185 runs—including warnings and outliers—are retained.
"""

readme = f"""# RescueSched INFOCOM local physical holdout artifact

Start with:

1. `{ART / 'FINAL_REPORT.md'}` — complete findings, regressions, statistics, mechanisms, RPC evidence, and limitations.
2. `{ART / 'VALIDATION_SUMMARY.md'}` — run counts, invariants, tests, and requirement checklist.
3. `{ART / 'FAILURE_DIAGNOSIS.md'}` — preserved failed attempts, root causes, and retained anomalies.

Core machine-readable data:

* `{ART / 'raw_metrics.csv'}`
* `{ART / 'paired_metrics.csv'}`
* `{ART / 'aggregate.csv'}`
* `{ART / 'bootstrap_95ci.csv'}`
* `{ART / 'setup_variant_summary.csv'}`
* `{ART / 'mechanism_metrics.csv'}`
* `{ART / 'mechanism_summary.csv'}`
* `{ART / 'mechanism_cost_aggregate.csv'}`
* `{ART / 'tail_group_summary.csv'}`
* `{ART / 'tail_policy_comparisons.csv'}`
* `{ART / 'tail_regressions.csv'}`
* `{ART / 'rpc_path_breakdown.csv'}`
* `{ART / 'correctness_invariant_detail.csv'}`

Validation:

```bash
python3 {ART / 'validate_final_artifact.py'}
sha256sum -c {ART / 'SHA256SUMS'}
```

Scope is local synthetic runtime plus single-host loopback UDP RPC only. The tree is dirty and uncommitted. No parameter scan, S3, CloudLab, remote host, host tuning, or kernel tuning was performed in this final holdout work.
"""

(ART / "FINAL_REPORT.md").write_text(report)
(ART / "VALIDATION_SUMMARY.md").write_text(validation)
(ART / "FAILURE_DIAGNOSIS.md").write_text(failure)
(ART / "README.md").write_text(readme)
print(ART / "FINAL_REPORT.md")
print(ART / "VALIDATION_SUMMARY.md")
print(ART / "FAILURE_DIAGNOSIS.md")
print(ART / "README.md")
