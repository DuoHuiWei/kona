#!/usr/bin/env python3
import csv
import math
import statistics
from pathlib import Path


ROOT = Path("/home/u7231/kona-work/Kona")
INPUT_CSV = ROOT / "KNN-experiment-res" / "he_ckks_tcga_query0_distances.csv"
OUT_DIR = ROOT / "KNN-experiment-res"

COMPARE_REPORT = OUT_DIR / "he_ckks_vs_plain_distribution_report.txt"
CKKS_HIST_SVG = OUT_DIR / "he_ckks_distance_histogram.svg"
CKKS_BELL_SVG = OUT_DIR / "he_ckks_distance_bell_curve.svg"
ERROR_HIST_SVG = OUT_DIR / "he_ckks_abs_error_histogram.svg"
ERROR_BELL_SVG = OUT_DIR / "he_ckks_abs_error_bell_curve.svg"


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


def stats(values):
    vals = sorted(values)
    mean = statistics.mean(vals)
    variance = statistics.pvariance(vals)
    stddev = math.sqrt(variance)
    n = len(vals)
    skew = 0.0 if stddev == 0 else sum(((x - mean) / stddev) ** 3 for x in vals) / n
    return {
        "count": n,
        "mean": mean,
        "variance": variance,
        "stddev": stddev,
        "min": vals[0],
        "p5": percentile(vals, 5),
        "q1": percentile(vals, 25),
        "median": percentile(vals, 50),
        "q3": percentile(vals, 75),
        "p95": percentile(vals, 95),
        "max": vals[-1],
        "range": vals[-1] - vals[0],
        "skewness": skew,
    }


def build_bins(values, bin_count=30):
    x_min = min(values)
    x_max = max(values)
    width = (x_max - x_min) / bin_count if x_max != x_min else 1.0
    bins = [0] * bin_count
    for v in values:
        idx = int((v - x_min) / width) if width != 0 else 0
        if idx >= bin_count:
            idx = bin_count - 1
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
        "<style>",
        "text { font-family: Arial, sans-serif; fill: #111827; }",
        ".title { font-size: 20px; font-weight: bold; }",
        ".label { font-size: 12px; }",
        ".tick { font-size: 11px; }",
        "</style>",
    ]


def render_histogram(values, title, subtitle, out_path, color):
    width, height = 1200, 720
    left, right, top, bottom = 90, 40, 70, 90
    plot_w = width - left - right
    plot_h = height - top - bottom
    bins, x_min, x_max, _ = build_bins(values, 30)
    max_count = max(bins) if bins else 1

    lines = svg_header(width, height)
    lines.append(f'<text x="{left}" y="35" class="title">{title}</text>')
    lines.append(f'<text x="{left}" y="55" class="label">{subtitle}</text>')
    lines.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#111827" stroke-width="1"/>')

    for i, count in enumerate(bins):
        x = left + i * plot_w / len(bins)
        w = plot_w / len(bins) - 2
        h = 0 if max_count == 0 else (count / max_count) * plot_h
        y = top + plot_h - h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{color}" opacity="0.84"/>')

    for t in range(6):
        y = top + plot_h - (t / 5.0) * plot_h
        v = (t / 5.0) * max_count
        lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d1d5db" stroke-width="1"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" class="tick">{v:.0f}</text>')

    for t in range(6):
        frac = t / 5.0
        x = left + frac * plot_w
        val = x_min + frac * (x_max - x_min)
        lines.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#111827" stroke-width="1"/>')
        lines.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" class="tick">{val:.6e}</text>')

    lines.append(f'<text x="{left + plot_w/2:.2f}" y="{height - 28}" text-anchor="middle" class="label">value</text>')
    lines.append(f'<text x="24" y="{top + plot_h/2:.2f}" transform="rotate(-90 24,{top + plot_h/2:.2f})" text-anchor="middle" class="label">count</text>')
    lines.append("</svg>")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def render_bell_curve(values, metric_stats, title, subtitle, out_path, bar_color, curve_color):
    width, height = 1200, 720
    left, right, top, bottom = 90, 40, 70, 90
    plot_w = width - left - right
    plot_h = height - top - bottom
    bins, x_min, x_max, bin_width = build_bins(values, 30)
    max_count = max(bins) if bins else 1
    xs = [x_min + (x_max - x_min) * i / 400.0 for i in range(401)]
    ys = [normal_pdf(x, metric_stats["mean"], metric_stats["stddev"]) * len(values) * bin_width for x in xs]
    max_y = max(max_count, max(ys) if ys else 0.0)

    lines = svg_header(width, height)
    lines.append(f'<text x="{left}" y="35" class="title">{title}</text>')
    lines.append(f'<text x="{left}" y="55" class="label">{subtitle}</text>')
    lines.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#111827" stroke-width="1"/>')

    for i, count in enumerate(bins):
        x = left + i * plot_w / len(bins)
        w = plot_w / len(bins) - 2
        h = 0 if max_y == 0 else (count / max_y) * plot_h
        y = top + plot_h - h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{bar_color}" opacity="0.74"/>')

    pts = []
    for x_val, y_val in zip(xs, ys):
        x = left + ((x_val - x_min) / (x_max - x_min)) * plot_w if x_max != x_min else left
        y = top + plot_h - (y_val / max_y) * plot_h if max_y != 0 else top + plot_h
        pts.append(f"{x:.2f},{y:.2f}")
    lines.append(f'<polyline fill="none" stroke="{curve_color}" stroke-width="3" points="{" ".join(pts)}"/>')

    for name, val, color in [("mean", metric_stats["mean"], "#059669"), ("median", metric_stats["median"], "#7c3aed")]:
        x = left + ((val - x_min) / (x_max - x_min)) * plot_w if x_max != x_min else left
        lines.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}" stroke="{color}" stroke-dasharray="6,6" stroke-width="2"/>')
        lines.append(f'<text x="{x + 6:.2f}" y="{top + 18}" class="tick">{name}={val:.6e}</text>')

    for t in range(6):
        y = top + plot_h - (t / 5.0) * plot_h
        v = (t / 5.0) * max_y
        lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d1d5db" stroke-width="1"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" class="tick">{v:.1f}</text>')

    for t in range(6):
        frac = t / 5.0
        x = left + frac * plot_w
        val = x_min + frac * (x_max - x_min)
        lines.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#111827" stroke-width="1"/>')
        lines.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" class="tick">{val:.6e}</text>')

    lines.append(f'<text x="{left + plot_w/2:.2f}" y="{height - 28}" text-anchor="middle" class="label">value</text>')
    lines.append(f'<text x="24" y="{top + plot_h/2:.2f}" transform="rotate(-90 24,{top + plot_h/2:.2f})" text-anchor="middle" class="label">count / fitted curve scale</text>')
    lines.append("</svg>")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def load_data():
    with INPUT_CSV.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    plain = [float(r["plaintext_distance"]) for r in rows]
    ckks = [float(r["ckks_distance"]) for r in rows]
    errs = [abs(float(r["ckks_distance"]) - float(r["plaintext_distance"])) for r in rows]
    query_label = rows[0]["query_label"] if rows else ""
    return plain, ckks, errs, query_label


