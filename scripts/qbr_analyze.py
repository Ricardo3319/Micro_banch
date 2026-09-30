#!/usr/bin/env python3
"""Generate paired bootstrap tables and standalone PDF/PNG research figures."""
import argparse
import collections
import csv
import json
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path(__file__).resolve().parents[1] / ".mplconfig"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

METHODS = ["jsw", "p2c", "jbsq", "steal", "threshold", "single", "qbr", "work", "bwc"]
COLORS = {"jsw": "#666666", "p2c": "#aaaaaa", "jbsq": "#2166ac", "steal": "#1b9e77",
          "threshold": "#7570b3", "single": "#a6761d", "qbr": "#d95f02", "work": "#e7298a", "bwc": "#cc79a7"}
NAMES = {"steady": "Steady", "capacity": "Capacity loss", "stall": "Single-host stall",
         "burst": "Global bursts", "heavy": "Heavy-tailed service"}


def ci(values):
    a = np.array(values, dtype=float)
    if len(a) < 2:
        return float(a.mean()), float(a.mean()), float(a.mean())
    rng = np.random.default_rng(20260926)
    means = a[rng.integers(0, len(a), size=(5000, len(a)))].mean(axis=1)
    return float(a.mean()), float(np.quantile(means, .025)), float(np.quantile(means, .975))


def paired(rows):
    groups = collections.defaultdict(dict)
    for r in rows:
        groups[r["group"]][r["variant"]] = r
    differences = collections.defaultdict(list)
    for values in groups.values():
        if len({r["trace_sha256"] for r in values.values()}) != 1:
            raise RuntimeError("unpaired trace hashes")
        if any(r["completed"] != r["generated"] for r in values.values()):
            raise RuntimeError("incomplete drain")
        for method, r in values.items():
            for reference in ("jsw", "jbsq", "steal", "threshold", "single", "qbr"):
                if reference not in values or method == reference:
                    continue
                b = values[reference]
                for metric in ("slo_miss_rate", "p99_us", "p999_us", "class0_p99_us", "control_cpu_mean"):
                    scale = 100 if metric in ("slo_miss_rate", "control_cpu_mean") else 1
                    key = (r["scenario"], r["rho"], r["axis"], r["value"], method, reference, metric)
                    differences[key].append((r[metric] - b[metric]) * scale)
    output = []
    for key, values in sorted(differences.items(), key=lambda kv: str(kv[0])):
        mean, lo, hi = ci(values)
        row = dict(zip(("scenario", "rho", "axis", "value", "method", "reference", "metric"), key))
        row.update(n=len(values), mean_difference=mean, ci95_low=lo, ci95_high=hi,
                   interpretation="lower" if hi < 0 else ("higher" if lo > 0 else "uncertain"))
        output.append(row)
    return output


def write_csv(path, rows):
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, list(rows[0])); w.writeheader(); w.writerows(rows)


def save(fig, directory, name):
    fig.savefig(directory / (name + ".pdf"), bbox_inches="tight")
    fig.savefig(directory / (name + ".png"), dpi=180, bbox_inches="tight")
    plt.close(fig)


