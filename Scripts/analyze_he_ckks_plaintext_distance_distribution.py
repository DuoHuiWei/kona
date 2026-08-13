#!/usr/bin/env python3
import csv
import math
import statistics
from pathlib import Path


ROOT = Path("/home/u7231/kona-work/Kona")
INPUT_CSV = ROOT / "KNN-experiment-res" / "he_ckks_tcga_query0_distances.csv"
OUT_DIR = ROOT / "KNN-experiment-res"
REPORT_PATH = OUT_DIR / "he_ckks_plaintext_distance_distribution_report.txt"
HIST_SVG_PATH = OUT_DIR / "he_ckks_plaintext_distance_histogram.svg"
BELL_SVG_PATH = OUT_DIR / "he_ckks_plaintext_distance_bell_curve.svg"


def load_plaintext_distances():
    with INPUT_CSV.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    values = [float(row["plaintext_distance"]) for row in rows]
    query_label = rows[0]["query_label"] if rows else ""
    return values, query_label


def percentile(sorted_vals, p):
    if not sorted_vals:
        raise ValueError("empty data")
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


def compute_stats(values):
    sorted_vals = sorted(values)
    mean = statistics.mean(values)
    variance = statistics.pvariance(values)
    std = math.sqrt(variance)
    n = len(values)
    if std == 0:
        skewness = 0.0
    else:
        skewness = sum(((x - mean) / std) ** 3 for x in values) / n
    return {
        "count": n,
        "mean": mean,
        "variance": variance,
        "stddev": std,
        "min": sorted_vals[0],
        "p5": percentile(sorted_vals, 5),
        "q1": percentile(sorted_vals, 25),
        "median": percentile(sorted_vals, 50),
        "q3": percentile(sorted_vals, 75),
        "p95": percentile(sorted_vals, 95),
        "max": sorted_vals[-1],
        "range": sorted_vals[-1] - sorted_vals[0],
        "skewness": skewness,
    }


def make_histogram(values, bin_count=30):
    x_min = min(values)
    x_max = max(values)
    if x_max == x_min:
        return [len(values)], x_min, x_max, 1.0
    width = (x_max - x_min) / bin_count
    bins = [0] * bin_count
    for value in values:
        idx = int((value - x_min) / width)
        if idx == bin_count:
            idx -= 1
        bins[idx] += 1
    return bins, x_min, x_max, width


def normal_pdf(x, mean, stddev):
    if stddev == 0:
        return 0.0
    z = (x - mean) / stddev
    return math.exp(-0.5 * z * z) / (stddev * math.sqrt(2 * math.pi))


def svg_header(width, height):
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>',
        'text { font-family: Arial, sans-serif; fill: #1f2937; }',
        '.title { font-size: 20px; font-weight: bold; }',
        '.label { font-size: 12px; }',
        '.tick { font-size: 11px; }',
        '</style>',
    ]


def generate_histogram_svg(values, stats, output_path):
    width, height = 1200, 720
    left, right, top, bottom = 90, 40, 70, 90
    plot_w = width - left - right
    plot_h = height - top - bottom

    bins, x_min, x_max, bin_width = make_histogram(values, 30)
    max_count = max(bins)

    lines = svg_header(width, height)
    lines.append(f'<text x="{left}" y="35" class="title">Plaintext Distance Distribution Histogram</text>')
    lines.append(f'<text x="{left}" y="55" class="label">count={stats["count"]} mean={stats["mean"]:.12f} variance={stats["variance"]:.12e}</text>')
    lines.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#111827" stroke-width="1"/>')

    for i, count in enumerate(bins):
        bar_h = 0 if max_count == 0 else (count / max_count) * plot_h
        x = left + (i / len(bins)) * plot_w
        w = plot_w / len(bins) - 2
        y = top + plot_h - bar_h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{bar_h:.2f}" fill="#4f46e5" opacity="0.82"/>')

    for t in range(6):
        y_val = top + plot_h - (t / 5.0) * plot_h
        count_val = (t / 5.0) * max_count
        lines.append(f'<line x1="{left}" y1="{y_val:.2f}" x2="{left + plot_w}" y2="{y_val:.2f}" stroke="#d1d5db" stroke-width="1"/>')
        lines.append(f'<text x="{left - 10}" y="{y_val + 4:.2f}" text-anchor="end" class="tick">{count_val:.0f}</text>')

    for t in range(6):
        frac = t / 5.0
        x = left + frac * plot_w
        val = x_min + frac * (x_max - x_min)
        lines.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#111827" stroke-width="1"/>')
        lines.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" class="tick">{val:.6f}</text>')

    lines.append(f'<text x="{left + plot_w/2:.2f}" y="{height - 28}" text-anchor="middle" class="label">plaintext_distance</text>')
    lines.append(f'<text x="24" y="{top + plot_h/2:.2f}" transform="rotate(-90 24,{top + plot_h/2:.2f})" text-anchor="middle" class="label">count</text>')
    lines.append('</svg>')
    output_path.write_text("\n".join(lines), encoding="utf-8")


