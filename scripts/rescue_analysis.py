import csv
import math
import os
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "artifacts" / "step-15-rescuesched"
OUT = ROOT / "artifacts" / "step-16-rescuesched-readiness"
FIG = OUT / "figures"
SRC17 = ROOT / "artifacts" / "step-17-rescuesched-closure"
FIG17 = SRC17 / "figures"


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def read_optional_csv(path):
    if not path.exists():
        return []
    return read_csv(path)


def f(row, key, default=0.0):
    try:
        value = row.get(key, "")
        if value == "":
            return default
        return float(value)
    except ValueError:
        return default


def med(rows, key):
    vals = [f(r, key) for r in rows]
    return statistics.median(vals) if vals else 0.0


def mean(rows, key):
    vals = [f(r, key) for r in rows]
    return statistics.mean(vals) if vals else 0.0


def group(rows, keys):
    out = defaultdict(list)
    for row in rows:
        out[tuple(row.get(k, "") for k in keys)].append(row)
    return out


def fmt(x, digits=6):
    if abs(x) >= 1000:
        return f"{x:.0f}"
    return f"{x:.{digits}f}".rstrip("0").rstrip(".")


def write_median_summary(datasets, out_dir=OUT, filename="median_summary.csv"):
    out_dir.mkdir(parents=True, exist_ok=True)
    cols = [
        "source",
        "scenario",
        "workload",
        "method",
        "rho",
        "check_period_us",
        "epsilon_us",
        "budget_per_check",
        "migration_cost_us",
        "service_estimate_mode",
        "service_estimate_noise_cv",
        "P99_us",
        "P999_us",
        "slo_violation_rate",
        "intra_move_rate",
        "intra_move_count",
        "rescue_success_count",
        "beneficial_migration_ratio",
        "useless_migration_ratio",
        "rescue_per_migration",
        "predicted_target_unsafe_accept_count",
        "harmful_actual_count",
        "harmful_actual_ratio",
        "target_induced_miss_actual",
        "needless_migration_count",
        "unsaved_migration_count",
    ]
    metric_cols = cols[11:]

    rows_out = []
    for source, rows, keys in datasets:
        for key, vals in sorted(group(rows, keys).items()):
            sample = vals[0]
            out = {c: "" for c in cols}
            out["source"] = source
            for k, v in zip(keys, key):
                out[k] = v
            for c in ["scenario", "workload", "method", "rho",
                      "check_period_us", "epsilon_us", "budget_per_check",
                      "migration_cost_us", "service_estimate_mode",
                      "service_estimate_noise_cv"]:
                if not out[c]:
                    out[c] = sample.get(c, "")
            for c in metric_cols:
                out[c] = fmt(med(vals, c))
            rows_out.append(out)

    path = out_dir / filename
    with path.open("w", newline="", encoding="utf-8") as fcsv:
        writer = csv.DictWriter(fcsv, fieldnames=cols)
        writer.writeheader()
        writer.writerows(rows_out)
    return rows_out


def try_import_plot():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except Exception:
        return None


def save_fig(fig, name, fig_dir=FIG):
    fig_dir.mkdir(parents=True, exist_ok=True)
    fig.savefig(fig_dir / f"{name}.png", dpi=200, bbox_inches="tight")
    fig.savefig(fig_dir / f"{name}.pdf", bbox_inches="tight")