def plots(rows, directory):
    plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False,
                         "pdf.fonttype": 42, "axes.grid": True, "grid.alpha": .2})
    for metric, ylabel, name in (("slo_miss_rate", "SLO violations (%)", "slo_by_load"),
                                 ("class0_p99_us", "Short-class P99 (us)", "short_tail_by_load")):
        fig, axes = plt.subplots(1, 5, figsize=(17, 3.5), sharey=False)
        for ax, scenario in zip(axes, NAMES):
            for method in ("jsw", "jbsq", "steal", "threshold", "qbr"):
                xs, ys, lows, highs = [], [], [], []
                for rho in sorted({r["rho"] for r in rows}):
                    a = [r[metric] * (100 if metric == "slo_miss_rate" else 1)
                         for r in rows if r["scenario"] == scenario and r["rho"] == rho and r["variant"] == method]
                    if not a:
                        continue
                    m, lo, hi = ci(a); xs.append(rho); ys.append(m); lows.append(lo); highs.append(hi)
                ax.plot(xs, ys, "o-", label=method.upper(), color=COLORS[method], markersize=3)
                ax.fill_between(xs, lows, highs, color=COLORS[method], alpha=.12)
            ax.set_title(NAMES[scenario]); ax.set_xlabel("Nominal load (rho)")
        axes[0].set_ylabel(ylabel)
        axes[-1].legend(fontsize=8, loc="upper left")
        fig.suptitle("Frozen traces; 10 seeds; shaded 95% bootstrap intervals", y=1.04)
        fig.tight_layout(); save(fig, directory, name)
    fig, axes = plt.subplots(1, 3, figsize=(13, 3.5))
    scenario = "stall"
    for ax, metric, title in zip(axes, ("migrated_fraction", "mean_batch", "control_cpu_mean"),
                                 ("Transferred requests (%)", "Requests per transfer batch", "Mean controller CPU (%)")):
        for method in ("steal", "threshold", "single", "qbr"):
            xs, ys = [], []
            for rho in (.5, .7, .85, .95):
                values = [r[metric] * (1 if metric == "mean_batch" else 100) for r in rows
                          if r["scenario"] == scenario and r["rho"] == rho and r["variant"] == method]
                xs.append(rho); ys.append(np.mean(values))
            ax.plot(xs, ys, "o-", label=method.upper(), color=COLORS[method], markersize=3)
        ax.set_title(title); ax.set_xlabel("Nominal load (rho)")
    axes[-1].legend(fontsize=8); fig.tight_layout(); save(fig, directory, "migration_costs")
    # The rejected work-credit candidates remain visible in a dedicated figure.
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.5))
    for ax, scenario in zip(axes, ("steady", "capacity", "stall")):
        for method in ("jbsq", "work", "bwc"):
            xs, ys = [], []
            for rho in (.5, .7, .85, .95):
                a = [r["slo_miss_rate"] * 100 for r in rows if r["scenario"] == scenario and r["rho"] == rho and r["variant"] == method]
                xs.append(rho); ys.append(np.mean(a))
            ax.plot(xs, ys, "o-", label=method.upper(), color=COLORS[method], markersize=3)
        ax.set_title(NAMES[scenario]); ax.set_xlabel("Nominal load (rho)")
    axes[0].set_ylabel("SLO violations (%)"); axes[-1].legend(); fig.tight_layout()
    save(fig, directory, "work_credit_negative_result")


def sensitivity_plots(rows, differences, directory):
    axes_names = ["network-us", "report-us", "decision-cpu-us", "max-batch", "hosts", "bandwidth-gbps", "ingress-cpu-us"]
    fig, axes = plt.subplots(2, 4, figsize=(15, 7)); axes = axes.ravel()
    for ax, axis in zip(axes, axes_names):
        for scenario, style in (("capacity", "s-"), ("stall", "o-")):
            selected = sorted((r for r in differences if r["axis"] == axis and r["method"] == "qbr"
                              and r["reference"] == "jsw" and r["metric"] == "slo_miss_rate"
                              and r["scenario"] == scenario), key=lambda r: float(r["value"]))
            xs = [float(r["value"]) for r in selected]; ys = [r["mean_difference"] for r in selected]
            errors = [[r["mean_difference"] - r["ci95_low"] for r in selected],
                      [r["ci95_high"] - r["mean_difference"] for r in selected]]
            ax.errorbar(xs, ys, yerr=errors, fmt=style, capsize=2, label=NAMES[scenario], markersize=3)
        ax.axhline(0, color="black", linewidth=.7); ax.set_xlabel(axis); ax.set_ylabel("QBR - JSW SLO (pp)")
    axes[0].legend(fontsize=8); axes[-1].axis("off")
    fig.suptitle("Sensitivity at rho = 0.85; negative values favor QBR", y=1.01)
    fig.tight_layout(); save(fig, directory, "sensitivity")


