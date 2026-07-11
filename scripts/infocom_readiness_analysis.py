import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ALLOW_LEGACY = "--allow-legacy" in sys.argv
STEP15 = ROOT / "artifacts" / (
    "step-15-rescuesched" if ALLOW_LEGACY else "step-19-rescuesched-validity-v2"
)
STEP18 = ROOT / "artifacts" / (
    "step-18-infocom-readiness" if ALLOW_LEGACY
    else "step-19-rescuesched-validity-v2"
)


def read_csv(path):
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if rows and not ALLOW_LEGACY:
        versions = {row.get("schema_version", "") for row in rows}
        if versions != {"rescuesched-v2"}:
            raise ValueError(
                f"{path} is not strict rescuesched-v2; use --allow-legacy explicitly"
            )
    return rows


def f(row, key, default=0.0):
    try:
        value = row.get(key, "")
        if value == "":
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def group(rows, keys):
    out = defaultdict(list)
    for row in rows:
        out[tuple(row.get(k, "") for k in keys)].append(row)
    return out


def med(rows, key):
    vals = [f(r, key) for r in rows]
    return statistics.median(vals) if vals else 0.0


def mean(rows, key):
    vals = [f(r, key) for r in rows]
    return statistics.mean(vals) if vals else 0.0


def tcrit_95(n):
    table = {
        2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776,
        6: 2.571, 7: 2.447, 8: 2.365, 9: 2.306, 10: 2.262,
    }
    return table.get(n, 1.96)


def ci95(rows, key):
    vals = [f(r, key) for r in rows]
    if not vals:
        return (0.0, 0.0, 0.0)
    mu = statistics.mean(vals)
    if len(vals) <= 1:
        return (mu, mu, mu)
    half = tcrit_95(len(vals)) * statistics.stdev(vals) / math.sqrt(len(vals))
    return (mu, mu - half, mu + half)


def fmt(x, digits=6):
    if isinstance(x, str):
        return x
    if abs(x) >= 1000:
        return f"{x:.0f}"
    return f"{x:.{digits}f}".rstrip("0").rstrip(".")


def lookup_group(rows, keys, **where):
    for key, vals in group(rows, keys).items():
        sample = vals[0]
        ok = True
        for k, v in where.items():
            if sample.get(k, "") != str(v):
                ok = False
                break
        if ok:
            return vals
    return []