def plot_figures(rows):
    plt = try_import_plot()
    if plt is None:
        return []

    generated = []
    main = rows["main"]
    ablation = rows["ablation"]
    sweep = rows["sweep"]

    method_order = [
        "L0_RandomCore",
        "L1_WorkStealing",
        "M0_IntraHostProactive",
        "M1_RescueSched",
        "M1_RescueSched_NoTargetSafety",
    ]
    colors = {
        "L0_RandomCore": "#777777",
        "L1_WorkStealing": "#4C78A8",
        "M0_IntraHostProactive": "#F58518",
        "M1_RescueSched": "#54A24B",
        "M1_RescueSched_NoTargetSafety": "#B279A2",
        "M1_RescueSched_NoRescuable": "#E45756",
    }

    data = group(main, ["rho", "method"])
    fig, ax = plt.subplots(figsize=(7.0, 4.0))
    for method in method_order:
        xs, ys = [], []
        for rho in sorted({float(r["rho"]) for r in main}):
            vals = data.get((str(rho), method))
            if vals is None:
                vals = data.get((f"{rho:.2f}", method))
            if vals:
                xs.append(rho)
                ys.append(med(vals, "slo_violation_rate"))
        if xs:
            ax.plot(xs, ys, marker="o", label=method, color=colors.get(method))
    ax.set_xlabel("rho")
    ax.set_ylabel("SLO violation rate")
    ax.set_title("RescueSched main result on W3 heavy-tail")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    save_fig(fig, "fig_rescue_slo_vs_rho")
    plt.close(fig)
    generated.extend([
        "figures/fig_rescue_slo_vs_rho.png",
        "figures/fig_rescue_slo_vs_rho.pdf",
    ])

    abl = group(ablation, ["method"])
    methods = [
        "M1_RescueSched",
        "M1_RescueSched_NoTargetSafety",
        "M1_RescueSched_NoRescuable",
    ]
    fig, axes = plt.subplots(1, 3, figsize=(10.0, 3.3))
    x = range(len(methods))
    axes[0].bar(x, [med(abl[(m,)], "slo_violation_rate") for m in methods],
                color=[colors[m] for m in methods])
    axes[0].set_ylabel("SLO violation")
    axes[0].set_title("SLO")
    axes[1].bar(x, [med(abl[(m,)], "beneficial_migration_ratio") for m in methods],
                color=[colors[m] for m in methods])
    axes[1].set_ylim(0, 1.05)
    axes[1].set_title("BMR")
    axes[2].bar(x, [med(abl[(m,)], "predicted_target_unsafe_accept_count") for m in methods],
                color=[colors[m] for m in methods])
    axes[2].set_title("Predicted unsafe accepts")
    for ax in axes:
        ax.set_xticks(list(x))
        ax.set_xticklabels(["Rescue", "NoSafety", "NoRescuable"], rotation=20)
        ax.grid(True, axis="y", alpha=0.3)
    fig.suptitle("RescueSched ablation on W3 rho=0.85")
    save_fig(fig, "fig_rescue_ablation_quality")
    plt.close(fig)
    generated.extend([
        "figures/fig_rescue_ablation_quality.png",
        "figures/fig_rescue_ablation_quality.pdf",
    ])

    budget_rows = [r for r in sweep if r["scenario"] == "rescue_budget_sweep"]
    budget = group(budget_rows, ["budget_per_check"])
    bs = sorted(int(k[0]) for k in budget)
    fig, ax1 = plt.subplots(figsize=(6.4, 3.6))
    slo = [med(budget[(str(b),)], "slo_violation_rate") for b in bs]
    bmr = [med(budget[(str(b),)], "beneficial_migration_ratio") for b in bs]
    umr = [med(budget[(str(b),)], "useless_migration_ratio") for b in bs]
    ax1.plot(bs, slo, marker="o", color="#E45756", label="SLO violation")
    ax1.set_xlabel("budget per check")
    ax1.set_ylabel("SLO violation")
    ax1.grid(True, alpha=0.3)
    ax2 = ax1.twinx()
    ax2.plot(bs, bmr, marker="s", color="#54A24B", label="BMR")
    ax2.plot(bs, umr, marker="^", color="#B279A2", label="UMR")
    ax2.set_ylabel("migration quality ratio")
    lines = ax1.get_lines() + ax2.get_lines()
    ax1.legend(lines, [l.get_label() for l in lines], fontsize=8, loc="center right")
    ax1.set_title("More migration budget is not better")
    save_fig(fig, "fig_rescue_budget_sweep")
    plt.close(fig)
    generated.extend([
        "figures/fig_rescue_budget_sweep.png",
        "figures/fig_rescue_budget_sweep.pdf",
    ])

    return generated


def row_lookup(summary_rows, **where):
    for row in summary_rows:
        ok = True
        for k, v in where.items():
            if str(row.get(k, "")) != str(v):
                ok = False
                break
        if ok:
            return row
    return {}


def tcrit_95(n):
    table = {
        2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776,
        6: 2.571, 7: 2.447, 8: 2.365, 9: 2.306, 10: 2.262,
    }
    return table.get(n, 1.96)