def write_report(plain_stats, ckks_stats, err_stats, query_label):
    lines = [
        "HE CKKS vs Plain Distance Distribution Report",
        f"input_csv={INPUT_CSV}",
        f"query_label={query_label}",
        "",
        "[PLAINTEXT_DISTANCE]",
    ]
    for key in ["count", "mean", "variance", "stddev", "min", "p5", "q1", "median", "q3", "p95", "max", "range", "skewness"]:
        val = plain_stats[key]
        lines.append(f"{key}={val}" if isinstance(val, int) else f"{key}={val:.15e}" if key == "variance" else f"{key}={val:.15f}")
    lines.extend([
        "",
        "[CKKS_DISTANCE]",
    ])
    for key in ["count", "mean", "variance", "stddev", "min", "p5", "q1", "median", "q3", "p95", "max", "range", "skewness"]:
        val = ckks_stats[key]
        lines.append(f"{key}={val}" if isinstance(val, int) else f"{key}={val:.15e}" if key == "variance" else f"{key}={val:.15f}")
    lines.extend([
        "",
        "[ABS_ERROR]",
    ])
    for key in ["count", "mean", "variance", "stddev", "min", "p5", "q1", "median", "q3", "p95", "max", "range", "skewness"]:
        val = err_stats[key]
        lines.append(f"{key}={val}" if isinstance(val, int) else f"{key}={val:.15e}" if key == "variance" else f"{key}={val:.15f}")
    lines.extend([
        "",
        f"ckks_histogram_svg={CKKS_HIST_SVG}",
        f"ckks_bell_curve_svg={CKKS_BELL_SVG}",
        f"error_histogram_svg={ERROR_HIST_SVG}",
        f"error_bell_curve_svg={ERROR_BELL_SVG}",
    ])
    COMPARE_REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    plain, ckks, errs, query_label = load_data()
    plain_stats = stats(plain)
    ckks_stats = stats(ckks)
    err_stats = stats(errs)

    render_histogram(
        ckks,
        "CKKS Distance Distribution Histogram",
        f'count={ckks_stats["count"]} mean={ckks_stats["mean"]:.12f} variance={ckks_stats["variance"]:.12e}',
        CKKS_HIST_SVG,
        "#0f766e",
    )
    render_bell_curve(
        ckks,
        ckks_stats,
        "CKKS Distance Histogram With Bell Curve",
        f'normal-fit mean={ckks_stats["mean"]:.12f} std={ckks_stats["stddev"]:.12f} skewness={ckks_stats["skewness"]:.6f}',
        CKKS_BELL_SVG,
        "#99f6e4",
        "#115e59",
    )
    render_histogram(
        errs,
        "Absolute Error Distribution Histogram",
        f'count={err_stats["count"]} mean={err_stats["mean"]:.6e} variance={err_stats["variance"]:.6e}',
        ERROR_HIST_SVG,
        "#ea580c",
    )
    render_bell_curve(
        errs,
        err_stats,
        "Absolute Error Histogram With Bell Curve",
        f'normal-fit mean={err_stats["mean"]:.6e} std={err_stats["stddev"]:.6e} skewness={err_stats["skewness"]:.6f}',
        ERROR_BELL_SVG,
        "#fdba74",
        "#c2410c",
    )
    write_report(plain_stats, ckks_stats, err_stats, query_label)

    print(f"COMPARE_REPORT={COMPARE_REPORT}")
    print(f"CKKS_HISTOGRAM={CKKS_HIST_SVG}")
    print(f"CKKS_BELL_CURVE={CKKS_BELL_SVG}")
    print(f"ERROR_HISTOGRAM={ERROR_HIST_SVG}")
    print(f"ERROR_BELL_CURVE={ERROR_BELL_SVG}")
    print(f"PLAIN_MEAN={plain_stats['mean']:.15f}")
    print(f"CKKS_MEAN={ckks_stats['mean']:.15f}")
    print(f"ERROR_MEAN={err_stats['mean']:.15e}")
    print(f"ERROR_MAX={err_stats['max']:.15e}")


if __name__ == "__main__":
    main()
