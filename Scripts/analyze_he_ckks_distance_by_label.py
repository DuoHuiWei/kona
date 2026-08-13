#!/usr/bin/env python3
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path("/home/u7231/kona-work/Kona")
INPUT_CSV = ROOT / "KNN-experiment-res" / "he_ckks_tcga_query0_distances.csv"
OUT_DIR = ROOT / "KNN-experiment-res"

REPORT_PATH = OUT_DIR / "he_ckks_plaintext_distance_by_label_report.txt"
GROUP_CSV = OUT_DIR / "he_ckks_plaintext_distance_by_label_summary.csv"
SORTED_HIST_SVG = OUT_DIR / "he_ckks_plaintext_distance_sorted_histogram.svg"

LABEL_ORDER = ["PRAD", "LUAD", "BRCA", "KIRC", "COAD"]
COLORS = {
    "PRAD": "#2563eb",
    "LUAD": "#dc2626",
    "BRCA": "#059669",
    "KIRC": "#7c3aed",
    "COAD": "#d97706",
}


def percentile(sorted_vals, p):
    if p <= 0:
        return sorted_vals[0]
    if p >= 100:
        return sorted_vals[-1]
    pos = (len(sorted_vals) - 1) * (p / 100.0)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return sorted_vals[lo]
    frac = pos - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def summary(values):
    vals = sorted(values)
    mean = statistics.mean(vals)
    variance = statistics.pvariance(vals)
    stddev = math.sqrt(variance)
    return {
        "count": len(vals),
        "mean": mean,
        "variance": variance,
        "stddev": stddev,
        "min": vals[0],
        "q1": percentile(vals, 25),
        "median": percentile(vals, 50),
        "q3": percentile(vals, 75),
        "max": vals[-1],
    }


def load_rows():
    with INPUT_CSV.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_group_csv(group_stats):
    fields = ["label", "count", "mean", "variance", "stddev", "min", "q1", "median", "q3", "max"]
    with GROUP_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for label in LABEL_ORDER:
            s = group_stats[label]
            writer.writerow({
                "label": label,
                "count": s["count"],
                "mean": f'{s["mean"]:.15f}',
                "variance": f'{s["variance"]:.15e}',
                "stddev": f'{s["stddev"]:.15f}',
                "min": f'{s["min"]:.15f}',
                "q1": f'{s["q1"]:.15f}',
                "median": f'{s["median"]:.15f}',
                "q3": f'{s["q3"]:.15f}',
                "max": f'{s["max"]:.15f}',
            })


def write_report(group_stats, query_label):
    lines = [
        "HE CKKS Plaintext Distance By Label Report",
        f"input_csv={INPUT_CSV}",
        f"query_label={query_label}",
        f"group_summary_csv={GROUP_CSV}",
        f"sorted_histogram_svg={SORTED_HIST_SVG}",
        "",
    ]
    for label in LABEL_ORDER:
        s = group_stats[label]
        lines.extend([
            f"[{label}]",
            f"count={s['count']}",
            f"mean={s['mean']:.15f}",
            f"variance={s['variance']:.15e}",
            f"stddev={s['stddev']:.15f}",
            f"min={s['min']:.15f}",
            f"q1={s['q1']:.15f}",
            f"median={s['median']:.15f}",
            f"q3={s['q3']:.15f}",
            f"max={s['max']:.15f}",
            "",
        ])
    REPORT_PATH.write_text("\n".join(lines), encoding="utf-8")


def build_bins(values, bin_count=25):
    x_min = min(values)
    x_max = max(values)
    width = (x_max - x_min) / bin_count if x_max != x_min else 1.0
    bins = [0] * bin_count
    for value in values:
        idx = int((value - x_min) / width) if width != 0 else 0
        if idx >= bin_count:
            idx = bin_count - 1
        bins[idx] += 1
    return bins, x_min, x_max, width