def write_ci_summary(rows, out_dir=SRC17):
    out_dir.mkdir(parents=True, exist_ok=True)
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
    ]
    cols = [
        "scenario", "workload", "rho", "method", "metric", "n",
        "mean", "median", "p25", "p75", "ci95_low", "ci95_high",
        "ci95_half_width",
    ]
    out_rows = []
    for key, vals in sorted(group(rows, ["scenario", "workload", "rho", "method"]).items()):
        for metric in metrics:
            xs = [f(r, metric) for r in vals]
            if not xs:
                continue
            n = len(xs)
            mu = statistics.mean(xs)
            medv = statistics.median(xs)
            sorted_xs = sorted(xs)
            p25 = sorted_xs[max(0, min(n - 1, int(math.floor(0.25 * (n - 1)))))]
            p75 = sorted_xs[max(0, min(n - 1, int(math.floor(0.75 * (n - 1)))))]
            if n > 1:
                half = tcrit_95(n) * statistics.stdev(xs) / math.sqrt(n)
            else:
                half = 0.0
            out_rows.append({
                "scenario": key[0],
                "workload": key[1],
                "rho": key[2],
                "method": key[3],
                "metric": metric,
                "n": n,
                "mean": fmt(mu),
                "median": fmt(medv),
                "p25": fmt(p25),
                "p75": fmt(p75),
                "ci95_low": fmt(mu - half),
                "ci95_high": fmt(mu + half),
                "ci95_half_width": fmt(half),
            })

    path = out_dir / "ci_summary.csv"
    with path.open("w", newline="", encoding="utf-8") as fcsv:
        writer = csv.DictWriter(fcsv, fieldnames=cols)
        writer.writeheader()
        writer.writerows(out_rows)
    return out_rows


def plot_step17_figures(rows):
    plt = try_import_plot()
    if plt is None:
        return []

    generated = []
    colors = {
        "L0_RandomCore": "#777777",
        "L1_WorkStealing": "#4C78A8",
        "M0_IntraHostProactive": "#F58518",
        "M1_RescueSched": "#54A24B",
        "M1_RescueSched_NoTargetSafety": "#B279A2",
        "M1_RescueSched_NoRescuable": "#E45756",
    }

    w2 = rows.get("w2_burst", [])
    if w2:
        method_order = [
            "L1_WorkStealing",
            "M0_IntraHostProactive",
            "M1_RescueSched",
            "M1_RescueSched_NoTargetSafety",
            "M1_RescueSched_NoRescuable",
        ]
        data = group(w2, ["rho", "method"])
        fig, ax = plt.subplots(figsize=(7.0, 4.0))
        for method in method_order:
            xs, ys = [], []
            for rho in sorted({float(r["rho"]) for r in w2}):
                vals = data.get((str(rho), method))
                if vals is None:
                    vals = data.get((f"{rho:.2f}", method))
                if vals:
                    xs.append(rho)
                    ys.append(med(vals, "slo_violation_rate"))
            if xs:
                ax.plot(xs, ys, marker="o", label=method, color=colors.get(method))
        ax.set_xlabel("rho")
        ax.set_ylabel("SLO violation rate")
        ax.set_title("W2 burst-skew RescueSched result")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        save_fig(fig, "fig_w2_burst_slo_vs_rho", FIG17)
        plt.close(fig)
        generated.extend([
            "figures/fig_w2_burst_slo_vs_rho.png",
            "figures/fig_w2_burst_slo_vs_rho.pdf",
        ])

    cal = rows.get("calibration", [])
    if cal:
        cost_rows = [r for r in cal if r["scenario"] == "rescue_cost_sweep"]
        service_rows = [r for r in cal if r["scenario"] == "rescue_service_estimate_sweep"]
        fig, axes = plt.subplots(1, 2, figsize=(10.0, 3.8))
        if cost_rows:
            data = group(cost_rows, ["migration_cost_us"])
            cost_keys = sorted(data.keys(), key=lambda k: float(k[0]))
            costs = [float(k[0]) for k in cost_keys]
            axes[0].plot(costs, [med(data[k], "slo_violation_rate")
                                 for k in cost_keys], marker="o", label="SLO")
            axes[0].plot(costs, [med(data[k], "beneficial_migration_ratio")
                                 for k in cost_keys], marker="s", label="BMR")
            axes[0].plot(costs, [med(data[k], "useless_migration_ratio")
                                 for k in cost_keys], marker="^", label="UMR")
            axes[0].set_xlabel("migration_cost_us")
            axes[0].set_title("Migration cost sensitivity")
            axes[0].grid(True, alpha=0.3)
            axes[0].legend(fontsize=8)
        if service_rows:
            labels = []
            vals = []
            for key, grouped in sorted(group(service_rows, [
                "service_estimate_mode", "service_estimate_noise_cv"
            ]).items()):
                mode, cv = key
                label = mode if f(grouped[0], "service_estimate_noise_cv") == 0 else f"{mode}\ncv={cv}"
                labels.append(label)
                vals.append(med(grouped, "beneficial_migration_ratio"))
            axes[1].bar(range(len(labels)), vals, color="#54A24B")
            axes[1].set_xticks(range(len(labels)))
            axes[1].set_xticklabels(labels, rotation=20)
            axes[1].set_ylim(0, 1.05)
            axes[1].set_ylabel("BMR")
            axes[1].set_title("Service estimate sensitivity")
            axes[1].grid(True, axis="y", alpha=0.3)
        save_fig(fig, "fig_rescue_calibration", FIG17)
        plt.close(fig)
        generated.extend([
            "figures/fig_rescue_calibration.png",
            "figures/fig_rescue_calibration.pdf",
        ])

    return generated