def generate_bell_curve_svg(values, stats, output_path):
    width, height = 1200, 720
    left, right, top, bottom = 90, 40, 70, 90
    plot_w = width - left - right
    plot_h = height - top - bottom

    bins, x_min, x_max, bin_width = make_histogram(values, 30)
    max_count = max(bins)
    xs = [x_min + (x_max - x_min) * i / 400.0 for i in range(401)]
    ys = [
        normal_pdf(x, stats["mean"], stats["stddev"]) * len(values) * bin_width
        for x in xs
    ]
    max_y = max(max_count, max(ys) if ys else 0.0)

    lines = svg_header(width, height)
    lines.append(f'<text x="{left}" y="35" class="title">Plaintext Distance Histogram With Bell Curve</text>')
    lines.append(f'<text x="{left}" y="55" class="label">normal-fit mean={stats["mean"]:.12f} std={stats["stddev"]:.12f} skewness={stats["skewness"]:.6f}</text>')
    lines.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#111827" stroke-width="1"/>')

    for i, count in enumerate(bins):
        bar_h = 0 if max_y == 0 else (count / max_y) * plot_h
        x = left + (i / len(bins)) * plot_w
        w = plot_w / len(bins) - 2
        y = top + plot_h - bar_h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{bar_h:.2f}" fill="#93c5fd" opacity="0.75"/>')

    path_points = []
    for x_val, y_val in zip(xs, ys):
        x = left + ((x_val - x_min) / (x_max - x_min)) * plot_w if x_max != x_min else left
        y = top + plot_h - (y_val / max_y) * plot_h if max_y != 0 else top + plot_h
        path_points.append(f'{x:.2f},{y:.2f}')
    lines.append(f'<polyline fill="none" stroke="#dc2626" stroke-width="3" points="{" ".join(path_points)}"/>')

    for marker_name, marker_value, color in [
        ("mean", stats["mean"], "#059669"),
        ("median", stats["median"], "#7c3aed"),
    ]:
        x = left + ((marker_value - x_min) / (x_max - x_min)) * plot_w if x_max != x_min else left
        lines.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}" stroke="{color}" stroke-dasharray="6,6" stroke-width="2"/>')
        lines.append(f'<text x="{x + 6:.2f}" y="{top + 18}" class="tick">{marker_name}={marker_value:.6f}</text>')

    for t in range(6):
        y_val = top + plot_h - (t / 5.0) * plot_h
        count_val = (t / 5.0) * max_y
        lines.append(f'<line x1="{left}" y1="{y_val:.2f}" x2="{left + plot_w}" y2="{y_val:.2f}" stroke="#d1d5db" stroke-width="1"/>')
        lines.append(f'<text x="{left - 10}" y="{y_val + 4:.2f}" text-anchor="end" class="tick">{count_val:.1f}</text>')

    for t in range(6):
        frac = t / 5.0
        x = left + frac * plot_w
        val = x_min + frac * (x_max - x_min)
        lines.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#111827" stroke-width="1"/>')
        lines.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" class="tick">{val:.6f}</text>')

    lines.append(f'<text x="{left + plot_w/2:.2f}" y="{height - 28}" text-anchor="middle" class="label">plaintext_distance</text>')
    lines.append(f'<text x="24" y="{top + plot_h/2:.2f}" transform="rotate(-90 24,{top + plot_h/2:.2f})" text-anchor="middle" class="label">count / fitted curve scale</text>')
    lines.append('</svg>')
    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_report(stats, query_label):
    lines = [
        "HE CKKS Plaintext Distance Distribution Report",
        f"input_csv={INPUT_CSV}",
        f"query_label={query_label}",
        f"count={stats['count']}",
        f"mean={stats['mean']:.15f}",
        f"variance={stats['variance']:.15e}",
        f"stddev={stats['stddev']:.15f}",
        f"min={stats['min']:.15f}",
        f"p5={stats['p5']:.15f}",
        f"q1={stats['q1']:.15f}",
        f"median={stats['median']:.15f}",
        f"q3={stats['q3']:.15f}",
        f"p95={stats['p95']:.15f}",
        f"max={stats['max']:.15f}",
        f"range={stats['range']:.15f}",
        f"skewness={stats['skewness']:.15f}",
        f"histogram_svg={HIST_SVG_PATH}",
        f"bell_curve_svg={BELL_SVG_PATH}",
    ]
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    values, query_label = load_plaintext_distances()
    stats = compute_stats(values)
    generate_histogram_svg(values, stats, HIST_SVG_PATH)
    generate_bell_curve_svg(values, stats, BELL_SVG_PATH)
    write_report(stats, query_label)
    print(f"REPORT={REPORT_PATH}")
    print(f"HISTOGRAM={HIST_SVG_PATH}")
    print(f"BELL_CURVE={BELL_SVG_PATH}")
    for key in ["count", "mean", "variance", "stddev", "min", "q1", "median", "q3", "max", "skewness"]:
        value = stats[key]
        if isinstance(value, int):
            print(f"{key.upper()}={value}")
        else:
            print(f"{key.upper()}={value:.15f}")


if __name__ == "__main__":
    main()
