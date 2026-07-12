#!/usr/bin/env python3
"""Generate paper-facing figures from corrected full RescueSched CSVs."""

import argparse
import csv
import pathlib
import random
import statistics

import matplotlib.pyplot as plt


METHODS = [
    "L1_WorkStealingPolling",
    "M0_AltoThreshold",
    "M1_RescueSched",
]
LABELS = {
    "L1_WorkStealingPolling": "Polling work stealing",
    "M0_AltoThreshold": "ALTO-style threshold",
    "M1_RescueSched": "RescueSched",
}
COLORS = {
    "L1_WorkStealingPolling": "#0072B2",
    "M0_AltoThreshold": "#E69F00",
    "M1_RescueSched": "#009E73",
}
MARKERS = {
    "L1_WorkStealingPolling": "o",
    "M0_AltoThreshold": "s",
    "M1_RescueSched": "^",
}


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if {row.get("schema_version") for row in rows} != {"rescuesched-v2"}:
        raise ValueError(f"{path} is not rescuesched-v2")
    return rows


def value(row: dict[str, str], key: str) -> float:
    return float(row[key])


def bootstrap_mean_ci(values: list[float]) -> tuple[float, float, float]:
    rng = random.Random(20260712)
    means = []
    for _ in range(10000):
        sample = [values[rng.randrange(len(values))] for _ in values]
        means.append(statistics.mean(sample))
    means.sort()
    return statistics.mean(values), means[249], means[9749]


def save(figure: plt.Figure, out_dir: pathlib.Path, stem: str) -> None:
    figure.tight_layout()
    figure.savefig(out_dir / f"{stem}.pdf", bbox_inches="tight")
    figure.savefig(out_dir / f"{stem}.png", dpi=220, bbox_inches="tight")
    plt.close(figure)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=pathlib.Path, required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    w3 = read_csv(args.input_dir / "w3.csv")
    w2 = read_csv(args.input_dir / "w2.csv")

    plt.rcParams.update({
        "font.size": 9,
        "axes.labelsize": 9,
        "axes.titlesize": 10,
        "legend.fontsize": 8,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
    })

    rhos = sorted({value(row, "rho") for row in w3})
    fig, ax = plt.subplots(figsize=(4.1, 2.7))
    for method in METHODS:
        means, lows, highs = [], [], []
        for rho in rhos:
            samples = [100.0 * value(row, "slo_violation_rate") for row in w3
                       if row["method"] == method and value(row, "rho") == rho]
            mean, low, high = bootstrap_mean_ci(samples)
            means.append(mean); lows.append(mean - low); highs.append(high - mean)
        ax.errorbar(rhos, means, yerr=[lows, highs], label=LABELS[method],
                    color=COLORS[method], marker=MARKERS[method], linewidth=1.6,
                    markersize=5, capsize=2.5)
    ax.set_xlabel("Offered load $\\rho$")
    ax.set_ylabel("Deadline violations (%)")
    ax.set_title("W3 heavy-tail: deadline outcome")
    ax.grid(True, alpha=0.25)
    ax.legend(frameon=False)
    save(fig, args.out_dir, "fig_w3_deadline_violation")

    fig, ax = plt.subplots(figsize=(4.1, 2.7))
    for method in METHODS:
        medians = []
        for rho in rhos:
            samples = [
                100.0 * value(row, "intra_moved_work_us")
                / value(row, "measured_generated_work_us")
                for row in w3
                if row["method"] == method and value(row, "rho") == rho
            ]
            medians.append(statistics.median(samples))
        ax.plot(rhos, medians, label=LABELS[method], color=COLORS[method],
                marker=MARKERS[method], linewidth=1.6, markersize=5)
    ax.set_xlabel("Offered load $\\rho$")
    ax.set_ylabel("Migrated work (%)")
    ax.set_title("W3 heavy-tail: queue-repair work")
    ax.grid(True, alpha=0.25)
    ax.legend(frameon=False)
    save(fig, args.out_dir, "fig_w3_migrated_work")

    target = [row for row in w2 if value(row, "rho") == 0.85]
    violation = [100.0 * statistics.median(
        value(row, "slo_violation_rate") for row in target if row["method"] == method)
        for method in METHODS]
    p99 = [statistics.median(
        value(row, "P99_us") for row in target if row["method"] == method)
        for method in METHODS]
    fig, axes = plt.subplots(1, 2, figsize=(6.7, 2.65))
    x = range(len(METHODS))
    colors = [COLORS[method] for method in METHODS]
    labels = ["Polling WS", "ALTO-style", "RescueSched"]
    axes[0].bar(x, violation, color=colors)
    axes[0].set_xticks(list(x), labels, rotation=18, ha="right")
    axes[0].set_ylabel("Deadline violations (%)")
    axes[0].set_title("Deadline outcome")
    axes[0].grid(True, axis="y", alpha=0.25)
    axes[1].bar(x, p99, color=colors)
    axes[1].set_xticks(list(x), labels, rotation=18, ha="right")
    axes[1].set_ylabel("P99 latency ($\\mu$s)")
    axes[1].set_yscale("log")
    axes[1].set_title("Unconditional tail")
    axes[1].grid(True, axis="y", alpha=0.25)
    fig.suptitle("W2 burst boundary at $\\rho=0.85$", y=1.02, fontsize=10)
    save(fig, args.out_dir, "fig_w2_deadline_tail_tradeoff")

    print(f"generated corrected evaluation figures -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