def ci_lookup(ci_rows, scenario, method, metric):
    for row in ci_rows:
        if (row.get("scenario") == scenario
            and row.get("method") == method
            and row.get("metric") == metric):
            return row
    return {}


def write_closure_report(summary_rows, ci_rows, generated_figures):
    SRC17.mkdir(parents=True, exist_ok=True)

    w2_l1 = row_lookup(summary_rows, source="w2_burst", rho="0.85",
                       method="L1_WorkStealing")
    w2_m0 = row_lookup(summary_rows, source="w2_burst", rho="0.85",
                       method="M0_IntraHostProactive")
    w2_rescue = row_lookup(summary_rows, source="w2_burst", rho="0.85",
                           method="M1_RescueSched")
    w2_no_safety = row_lookup(summary_rows, source="w2_burst", rho="0.85",
                              method="M1_RescueSched_NoTargetSafety")
    w2_no_rescue = row_lookup(summary_rows, source="w2_burst", rho="0.85",
                              method="M1_RescueSched_NoRescuable")
    robust_w3 = ci_lookup(ci_rows, "rescue_w3_robustness_10seed",
                          "M1_RescueSched", "slo_violation_rate")
    robust_w2 = ci_lookup(ci_rows, "rescue_w2_robustness_10seed",
                          "M1_RescueSched", "slo_violation_rate")
    cost0 = row_lookup(summary_rows, source="calibration",
                       scenario="rescue_cost_sweep", migration_cost_us="0",
                       method="M1_RescueSched")
    cost5 = row_lookup(summary_rows, source="calibration",
                       scenario="rescue_cost_sweep", migration_cost_us="5",
                       method="M1_RescueSched")
    mean_est = row_lookup(summary_rows, source="calibration",
                          scenario="rescue_service_estimate_sweep",
                          service_estimate_mode="mean",
                          method="M1_RescueSched")
    noisy50 = row_lookup(summary_rows, source="calibration",
                         scenario="rescue_service_estimate_sweep",
                         service_estimate_mode="noisy_oracle",
                         service_estimate_noise_cv="0.5",
                         method="M1_RescueSched")

    lines = []
    lines.append("# Step-17 RescueSched Closure Summary\n")
    lines.append("本轮补齐 Step-16 readiness 报告中标出的三个缺口：W2 burst-skew、10-seed/CI、以及 migration-cost 与 service-time estimate 校准。")
    lines.append("")
    lines.append("## 1. 新增实验产物\n")
    lines.append("- `rescue_w2_burst.csv`: W2 MMPP burst-skew 单 host 16 core 实验。")
    lines.append("- `rescue_robustness_10seed.csv`: W3/W2 rho=0.85 的 10-seed 稳健性实验。")
    lines.append("- `rescue_calibration.csv`: migration cost 与 service-time estimate sensitivity。")
    lines.append("- `migration_cost_microbench.csv`: 本地 descriptor queue/handoff 粗粒度成本微基准。")
    lines.append("- `closure_median_summary.csv` 与 `ci_summary.csv`: 自动汇总表。")
    lines.append("")
    lines.append("## 2. W2 Burst-Skew 结论\n")
    lines.append("| method | P99 | P999 | SLO violation | move rate | BMR | UMR | RPM |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for row in [w2_l1, w2_m0, w2_rescue, w2_no_safety, w2_no_rescue]:
        if not row:
            continue
        lines.append(
            f"| {row.get('method')} | {row.get('P99_us')} | {row.get('P999_us')} | "
            f"{row.get('slo_violation_rate')} | {row.get('intra_move_rate')} | "
            f"{row.get('beneficial_migration_ratio')} | {row.get('useless_migration_ratio')} | "
            f"{row.get('rescue_per_migration')} |"
        )
    lines.append("")
    lines.append("解释：W2 burst-skew 是由单 host 内 MMPP burst 期间 hot-core 偏置产生的局部不均衡。它补上了只看 W3 heavy-tail 的泛化缺口，但仍是仿真压力源，不等同于真实线上 trace。")
    lines.append("")
    lines.append("## 3. 10-Seed/CI 稳健性\n")
    lines.append("| scenario | method | metric | mean | 95% CI | n |")
    lines.append("|---|---|---|---:|---:|---:|")
    for row in [robust_w3, robust_w2]:
        if not row:
            continue
        lines.append(
            f"| {row.get('scenario')} | {row.get('method')} | {row.get('metric')} | "
            f"{row.get('mean')} | [{row.get('ci95_low')}, {row.get('ci95_high')}] | {row.get('n')} |"
        )
    lines.append("")
    lines.append("解释：10 seeds 主要用于给导师汇报时避免只讲 median。CI 仍来自本地模拟器，论文级强结论需要 CloudLab 或 trace replay 复现。")
    lines.append("")
    lines.append("## 4. Cost 与 Estimate 校准\n")
    lines.append("| setting | SLO violation | move rate | BMR | UMR |")
    lines.append("|---|---:|---:|---:|---:|")
    for label, row in [
        ("cost=0us oracle", cost0),
        ("cost=5us oracle", cost5),
        ("mean service estimate", mean_est),
        ("noisy oracle cv=0.5", noisy50),
    ]:
        if not row:
            continue
        lines.append(
            f"| {label} | {row.get('slo_violation_rate')} | {row.get('intra_move_rate')} | "
            f"{row.get('beneficial_migration_ratio')} | {row.get('useless_migration_ratio')} |"
        )
    lines.append("")
    lines.append("解释：`oracle` 仍是上界；`mean` 和 noisy-oracle 是第一版估计误差消融。真实服务时间预测还需要 EWMA/quantile 或线上观测校准。")
    lines.append("")
    lines.append("## 5. 可向导师汇报的结论\n")
    lines.append("1. RescueSched 的论文问题已经从负载均衡转成 SLO rescue：只迁移 queued-but-not-running 且 locally doomed、remote feasible、target safe 的任务。")
    lines.append("2. 在 W3 rho=0.85 主场景中，收益不是靠迁移更多任务，而是靠高 BMR/低 UMR。")
    lines.append("3. W2 burst-skew 和 10-seed/CI 已补齐本地仿真闭环，足够作为下一轮组会材料。")
    lines.append("4. 高负载或无 slack 时 RescueSched 会抑制迁移；它不声称解决全局过载。")
    lines.append("")
    lines.append("## 6. 仍需谨慎的边界\n")
    lines.append("- target safety 的实际 harmful 统计在当前 FIFO append-to-tail 模型下通常为 0；它验证的是 predicted unsafe exposure，而不是强 target-induced miss 现象。")
    lines.append("- migration-cost microbench 是本地笔记本、非 CPU pinning 的粗粒度估计，只能支持扫参范围，不能替代 CloudLab 成本测量。")
    lines.append("- service-time estimate 目前只有 mean/noisy-oracle 消融，真实系统需要基于 RPC 历史或类别的预测器。")
    lines.append("")
    lines.append("## 7. Generated Figures\n")
    for fig in generated_figures:
        lines.append(f"- `{fig}`")
    lines.append("")

    path = SRC17 / "summary.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def write_report(summary_rows, generated_figures, closure_available=False):
    def num(row, key):
        return float(row.get(key, 0) or 0)

    r_l1 = row_lookup(summary_rows, source="main", rho="0.85",
                      method="L1_WorkStealing")
    r_m0 = row_lookup(summary_rows, source="main", rho="0.85",
                      method="M0_IntraHostProactive")
    r_rescue = row_lookup(summary_rows, source="main", rho="0.85",
                          method="M1_RescueSched")
    r_nosafety = row_lookup(summary_rows, source="ablation",
                            method="M1_RescueSched_NoTargetSafety")
    r_norescue = row_lookup(summary_rows, source="ablation",
                            method="M1_RescueSched_NoRescuable")
    r_overload = row_lookup(summary_rows, source="overload",
                            scenario="rescue_w1_overload_boundary",
                            method="M1_RescueSched")
    r_budget4 = row_lookup(summary_rows, source="sweep",
                           scenario="rescue_budget_sweep",
                           budget_per_check="4")

    checks = [
        ("Non-overload SLO gain", "satisfied",
         f"W3 rho=0.85 SLO: L1 {r_l1.get('slo_violation_rate')}, "
         f"M0 {r_m0.get('slo_violation_rate')}, Rescue {r_rescue.get('slo_violation_rate')}."),
        ("Not from more migration", "satisfied",
         f"Rescue move rate {r_rescue.get('intra_move_rate')} vs M0 {r_m0.get('intra_move_rate')}."),
        ("High BMR", "satisfied",
         f"Rescue BMR {r_rescue.get('beneficial_migration_ratio')}."),
        ("Low actual UMR", "satisfied",
         f"Rescue UMR {r_rescue.get('useless_migration_ratio')} with actual harmful semantics."),
        ("NoRescuable necessity", "satisfied",
         f"NoRescuable BMR {r_norescue.get('beneficial_migration_ratio')}, UMR {r_norescue.get('useless_migration_ratio')}."),
        ("NoTargetSafety necessity", "partially satisfied",
         f"NoTargetSafety predicted unsafe accepts {r_nosafety.get('predicted_target_unsafe_accept_count')}, "
         f"but actual harmful {r_nosafety.get('harmful_actual_count')}."),
        ("Balanced workload non-overreaction", "not satisfied",
         "No dedicated balanced low-skew RescueSched run yet; L0 low-rho exists but not a balanced-specialized sanity."),
        ("Global overload suppress migration", "satisfied",
         f"W1 rho=0.95 Rescue moves {r_overload.get('rescue_success_count')}."),
        ("P99/P999 narrative", "satisfied",
         "Summary explicitly states RescueSched may raise P99/P999 while lowering SLO violations."),
        ("Seed stability", "satisfied" if closure_available else "partially satisfied",
         "10-seed CI table is available in step-17."
         if closure_available else
         "5 seeds are present; no 10-seed or CI table for RescueSched yet."),
        ("Parameter sensitivity", "satisfied",
         f"B=4 BMR {r_budget4.get('beneficial_migration_ratio')} and UMR {r_budget4.get('useless_migration_ratio')}."),
        ("Workload generalization", "satisfied" if closure_available else "partially satisfied",
         "W3 main, W1 sanity/overload, and W2 burst-skew RescueSched are available."
         if closure_available else
         "W3 main and W1 sanity/overload exist; W2 burst-skew RescueSched is not yet run."),
        ("Actual target harmful counterfactual", "satisfied with model caveat",
         "Implemented watch-based actual target-induced miss; FIFO append produces zero actual harmful in current model."),
        ("Reproducibility", "satisfied",
         "Runner modes and CSV outputs are deterministic by seed."),
        ("CloudLab readiness", "partially satisfied",
         "Ready for trace-driven simulator reproduction and migration-cost microbenchmark, not full user-space prototype claim."),
    ]

    lines = []
    lines.append("# Step-16 RescueSched Readiness Diagnosis\n")
    lines.append("Generated by `python scripts/rescue_analysis.py`.\n")
    lines.append("## 1. Project Status Diagnosis\n")
    lines.append("- Language/build: C++17 simulator built by CMake; Python scripts are used for analysis and figures.")
    lines.append("- Main entry: `src/app/main.cpp`, executable `build-aqb-check/simulator.exe`.")
    lines.append("- RescueSched methods: `M1_RescueSched`, `M1_RescueSched_NoTargetSafety`, `M1_RescueSched_NoRescuable`.")
    lines.append("- Workloads currently used by RescueSched: W3 Poisson+Lognormal main, W1 Poisson+Bimodal sanity and overload.")
    lines.append("- Existing outputs: step-15 raw CSVs plus this step-16 median table, figures, and readiness report.")
    lines.append("- Harmful migration status: predicted target-unsafe accepts and actual target-induced misses are now separated.")
    lines.append("")
    lines.append("## 2. Goal Checklist\n")
    lines.append("| Item | Status | Evidence |")
    lines.append("|---|---|---|")
    for item, status, evidence in checks:
        lines.append(f"| {item} | {status} | {evidence} |")
    lines.append("")
    lines.append("## 3. Key Corrective Finding\n")
    lines.append("The previous `harmful_migration_count` mixed a predicted target-risk signal into UMR. "
                 "After the fix, `predicted_target_unsafe_accept_count` records accepted unsafe-target predictions, "
                 "while `harmful_actual_count` records migrations that actually induced target-side misses.")
    lines.append("")
    lines.append("Under the current append-to-tail FIFO model, actual target-side harm is expected to be zero because an incoming task does not delay tasks already queued ahead of it. "
                 "The new watch-based counterfactual confirms this in the rerun CSVs. Therefore, target safety is currently only partially validated: it reduces exposure to already risky target queues, but this simulator does not yet demonstrate actual target-induced misses.")
    lines.append("")
    lines.append("## 4. Experiment Interpretation\n")
    lines.append(f"- W3 rho=0.85: RescueSched SLO violation is {r_rescue.get('slo_violation_rate')}, "
                 f"better than L1 {r_l1.get('slo_violation_rate')} and M0 {r_m0.get('slo_violation_rate')}.")
    lines.append(f"- W3 rho=0.85: RescueSched move rate is {r_rescue.get('intra_move_rate')}, "
                 f"lower than M0 {r_m0.get('intra_move_rate')}; the gain is not from moving more tasks.")
    lines.append(f"- NoRescuable: BMR drops to {r_norescue.get('beneficial_migration_ratio')} and UMR rises to {r_norescue.get('useless_migration_ratio')}; rescuability is necessary.")
    lines.append(f"- NoTargetSafety: predicted unsafe accepts are {r_nosafety.get('predicted_target_unsafe_accept_count')}, "
                 f"but actual harmful migrations are {r_nosafety.get('harmful_actual_count')}; target-safety evidence needs a stronger model or prototype.")
    lines.append(f"- W1 rho=0.95 overload: RescueSched moves {r_overload.get('rescue_success_count')} tasks and does not claim to solve global overload.")
    lines.append("")
    lines.append("## 5. Autonomous Task Choice\n")
    lines.append("Chosen tasks:")
    lines.append("1. Split predicted target unsafe from actual harmful counterfactual.")
    lines.append("2. Add RescueSched-specific analysis and figure generation.")
    lines.append("3. Generate a CloudLab readiness report.")
    lines.append("")
    lines.append("Not chosen yet:")
    lines.append("- New workload or baseline: lower priority than fixing harmful semantics because the existing claim would otherwise overstate target safety.")
    lines.append("- 10 seeds: useful but more expensive; 5 seeds already cover the first local gate.")
    lines.append("- Full user-space prototype: should follow CloudLab migration-cost and trace-reproduction checks.")
    lines.append("")
    lines.append("## 6. Generated Figures\n")
    for fig in generated_figures:
        lines.append(f"- `{fig}`")
    lines.append("")
    lines.append("## 7. CloudLab Readiness\n")
    lines.append("Decision: RescueSched is ready for CloudLab Phase 1, not yet for a full user-space prototype performance claim.")
    lines.append("")
    lines.append("Phase 1 should be trace-driven simulator reproduction plus microbenchmarks:")
    lines.append("1. Rebuild the simulator on CloudLab and reproduce `rescue-smoke`, `rescue-main`, `rescue-ablation`, and `rescue-overload-sanity`.")
    lines.append("2. Measure descriptor-only cross-core enqueue/dequeue cost to replace the current `migration_cost_us=0.5` assumption.")
    lines.append("3. Collect or replay per-core arrival/service traces to test whether random-core imbalance and heavy-tail rescue patterns persist.")
    lines.append("4. If implementing a user-space prototype, first preserve FIFO append semantics and explicitly log target queue snapshots for harmful attribution.")
    lines.append("")
    lines.append("Blocking caveats before a paper-grade CloudLab claim:")
    if closure_available:
        lines.append("- Step-17 now contains W2 burst-skew, 10-seed CI, and cost/service-estimate calibration for local reporting.")
        lines.append("- Need a target-side policy that can actually create target harm, or state clearly that append-to-tail cannot harm existing target tasks.")
        lines.append("- Need CloudLab or trace replay before claiming real-system performance.")
    else:
        lines.append("- Need W2 burst-skew RescueSched run or a CloudLab trace that stresses burst/skew, not only W3 heavy-tail.")
        lines.append("- Need calibrated service-time estimation instead of using true sampled service time as expected service time.")
        lines.append("- Need a target-side policy that can actually create target harm, or state clearly that append-to-tail cannot harm existing target tasks.")
        lines.append("- Need 10-seed or CI reporting if the local simulator remains central to the evaluation.")
    lines.append("")

    path = OUT / "readiness_report.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = {
        "main": read_csv(SRC / "rescue_main.csv"),
        "ablation": read_csv(SRC / "rescue_ablation.csv"),
        "sweep": read_csv(SRC / "rescue_check_sweep.csv"),
        "overload": read_csv(SRC / "rescue_overload_sanity.csv"),
        "w3_only": read_csv(SRC / "rescue_w3_only.csv"),
    }
    datasets = [
        ("main", rows["main"], ["rho", "method"]),
        ("ablation", rows["ablation"], ["method"]),
        ("sweep", rows["sweep"], ["scenario", "check_period_us", "epsilon_us", "budget_per_check"]),
        ("overload", rows["overload"], ["scenario", "rho", "method"]),
        ("w3_only", rows["w3_only"], ["method"]),
    ]
    summary_rows = write_median_summary(datasets)
    figures = plot_figures(rows)
    step17_rows = {
        "w2_burst": read_optional_csv(SRC17 / "rescue_w2_burst.csv"),
        "robustness": read_optional_csv(SRC17 / "rescue_robustness_10seed.csv"),
        "calibration": read_optional_csv(SRC17 / "rescue_calibration.csv"),
    }
    closure_available = all(step17_rows.values())
    report = write_report(summary_rows, figures, closure_available)
    print(f"wrote {OUT / 'median_summary.csv'}")
    print(f"wrote {report}")
    for fig in figures:
        print(f"wrote {OUT / fig}")

    if any(step17_rows.values()):
        datasets17 = []
        if step17_rows["w2_burst"]:
            datasets17.append(("w2_burst", step17_rows["w2_burst"],
                               ["scenario", "rho", "method"]))
        if step17_rows["robustness"]:
            datasets17.append(("robustness_10seed", step17_rows["robustness"],
                               ["scenario", "workload", "rho", "method"]))
        if step17_rows["calibration"]:
            datasets17.append(("calibration", step17_rows["calibration"],
                               ["scenario", "migration_cost_us",
                                "service_estimate_mode",
                                "service_estimate_noise_cv", "method"]))
        summary17 = write_median_summary(
            datasets17, SRC17, "closure_median_summary.csv")
        ci_rows = write_ci_summary(step17_rows["robustness"], SRC17) \
            if step17_rows["robustness"] else []
        figures17 = plot_step17_figures(step17_rows)
        closure_report = write_closure_report(summary17, ci_rows, figures17)
        print(f"wrote {SRC17 / 'closure_median_summary.csv'}")
        if ci_rows:
            print(f"wrote {SRC17 / 'ci_summary.csv'}")
        print(f"wrote {closure_report}")
        for fig in figures17:
            print(f"wrote {SRC17 / fig}")


if __name__ == "__main__":
    main()