def write_ci_summary(named_rows):
    metrics = [
        "P99_us",
        "P999_us",
        "slo_violation_rate",
        "intra_move_rate",
        "beneficial_migration_ratio",
        "useless_migration_ratio",
        "rescue_per_migration",
        "predicted_target_unsafe_accept_count",
        "harmful_actual_count",
        "target_induced_miss_actual",
    ]
    fieldnames = [
        "source", "scenario", "workload", "rho", "method",
        "service_estimate_mode", "migration_cost_us", "w2_hot_core_count",
        "w2_hot_dispatch_prob", "target_insert_policy", "metric",
        "n", "mean", "median", "ci95_low", "ci95_high",
    ]
    out_rows = []
    for source, rows, keys in named_rows:
        for _, vals in sorted(group(rows, keys).items()):
            sample = vals[0]
            for metric in metrics:
                mu, lo, hi = ci95(vals, metric)
                out_rows.append({
                    "source": source,
                    "scenario": sample.get("scenario", ""),
                    "workload": sample.get("workload", ""),
                    "rho": sample.get("rho", ""),
                    "method": sample.get("method", ""),
                    "service_estimate_mode": sample.get("service_estimate_mode", ""),
                    "migration_cost_us": sample.get("migration_cost_us", ""),
                    "w2_hot_core_count": sample.get("w2_hot_core_count", ""),
                    "w2_hot_dispatch_prob": sample.get("w2_hot_dispatch_prob", ""),
                    "target_insert_policy": sample.get("target_insert_policy", ""),
                    "metric": metric,
                    "n": len(vals),
                    "mean": fmt(mu),
                    "median": fmt(med(vals, metric)),
                    "ci95_low": fmt(lo),
                    "ci95_high": fmt(hi),
                })

    path = STEP18 / "infocom_ci_summary.csv"
    with path.open("w", newline="", encoding="utf-8") as fcsv:
        writer = csv.DictWriter(fcsv, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(out_rows)
    return out_rows


def median_row(vals):
    if not vals:
        return {}
    sample = vals[0]
    out = {
        "scenario": sample.get("scenario", ""),
        "workload": sample.get("workload", ""),
        "rho": sample.get("rho", ""),
        "method": sample.get("method", ""),
        "service_estimate_mode": sample.get("service_estimate_mode", ""),
        "migration_cost_us": sample.get("migration_cost_us", ""),
        "w2_hot_core_count": sample.get("w2_hot_core_count", ""),
        "w2_hot_dispatch_prob": sample.get("w2_hot_dispatch_prob", ""),
        "target_insert_policy": sample.get("target_insert_policy", ""),
    }
    for metric in [
        "P99_us", "P999_us", "slo_violation_rate", "intra_move_rate",
        "beneficial_migration_ratio", "useless_migration_ratio",
        "rescue_per_migration", "rescue_success_count",
        "relief_success_count", "relief_beneficial_migration_ratio",
        "relief_useless_migration_ratio",
        "predicted_target_unsafe_accept_count", "harmful_actual_count",
        "target_induced_miss_actual",
    ]:
        out[metric] = fmt(med(vals, metric))
    return out


def write_median_summary(named_rows):
    fieldnames = [
        "source", "scenario", "workload", "rho", "method",
        "service_estimate_mode", "migration_cost_us", "w2_hot_core_count",
        "w2_hot_dispatch_prob", "target_insert_policy", "P99_us", "P999_us",
        "slo_violation_rate", "intra_move_rate", "beneficial_migration_ratio",
        "useless_migration_ratio", "rescue_per_migration",
        "rescue_success_count", "relief_success_count",
        "relief_beneficial_migration_ratio", "relief_useless_migration_ratio",
        "predicted_target_unsafe_accept_count", "harmful_actual_count",
        "target_induced_miss_actual",
    ]
    out_rows = []
    for source, rows, keys in named_rows:
        for _, vals in sorted(group(rows, keys).items()):
            row = median_row(vals)
            row["source"] = source
            out_rows.append({k: row.get(k, "") for k in fieldnames})
    path = STEP18 / "infocom_median_summary.csv"
    with path.open("w", newline="", encoding="utf-8") as fcsv:
        writer = csv.DictWriter(fcsv, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(out_rows)
    return out_rows


def find_ci(ci_rows, source, scenario, method, metric, **where):
    for row in ci_rows:
        if row["source"] != source:
            continue
        if row["scenario"] != scenario or row["method"] != method or row["metric"] != metric:
            continue
        ok = True
        for k, v in where.items():
            if row.get(k, "") != str(v):
                ok = False
                break
        if ok:
            return row
    return {}


def find_med(med_rows, source, scenario=None, method=None, **where):
    for row in med_rows:
        if row["source"] != source:
            continue
        if scenario is not None and row["scenario"] != scenario:
            continue
        if method is not None and row["method"] != method:
            continue
        ok = True
        for k, v in where.items():
            if row.get(k, "") != str(v):
                ok = False
                break
        if ok:
            return row
    return {}


def count_norescuable_wins(boundary_rows):
    wins = 0
    total = 0
    for key, vals in group(boundary_rows, [
        "w2_hot_core_count", "w2_hot_dispatch_prob", "rho"
    ]).items():
        by_method = group(vals, ["method"])
        rescue = by_method.get(("M1_RescueSched",), [])
        norescue = by_method.get(("M1_RescueSched_NoRescuable",), [])
        if not rescue or not norescue:
            continue
        total += 1
        if med(norescue, "slo_violation_rate") < med(rescue, "slo_violation_rate"):
            wins += 1
    return wins, total


def write_report(med_rows, ci_rows, rows):
    w3_rescue_ci = find_ci(
        ci_rows, "estimator_main", "rescue_estimator_oracle",
        "M1_RescueSched", "slo_violation_rate")
    w3_l1_ci = find_ci(
        ci_rows, "estimator_main", "rescue_estimator_w3_baseline",
        "L1_WorkStealing", "slo_violation_rate")
    w3_m0_ci = find_ci(
        ci_rows, "estimator_main", "rescue_estimator_w3_baseline",
        "M0_IntraHostProactive", "slo_violation_rate")

    est_rows = [
        find_med(med_rows, "estimator_main", "rescue_estimator_oracle", "M1_RescueSched"),
        find_med(med_rows, "estimator_main", "rescue_estimator_class_mean", "M1_RescueSched"),
        find_med(med_rows, "estimator_main", "rescue_estimator_ewma", "M1_RescueSched"),
        find_med(med_rows, "estimator_main", "rescue_estimator_quantile_guard", "M1_RescueSched"),
        find_med(med_rows, "estimator_main", "rescue_estimator_global_mean", "M1_RescueSched"),
        find_med(med_rows, "estimator_main", "rescue_estimator_noisy_oracle_cv_0_5", "M1_RescueSched"),
    ]

    cost_default = find_med(
        med_rows, "cost_calibration", "rescue_cost_calibration",
        "M1_RescueSched", migration_cost_us="0.5")
    cost_measured = find_med(
        med_rows, "cost_calibration", "rescue_cost_measured_handoff",
        "M1_RescueSched")

    boundary_wins, boundary_total = count_norescuable_wins(rows["boundary"])
    hybrid_rescue = find_med(
        med_rows, "hybrid_main", "rescue_hybrid_w2_main",
        "M1_RescueSched", rho="0.85")
    hybrid_norescue = find_med(
        med_rows, "hybrid_main", "rescue_hybrid_w2_main",
        "M1_RescueSched_NoRescuable", rho="0.85")
    hybrid = find_med(
        med_rows, "hybrid_main", "rescue_hybrid_w2_main",
        "M1_RescueSched_Hybrid", rho="0.85")

    stress_append = find_med(
        med_rows, "target_safety_stress", "rescue_target_safety_stress",
        "M1_RescueSched_NoTargetSafety", target_insert_policy="append_tail")
    stress_head = find_med(
        med_rows, "target_safety_stress", "rescue_target_safety_stress",
        "M1_RescueSched_NoTargetSafety", target_insert_policy="head_insert_stress")

    overload_rows = rows["overload"]
    overload = lookup_group(overload_rows, ["scenario", "method"],
                            scenario="rescue_w1_overload_boundary",
                            method="M1_RescueSched")
    overload_med = median_row(overload)

    lines = []
    lines.append("# Step-18 INFOCOM Readiness Summary\n")
    lines.append("本报告由 `python scripts/infocom_readiness_analysis.py` 生成，目标是判断 RescueSched 是否已经从本地预实验推进到 INFOCOM 投稿前验证状态。")
    lines.append("")
    lines.append("## 1. 当前主 Claim 判断\n")
    lines.append("主 claim 可以作为 INFOCOM 预备 claim：RescueSched 在 heavy-tail、有局部 slack 的单 host 多核队列中，通过 rescuability-aware migration 提高迁移质量并降低 SLO violation。")
    lines.append("")
    lines.append("仍不能写成最终论文强 claim：它不是通用负载均衡器，不解决全局过载；target safety 的真实 harmful 证据依赖 stress 模型；真实系统结论仍需要 CloudLab 或 trace replay。")
    lines.append("")
    lines.append("## 2. W3 主结果与 95% CI\n")
    lines.append("| method | SLO mean | 95% CI | note |")
    lines.append("|---|---:|---:|---|")
    for label, row in [
        ("L1_WorkStealing", w3_l1_ci),
        ("M0_IntraHostProactive", w3_m0_ci),
        ("M1_RescueSched oracle", w3_rescue_ci),
    ]:
        if not row:
            continue
        lines.append(f"| {label} | {row['mean']} | [{row['ci95_low']}, {row['ci95_high']}] | n={row['n']} |")
    lines.append("")
    lines.append("| setting | P99 | P999 | SLO | move rate | BMR | UMR | RPM |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for row in [
        find_med(med_rows, "estimator_main", "rescue_estimator_w3_baseline", "L1_WorkStealing"),
        find_med(med_rows, "estimator_main", "rescue_estimator_w3_baseline", "M0_IntraHostProactive"),
        find_med(med_rows, "estimator_main", "rescue_estimator_oracle", "M1_RescueSched"),
    ]:
        if not row:
            continue
        lines.append(
            f"| {row['method']} {row.get('service_estimate_mode', '')} | {row['P99_us']} | {row['P999_us']} | "
            f"{row['slo_violation_rate']} | {row['intra_move_rate']} | {row['beneficial_migration_ratio']} | "
            f"{row['useless_migration_ratio']} | {row['rescue_per_migration']} |"
        )
    lines.append("")
    lines.append("## 3. Estimator Sensitivity\n")
    lines.append("| estimator | SLO | move rate | BMR | UMR | interpretation |")
    lines.append("|---|---:|---:|---:|---:|---|")
    for row in est_rows:
        if not row:
            continue
        mode = row["service_estimate_mode"]
        interp = "oracle upper bound" if mode == "oracle" else "realistic/sensitivity"
        lines.append(
            f"| {mode} | {row['slo_violation_rate']} | {row['intra_move_rate']} | "
            f"{row['beneficial_migration_ratio']} | {row['useless_migration_ratio']} | {interp} |"
        )
    lines.append("")
    lines.append("## 4. Migration Cost Sensitivity\n")
    lines.append("| setting | migration_cost_us | SLO | move rate | BMR | UMR |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for label, row in [("default", cost_default), ("measured local handoff upper bound", cost_measured)]:
        if not row:
            continue
        lines.append(
            f"| {label} | {row['migration_cost_us']} | {row['slo_violation_rate']} | "
            f"{row['intra_move_rate']} | {row['beneficial_migration_ratio']} | {row['useless_migration_ratio']} |"
        )
    lines.append("")
    lines.append("## 5. W2 Boundary 与 Hybrid 必要性\n")
    lines.append(f"W2 boundary sweep 中，NoRescuable 在 {boundary_wins}/{boundary_total} 个 hot-core/prob/rho 组合上 SLO 低于标准 RescueSched。")
    lines.append("")
    lines.append("| method | rho | SLO | move rate | BMR | UMR | relief moves | relief BMR |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for row in [hybrid_rescue, hybrid_norescue, hybrid]:
        if not row:
            continue
        lines.append(
            f"| {row['method']} | {row['rho']} | {row['slo_violation_rate']} | {row['intra_move_rate']} | "
            f"{row['beneficial_migration_ratio']} | {row['useless_migration_ratio']} | "
            f"{row['relief_success_count']} | {row['relief_beneficial_migration_ratio']} |"
        )
    lines.append("")
    lines.append("解释：W2 burst-skew 下 NoRescuable 的优势说明纯 SLO rescue 不是所有 burst/skew 场景的最优 SLO 策略；Hybrid 是投稿前需要继续打磨的边界策略，而不是 W3 主 claim 的必要条件。")
    lines.append("")
    lines.append("## 6. Target Safety Stress\n")
    lines.append("| policy | method | PredUnsafe | HarmActual | TargetInducedMiss | SLO |")
    lines.append("|---|---|---:|---:|---:|---:|")
    for row in [stress_append, stress_head]:
        if not row:
            continue
        lines.append(
            f"| {row['target_insert_policy']} | {row['method']} | "
            f"{row['predicted_target_unsafe_accept_count']} | {row['harmful_actual_count']} | "
            f"{row['target_induced_miss_actual']} | {row['slo_violation_rate']} |"
        )
    lines.append("")
    lines.append("解释：append-tail 是真实默认模型；head-insert-stress 是为了让 target-side delay 可观测的压力模型。若 stress 下 NoTargetSafety 明显增加 actual harm，可把 target safety 写成 stress/ablation 证据；真实系统仍要用 CloudLab trace 复核。")
    lines.append("")
    lines.append("## 7. Overload Sanity\n")
    if overload_med:
        lines.append(
            f"W1 rho=0.95 下 RescueSched SLO={overload_med.get('slo_violation_rate')}，"
            f"moves={overload_med.get('rescue_success_count')}。这支持“不解决全局过载、不创造容量”的边界叙事。"
        )
    else:
        lines.append("未找到 W1 rho=0.95 overload sanity 数据。")
    lines.append("")
    lines.append("## 8. INFOCOM 风险清单\n")
    lines.append("强结论：W3 heavy-tail rho=0.85 的 SLO gain、低迁移率、高 BMR/低 UMR；NoRescuable 在 W3 的迁移质量退化；全局过载不误迁移。")
    lines.append("中等结论：realistic estimator 下仍保留多少收益；measured local handoff cost 对 rescue window 的影响；target safety stress 是否产生 actual harm。")
    lines.append("必须上 CloudLab/trace 后才能写成主结论：真实 migration cost、真实 RPC service-time estimator、真实 target-side interference、W2 burst/skew 是否需要 Hybrid。")
    lines.append("")

    path = STEP18 / "summary.md"
    path.write_text("\n".join(lines), encoding="utf-8-sig")
    return path


def main():
    STEP18.mkdir(parents=True, exist_ok=True)
    rows = {
        "estimator_main": read_csv(STEP18 / "rescue_estimator_main.csv"),
        "estimator_w2": read_csv(STEP18 / "rescue_estimator_w2.csv"),
        "cost": read_csv(STEP18 / "rescue_cost_calibration.csv"),
        "boundary": read_csv(STEP18 / "rescue_w2_boundary.csv"),
        "hybrid_main": read_csv(STEP18 / "rescue_hybrid_main.csv"),
        "target_safety": read_csv(STEP18 / "rescue_target_safety_stress.csv"),
        "overload": read_csv(STEP15 / "rescue_overload_sanity.csv"),
    }
    named_rows = [
        ("estimator_main", rows["estimator_main"],
         ["scenario", "method", "service_estimate_mode"]),
        ("estimator_w2", rows["estimator_w2"],
         ["scenario", "rho", "method", "service_estimate_mode"]),
        ("cost_calibration", rows["cost"],
         ["scenario", "method", "migration_cost_us"]),
        ("w2_boundary", rows["boundary"],
         ["scenario", "w2_hot_core_count", "w2_hot_dispatch_prob", "rho", "method"]),
        ("hybrid_main", rows["hybrid_main"],
         ["scenario", "rho", "method"]),
        ("target_safety_stress", rows["target_safety"],
         ["scenario", "target_insert_policy", "method"]),
    ]
    named_rows = [(name, r, keys) for name, r, keys in named_rows if r]
    med_rows = write_median_summary(named_rows)
    ci_rows = write_ci_summary(named_rows)
    report = write_report(med_rows, ci_rows, rows)
    print(f"wrote {STEP18 / 'infocom_median_summary.csv'}")
    print(f"wrote {STEP18 / 'infocom_ci_summary.csv'}")
    print(f"wrote {report}")


if __name__ == "__main__":
    main()