def generate_sorted_histogram(rows):
    values = sorted(float(row["plaintext_distance"]) for row in rows)
    labels = [row["label"] for row in sorted(rows, key=lambda r: float(r["plaintext_distance"]))]
    bins, x_min, x_max, width = build_bins(values, 25)
    bin_label_counts = [defaultdict(int) for _ in bins]
    for value, label in zip(values, labels):
        idx = int((value - x_min) / width) if width != 0 else 0
        if idx >= len(bins):
            idx = len(bins) - 1
        bin_label_counts[idx][label] += 1

    width_px, height_px = 1280, 760
    left, right, top, bottom = 90, 40, 70, 95
    plot_w = width_px - left - right
    plot_h = height_px - top - bottom
    max_count = max(bins) if bins else 1

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width_px}" height="{height_px}" viewBox="0 0 {width_px} {height_px}">',
        "<style>",
        "text { font-family: Arial, sans-serif; fill: #111827; }",
        ".title { font-size: 20px; font-weight: bold; }",
        ".label { font-size: 12px; }",
        ".tick { font-size: 11px; }",
        "</style>",
        f'<text x="{left}" y="34" class="title">Sorted Plaintext Distance Histogram (Uniform Axes)</text>',
        f'<text x="{left}" y="54" class="label">X axis: uniform distance bins. Y axis: uniform count scale. Stacked by label.</text>',
        f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#111827" stroke-width="1"/>',
    ]

    for t in range(6):
        y = top + plot_h - (t / 5.0) * plot_h
        v = (t / 5.0) * max_count
        lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d1d5db" stroke-width="1"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" class="tick">{v:.0f}</text>')

    bar_slot = plot_w / len(bins)
    for i, total in enumerate(bins):
        x = left + i * bar_slot + 1
        w = bar_slot - 2
        y_cursor = top + plot_h
        for label in LABEL_ORDER:
            count = bin_label_counts[i].get(label, 0)
            if count == 0:
                continue
            h = (count / max_count) * plot_h
            y_cursor -= h
            lines.append(f'<rect x="{x:.2f}" y="{y_cursor:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{COLORS[label]}" opacity="0.88"/>')

    for t in range(6):
        frac = t / 5.0
        x = left + frac * plot_w
        val = x_min + frac * (x_max - x_min)
        lines.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#111827" stroke-width="1"/>')
        lines.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" class="tick">{val:.6f}</text>')

    legend_x = left + plot_w - 160
    legend_y = top + 10
    for idx, label in enumerate(LABEL_ORDER):
        y = legend_y + idx * 22
        lines.append(f'<rect x="{legend_x}" y="{y}" width="14" height="14" fill="{COLORS[label]}"/>')
        lines.append(f'<text x="{legend_x + 22}" y="{y + 12}" class="label">{label}</text>')

    lines.append(f'<text x="{left + plot_w / 2:.2f}" y="{height_px - 28}" text-anchor="middle" class="label">plaintext_distance (uniform bin spacing)</text>')
    lines.append(f'<text x="24" y="{top + plot_h/2:.2f}" transform="rotate(-90 24,{top + plot_h/2:.2f})" text-anchor="middle" class="label">count</text>')
    lines.append("</svg>")
    SORTED_HIST_SVG.write_text("\n".join(lines), encoding="utf-8")


def main():
    rows = load_rows()
    query_label = rows[0]["query_label"]
    grouped = defaultdict(list)
    for row in rows:
        grouped[row["label"]].append(float(row["plaintext_distance"]))
    group_stats = {label: summary(grouped[label]) for label in LABEL_ORDER}
    write_group_csv(group_stats)
    write_report(group_stats, query_label)
    generate_sorted_histogram(rows)
    print(f"REPORT={REPORT_PATH}")
    print(f"GROUP_CSV={GROUP_CSV}")
    print(f"SORTED_HISTOGRAM={SORTED_HIST_SVG}")
    for label in LABEL_ORDER:
        s = group_stats[label]
        print(f"{label} count={s['count']} mean={s['mean']:.12f} median={s['median']:.12f} min={s['min']:.12f} max={s['max']:.12f}")


if __name__ == "__main__":
    main()