def report(rows, differences, directory, sens_rows, sens_diff):
    index = {(r["scenario"], r["rho"], r["method"], r["reference"], r["metric"]): r for r in differences}
    def mean(s, rho, m, key):
        return float(np.mean([r[key] for r in rows if r["scenario"] == s and r["rho"] == rho and r["variant"] == m]))
    lines = ["# 跨主机微秒任务调度：冻结实验结果", "", "生成于 2026-09-26。该报告由原始 JSON 自动生成，范围为策略级离散事件仿真。", "",
             f"主实验共 **{len(rows)} 次运行**：5 类场景 × 4 档负载 × 9 种方法 × 10 个种子。另有 {len(sens_rows)} 次敏感性/消融运行。", "",
             "## 可信度与口径", "", "- 每组方法的输入 SHA-256 一致；所有生成请求完成并排空。预热 5 ms，统计到达窗口 20 ms。",
             "- 16 主机 × 8 执行核心，每主机额外一个控制核心，入口一个控制核心。默认单程 3.15 μs、25 Gbps、主机开销 2.1 μs。",
             "- 服务类别均值 5/100 μs，概率 80%/20%；SLO 40/200 μs。高方差场景只改变类内方差。调度器不读取真实服务时长。",
             "- 负载按含 2.1 μs 开销的 26.1 μs 平均工作量归一化；全局突发另乘 1.5。容量下降阶段剩余能力为 81.25%，因此 rho=0.85/0.95 含真实过载。",
             "- 单主机短暂停顿每 1 ms 持续 200 μs，速度降为 2%，机架剩余能力为 93.875%；rho≤0.85 时仍有总量余裕。",
             "- 区间是同轨迹、按种子配对的均值差 bootstrap 95% 区间（5000 重采样）。比较很多，未校正多重检验，不能仅凭少数显著单元格宣称普遍优越。",
             "- P99/P99.9 来自无固定上界的对数直方图，桶宽约为 0.1%×(延迟+1 μs)。P99.9 为探索指标，20 ms 窗口不足以证明稀有事件稳定性。", "",
             "## 主实验：SLO 违约率与配对差值", "", "差值单位为百分点，负值表示 QBR 更好。", "",
             "| 场景 | 负载 | JSW % | JBSQ % | QBR % | QBR−JSW [95% CI] | QBR−JBSQ [95% CI] |",
             "| --- | ---: | ---: | ---: | ---: | --- | --- |"]
    for s in NAMES:
        for rho in (.5, .7, .85, .95):
            a = index[(s, rho, "qbr", "jsw", "slo_miss_rate")]
            b = index[(s, rho, "qbr", "jbsq", "slo_miss_rate")]
            fmt = lambda d: f"{d['mean_difference']:+.3f} [{d['ci95_low']:+.3f}, {d['ci95_high']:+.3f}]"
            lines.append(f"| {s} | {rho:.2f} | {100*mean(s,rho,'jsw','slo_miss_rate'):.3f} | {100*mean(s,rho,'jbsq','slo_miss_rate'):.3f} | {100*mean(s,rho,'qbr','slo_miss_rate'):.3f} | {fmt(a)} | {fmt(b)} |")
    lines.extend(["", "## 迁移机制是否有足够作用空间", "",
                  "| 场景（rho=0.85） | QBR 迁移占比 % | 平均批次 | 无改善迁移 % | QBR 控制 CPU % | JSW 控制 CPU % |",
                  "| --- | ---: | ---: | ---: | ---: | ---: |"])
    for s in NAMES:
        invalid = [r["invalid_migration_rate"] for r in rows if r["scenario"] == s and r["rho"] == .85 and r["variant"] == "qbr" and r["invalid_migration_rate"] is not None]
        lines.append(f"| {s} | {100*mean(s,.85,'qbr','migrated_fraction'):.3f} | {mean(s,.85,'qbr','mean_batch'):.2f} | {100*np.mean(invalid):.2f} | {100*mean(s,.85,'qbr','control_cpu_mean'):.3f} | {100*mean(s,.85,'jsw','control_cpu_mean'):.3f} |")
    lines.extend(["", "无改善迁移比例：同一请求在 QBR 的延迟不低于 JSW 运行时延迟。它包含其他请求重新分布产生的干扰，不是孤立迁移的因果估计。平均批次按种子等权；零迁移种子的批次计 0。", "",
                  "## 不应隐藏的反例", "", "`work` 为请求数/工作量双约束入口，`bwc` 再加入完成额度合并反馈。保留其全部结果。", "",
                  "| 场景（rho=0.85） | JBSQ 违约 % | work 违约 % | bwc 违约 % |",
                  "| --- | ---: | ---: | ---: |"])
    for s in NAMES:
        lines.append(f"| {s} | {100*mean(s,.85,'jbsq','slo_miss_rate'):.3f} | {100*mean(s,.85,'work','slo_miss_rate'):.3f} | {100*mean(s,.85,'bwc','slo_miss_rate'):.3f} |")
    lines.extend(["", "该实现的工作量上限会保守地限制并发，合并反馈又延迟了额度归还；这是一种具体设计失败，不能据此否定所有工作量准入或批量信用方案。", "",
                  "## 产物", "", "- `results.csv` / `results.json` / `raw/`：每次运行完整指标与命令。",
                  "- `paired_comparisons.csv`：全部配对差值，包括 P99、P99.9 和短类 P99。",
                  "- `manifest.json`：模型、可执行文件、生成器 SHA-256、种子和配置；`traces/`：冻结的实际输入。",
                  "- `figures/`：可独立导出的 PDF/PNG。首个正式种子的每请求延迟保留为 `.outcomes.gz`。", "",
                  "## 结论边界", "", "这里验证的是明确成本假设下的策略行为。尚未复现论文完整数据面、操作系统和硬件；没有真实多主机/RDMA/SmartNIC 测量。有限候选只约束迁移控制，JSW 入口仍扫描全部主机。不能用这些仿真图直接声称系统吞吐、SmartNIC 可实现性或已具备投稿条件。"])
    if sens_diff:
        lines.extend(["", "## 敏感性与消融", "", "详见 `sensitivity_paired.csv` 和 `figures/sensitivity.pdf`。网络、反馈周期、决策 CPU、批次上限、主机数、链路带宽和入口 CPU 分别变化；其余使用默认值。",
                      "", "| 场景 | 变化 | QBR 相对对应 JSW 的违约百分点 [95% CI] |", "| --- | --- | --- |"])
        for r in sens_diff:
            if r["method"] == "qbr" and r["reference"] == "jsw" and r["metric"] == "slo_miss_rate" and r["axis"] in ("network-us", "report-us"):
                lines.append(f"| {r['scenario']} | {r['axis']}={r['value']} | {r['mean_difference']:+.3f} [{r['ci95_low']:+.3f}, {r['ci95_high']:+.3f}] |")
    (directory / "研究结果.md").write_text("\n".join(lines) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument("directory"); p.add_argument("--sensitivity")
    a = p.parse_args(); directory = Path(a.directory)
    rows = json.loads((directory / "results.json").read_text()); differences = paired(rows)
    write_csv(directory / "paired_comparisons.csv", differences)
    figs = directory / "figures"; figs.mkdir(exist_ok=True); plots(rows, figs)
    sens_rows, sens_diff = [], []
    if a.sensitivity:
        sens_rows = json.loads((Path(a.sensitivity) / "results.json").read_text()); sens_diff = paired(sens_rows)
        write_csv(directory / "sensitivity_paired.csv", sens_diff)
        sensitivity_plots(sens_rows, sens_diff, figs)
    report(rows, differences, directory, sens_rows, sens_diff)
    print(f"Generated paired tables, report and figures in {directory}")


if __name__ == "__main__":
    main()
