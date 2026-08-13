#!/usr/bin/env python3
"""
Read-only audit and plaintext analysis for the UCI TCGA-PANCAN RNA-Seq dataset.

This script:
1. Audits the current Kona preprocessing / data-loading path from source files.
2. Loads the raw UCI CSV matrix and labels using all 20,531 features.
3. Builds three full-feature representations:
   - raw float
   - global scaled float
   - q1000 fixed-point reference
4. Computes leave-one-out pairwise squared Euclidean distances and top-k gaps.
5. Runs plain KNN classification and representation-consistency checks.
6. Writes Markdown, JSON, and CSV artifacts.

No feature selection, per-feature normalization, clipping, or CKKS parameter
search is performed.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import statistics
import subprocess
import tempfile
from array import array
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


RAW_DATA_DEFAULT = Path(
    "/mnt/c/Users/77231/Downloads/"
    "gene+expression+cancer+rna+seq/"
    "TCGA-PANCAN-HiSeq-801x20531.tar/"
    "TCGA-PANCAN-HiSeq-801x20531/"
    "data.csv"
)
LABELS_DEFAULT = RAW_DATA_DEFAULT.with_name("labels.csv")
REPO_ROOT_DEFAULT = Path("/home/u7231/kona-work/Kona")
OUTPUT_DIR_DEFAULT = REPO_ROOT_DEFAULT / "reports" / "tcga_pancan_plain_task1"
GLOBAL_QUANTILES = [0.0, 0.001, 0.01, 0.05, 0.5, 0.95, 0.99, 0.999, 1.0]
TOPK_LIST = [1, 3, 5, 10]
CURRENT_PROJECT_K = 5
HE_COMPARE_ABS_INPUT_LIMIT = 1000.0
LABEL_ORDER_FALLBACK = ["PRAD", "LUAD", "BRCA", "KIRC", "COAD"]


@dataclass
class AuditFacts:
    current_k: int
    kona_dataset_name: str
    kona_meta_path: str
    kona_train_x_path: str
    kona_train_y_path: str
    kona_test_x_path: str
    kona_test_y_path: str
    kona_split_train: int
    kona_split_test: int
    kona_num_features: int
    kona_matrix_orientation: str
    label_map: list[dict[str, object]]
    raw_source_dir: str
    import_scale: int
    import_seed: int
    import_test_count: int
    applied_log: bool
    applied_minmax: bool
    applied_zscore: bool
    applied_multiply_1000: bool
    applied_multiply_10000: bool
    applied_round_to_int: bool
    applied_clip: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-csv", type=Path, default=RAW_DATA_DEFAULT)
    parser.add_argument("--labels-csv", type=Path, default=LABELS_DEFAULT)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT_DEFAULT)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR_DEFAULT)
    parser.add_argument(
        "--seed",
        type=int,
        default=20260801,
        help="Recorded in outputs for reproducibility. Analysis itself is deterministic.",
    )
    return parser.parse_args()


def quantile_from_sorted(sorted_values: list[float] | array, q: float) -> float:
    if not sorted_values:
        raise ValueError("cannot compute quantile of empty sequence")
    if q <= 0.0:
        return float(sorted_values[0])
    if q >= 1.0:
        return float(sorted_values[-1])
    pos = (len(sorted_values) - 1) * q
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(sorted_values[lo])
    weight = pos - lo
    return float(sorted_values[lo]) * (1.0 - weight) + float(sorted_values[hi]) * weight


def quantiles_from_sorted(
    sorted_values: list[float] | array,
    quantiles: Iterable[float],
) -> dict[str, float]:
    return {f"{q * 100:g}%": quantile_from_sorted(sorted_values, q) for q in quantiles}


def csv_escape(value: object) -> str:
    if value is None:
        return ""
    text = str(value)
    if any(ch in text for ch in [",", "\"", "\n"]):
        return '"' + text.replace('"', '""') + '"'
    return text


def write_csv(path: Path, header: list[str], rows: Iterable[dict[str, object]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def extract_int(pattern: str, text: str, label: str) -> int:
    match = re.search(pattern, text, flags=re.MULTILINE)
    if not match:
        raise ValueError(f"failed to extract {label}")
    return int(match.group(1))


def extract_dataset_name(text: str) -> str:
    match = re.search(r'vector<string>dataset_name_list=\{"([^"]+)"\};', text)
    if not match:
        raise ValueError("failed to extract dataset_name_list")
    return match.group(1)


def extract_import_path(script_text: str) -> str:
    match = re.search(
        r"DEFAULT_SOURCE\s*=\s*Path\((.*?)\)\s*DEFAULT_OUTPUT",
        script_text,
        flags=re.DOTALL,
    )
    if not match:
        raise ValueError("failed to locate DEFAULT_SOURCE block")
    parts = re.findall(r'"([^"]+)"', match.group(1))
    if not parts:
        raise ValueError("failed to extract DEFAULT_SOURCE string parts")
    return "".join(parts)


def load_label_map(path: Path) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            idx_str, label = line.split("\t", 1)
            result.append({"label_id": int(idx_str), "label_name": label})
    return result


def audit_current_code(repo_root: Path) -> AuditFacts:
    kona_cpp = (repo_root / "Machines" / "kona.cpp").read_text(encoding="utf-8")
    import_py = (repo_root / "Scripts" / "import_tcga_pancan_to_kona.py").read_text(
        encoding="utf-8"
    )
    meta_path = repo_root / "Player-Data" / "Knn-Data" / "knn-1" / "tcga-pancan-data" / "Knn-meta"
    label_map_path = (
        repo_root / "Player-Data" / "Knn-Data" / "knn-1" / "tcga-pancan-data" / "label_map.txt"
    )
    meta_parts = meta_path.read_text(encoding="utf-8").strip().split()
    if len(meta_parts) != 3:
        raise ValueError(f"unexpected Knn-meta content in {meta_path}")

    dataset_name = extract_dataset_name(kona_cpp)
    import_scale = extract_int(r"default=(\d+),\s*\n\s*help=\"Multiply each", import_py, "scale")
    import_seed = extract_int(r"default=(\d+),\s*\n\s*help=\"Shuffle seed", import_py, "seed")
    import_test_count = extract_int(
        r"default=(\d+),\s*\n\s*help=\"Number of test samples", import_py, "test_count"
    )

    return AuditFacts(
        current_k=extract_int(r"const int k_const=(\d+);", kona_cpp, "current k"),
        kona_dataset_name=dataset_name,
        kona_meta_path=str(meta_path),
        kona_train_x_path=str(meta_path.with_name("P0-0-X-Train")),
        kona_train_y_path=str(meta_path.with_name("P0-0-Y-Train")),
        kona_test_x_path=str(meta_path.with_name("P1-0-X-Test")),
        kona_test_y_path=str(meta_path.with_name("P1-0-Y-Test")),
        kona_split_train=int(meta_parts[1]),
        kona_split_test=int(meta_parts[2]),
        kona_num_features=int(meta_parts[0]),
        kona_matrix_orientation="rows=samples, columns=gene features",
        label_map=load_label_map(label_map_path),
        raw_source_dir=extract_import_path(import_py),
        import_scale=import_scale,
        import_seed=import_seed,
        import_test_count=import_test_count,
        applied_log=False,
        applied_minmax=False,
        applied_zscore=False,
        applied_multiply_1000=False,
        applied_multiply_10000=True,
        applied_round_to_int=True,
        applied_clip=False,
    )


def format_float(x: float) -> str:
    if math.isnan(x) or math.isinf(x):
        return str(x)
    return f"{x:.12g}"


def compute_external_quantiles(values: array, quantiles: list[float]) -> dict[str, float]:
    ranks = {}
    n = len(values)
    for q in quantiles:
        pos = (n - 1) * q
        lo = int(math.floor(pos))
        hi = int(math.ceil(pos))
        ranks[lo] = None
        ranks[hi] = None

    with tempfile.TemporaryDirectory(prefix="tcga_plain_sort_") as tmpdir:
        unsorted_path = Path(tmpdir) / "values.txt"
        sorted_path = Path(tmpdir) / "values.sorted.txt"
        with unsorted_path.open("w", encoding="utf-8") as f:
            for value in values:
                f.write(f"{value:.12g}\n")
        with sorted_path.open("w", encoding="utf-8") as out:
            subprocess.run(
                ["sort", "-g", str(unsorted_path)],
                check=True,
                stdout=out,
            )
        needed = set(ranks.keys())
        with sorted_path.open("r", encoding="utf-8") as f:
            for idx, line in enumerate(f):
                if idx in needed:
                    ranks[idx] = float(line.strip())
                if all(ranks[k] is not None for k in needed):
                    break

    result: dict[str, float] = {}
    for q in quantiles:
        pos = (n - 1) * q
        lo = int(math.floor(pos))
        hi = int(math.ceil(pos))
        if lo == hi:
            result[f"{q * 100:g}%"] = float(ranks[lo])
        else:
            weight = pos - lo
            result[f"{q * 100:g}%"] = float(ranks[lo]) * (1.0 - weight) + float(ranks[hi]) * weight
    return result


def stable_label_order(labels: list[str], audit_facts: AuditFacts) -> list[str]:
    mapped = [item["label_name"] for item in audit_facts.label_map]
    label_set = set(labels)
    if mapped and set(mapped) == label_set:
        return mapped
    ordered = [label for label in LABEL_ORDER_FALLBACK if label in label_set]
    for label in sorted(label_set):
        if label not in ordered:
            ordered.append(label)
    return ordered


def majority_vote(label_ids: list[int]) -> int:
    counts = Counter(label_ids)
    max_count = max(counts.values())
    winners = [label for label, count in counts.items() if count == max_count]
    return min(winners)


def confusion_matrix(
    true_labels: list[int],
    pred_labels: list[int],
    ordered_label_ids: list[int],
) -> list[list[int]]:
    index = {label_id: i for i, label_id in enumerate(ordered_label_ids)}
    matrix = [[0 for _ in ordered_label_ids] for _ in ordered_label_ids]
    for truth, pred in zip(true_labels, pred_labels):
        matrix[index[truth]][index[pred]] += 1
    return matrix


def matrix_to_markdown(
    matrix: list[list[int]],
    ordered_label_ids: list[int],
    label_name_by_id: dict[int, str],
) -> str:
    labels = [label_name_by_id[idx] for idx in ordered_label_ids]
    header = "| true \\ pred | " + " | ".join(labels) + " |"
    sep = "|---|" + "|".join(["---"] * len(labels)) + "|"
    rows = [header, sep]
    for i, row in enumerate(matrix):
        rows.append("| " + labels[i] + " | " + " | ".join(str(v) for v in row) + " |")
    return "\n".join(rows)


def topk_set_overlap(a: list[int], b: list[int], k: int) -> float:
    return len(set(a[:k]) & set(b[:k])) / float(k)


def topk_rank_overlap(a: list[int], b: list[int], k: int) -> float:
    return 1.0 if a[:k] == b[:k] else 0.0


def build_gap_summary(
    representation: str,
    k: int,
    gaps: list[float],
    adjacent_gaps: list[float],
    tiny_thresholds: list[float],
) -> dict[str, object]:
    sorted_gaps = sorted(gaps)
    sorted_adjacent = sorted(adjacent_gaps)
    row: dict[str, object] = {
        "representation": representation,
        "k": k,
        "query_count": len(gaps),
        "gap_min": sorted_gaps[0],
        "gap_mean": statistics.fmean(gaps),
        "gap_std": statistics.pstdev(gaps),
        "gap_equal_zero_count": sum(1 for x in gaps if x == 0.0),
        "adjacent_count": len(adjacent_gaps),
        "adjacent_min": sorted_adjacent[0],
        "adjacent_mean": statistics.fmean(adjacent_gaps),
        "adjacent_std": statistics.pstdev(adjacent_gaps),
        "adjacent_equal_zero_count": sum(1 for x in adjacent_gaps if x == 0.0),
    }
    for key, value in quantiles_from_sorted(sorted_gaps, GLOBAL_QUANTILES).items():
        row[f"gap_quantile_{key}"] = value
    for key, value in quantiles_from_sorted(sorted_adjacent, GLOBAL_QUANTILES).items():
        row[f"adjacent_quantile_{key}"] = value
    for threshold in tiny_thresholds:
        row[f"gap_lt_{threshold:.0e}"] = sum(1 for x in gaps if x < threshold)
    return row


def main() -> None:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    audit = audit_current_code(args.repo_root)

    if not args.data_csv.exists():
        raise FileNotFoundError(f"missing data file: {args.data_csv}")
    if not args.labels_csv.exists():
        raise FileNotFoundError(f"missing labels file: {args.labels_csv}")

    labels_rows: list[tuple[str, str]] = []
    with args.labels_csv.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        for row in reader:
            if not row:
                continue
            labels_rows.append((row[0].strip(), row[1].strip()))

    label_order = stable_label_order([label for _, label in labels_rows], audit)
    label_to_id = {label: idx for idx, label in enumerate(label_order)}
    label_name_by_id = {idx: label for label, idx in label_to_id.items()}

    feature_names: list[str] = []
    sample_ids: list[str] = []
    labels: list[str] = []
    label_ids: list[int] = []
    rows_raw: list[array] = []
    rows_q1000: list[array] = []
    norm2_raw: list[float] = []
    norm2_q1000: list[float] = []
    sample_min: list[float] = []
    sample_max: list[float] = []
    sample_mean: list[float] = []
    sample_std: list[float] = []
    sample_zero_count: list[int] = []
    flattened_values = array("d")

    feature_sum: list[float] | None = None
    feature_sumsq: list[float] | None = None
    feature_min: list[float] | None = None
    feature_max: list[float] | None = None
    feature_zero_count: list[int] | None = None

    global_min = math.inf
    global_max = -math.inf
    global_sum = 0.0
    global_sumsq = 0.0
    nan_count = 0
    inf_count = 0
    missing_count = 0
    zero_count = 0

    label_lookup = {sample_id: label for sample_id, label in labels_rows}

    with args.data_csv.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        feature_names = header[1:]
        d = len(feature_names)
        feature_sum = [0.0] * d
        feature_sumsq = [0.0] * d
        feature_min = [math.inf] * d
        feature_max = [-math.inf] * d
        feature_zero_count = [0] * d

        for row_idx, row in enumerate(reader):
            if not row:
                continue
            sample_id = row[0].strip()
            if len(row) != d + 1:
                missing_count += max(0, d + 1 - len(row))
                raise ValueError(
                    f"row {row_idx + 2} has {len(row) - 1} features, expected {d}"
                )
            if sample_id not in label_lookup:
                raise ValueError(f"sample id {sample_id} missing from labels.csv")

            values = array("d")
            row_min = math.inf
            row_max = -math.inf
            row_sum = 0.0
            row_sumsq = 0.0
            row_zero = 0

            for j, token in enumerate(row[1:]):
                token = token.strip()
                if token == "":
                    missing_count += 1
                    raise ValueError(f"missing value at sample {sample_id} feature {feature_names[j]}")
                value = float(token)
                if math.isnan(value):
                    nan_count += 1
                if math.isinf(value):
                    inf_count += 1

                values.append(value)
                flattened_values.append(value)
                global_min = min(global_min, value)
                global_max = max(global_max, value)
                global_sum += value
                global_sumsq += value * value

                feature_sum[j] += value
                feature_sumsq[j] += value * value
                if value < feature_min[j]:
                    feature_min[j] = value
                if value > feature_max[j]:
                    feature_max[j] = value

                if value == 0.0:
                    zero_count += 1
                    row_zero += 1
                    feature_zero_count[j] += 1

                if value < row_min:
                    row_min = value
                if value > row_max:
                    row_max = value
                row_sum += value
                row_sumsq += value * value

            sample_ids.append(sample_id)
            label = label_lookup[sample_id]
            labels.append(label)
            label_ids.append(label_to_id[label])
            rows_raw.append(values)
            row_norm2 = math.sumprod(values, values)
            norm2_raw.append(row_norm2)
            sample_min.append(row_min)
            sample_max.append(row_max)
            sample_mean.append(row_sum / d)
            sample_std.append(math.sqrt(max(row_sumsq / d - (row_sum / d) ** 2, 0.0)))
            sample_zero_count.append(row_zero)

    n = len(rows_raw)
    d = len(feature_names)
    total_values = n * d
    if n != len(labels_rows):
        raise ValueError(f"data rows ({n}) and label rows ({len(labels_rows)}) mismatch")

    global_mean = global_sum / total_values
    global_std = math.sqrt(max(global_sumsq / total_values - global_mean * global_mean, 0.0))

    global_quantiles = compute_external_quantiles(flattened_values, GLOBAL_QUANTILES)

    S_ceil = math.ceil(global_max)
    S_power2 = 1
    while S_power2 < S_ceil:
        S_power2 <<= 1
    S = float(S_ceil)

    for values in rows_raw:
        qrow = array("H")
        for value in values:
            scaled = int(round(1000.0 * (value / S)))
            if scaled < 0 or scaled > 65535:
                raise ValueError(f"q1000 value out of uint16 range: {scaled}")
            qrow.append(scaled)
        rows_q1000.append(qrow)
        norm2_q1000.append(math.sumprod(qrow, qrow))

    distance_raw = [[0.0 for _ in range(n)] for _ in range(n)]
    distance_q1000_int = [[0.0 for _ in range(n)] for _ in range(n)]
    for i in range(n):
        distance_raw[i][i] = 0.0
        distance_q1000_int[i][i] = 0.0
        if i % 50 == 0:
            print(f"pairwise distances: row {i}/{n}")
        for j in range(i + 1, n):
            dot_raw = math.sumprod(rows_raw[i], rows_raw[j])
            dist_raw = norm2_raw[i] + norm2_raw[j] - 2.0 * dot_raw
            if dist_raw < 0 and dist_raw > -1e-9:
                dist_raw = 0.0
            distance_raw[i][j] = dist_raw
            distance_raw[j][i] = dist_raw

            dot_q = math.sumprod(rows_q1000[i], rows_q1000[j])
            dist_q_int = norm2_q1000[i] + norm2_q1000[j] - 2.0 * dot_q
            if dist_q_int < 0 and dist_q_int > -1e-9:
                dist_q_int = 0.0
            distance_q1000_int[i][j] = dist_q_int
            distance_q1000_int[j][i] = dist_q_int

    pair_distances_raw: list[float] = []
    pair_distances_global: list[float] = []
    pair_distances_q1000: list[float] = []
    pair_diffs_global: list[float] = []
    scale_global = 1.0 / (S * S * d)
    scale_q1000 = 1.0 / (1000.0 * 1000.0 * d)
    for i in range(n):
        for j in range(n):
            if i == j:
                continue
            raw_val = distance_raw[i][j]
            pair_distances_raw.append(raw_val)
            pair_distances_global.append(raw_val * scale_global)
            pair_distances_q1000.append(distance_q1000_int[i][j] * scale_q1000)
            pair_diffs_global.append(distance_raw[i][j] * scale_global)

    pair_distances_raw_sorted = sorted(pair_distances_raw)
    pair_distances_global_sorted = sorted(pair_distances_global)
    pair_distances_q1000_sorted = sorted(pair_distances_q1000)

    sorted_neighbors_raw: list[list[int]] = []
    sorted_neighbors_global: list[list[int]] = []
    sorted_neighbors_q1000: list[list[int]] = []
    sorted_dists_raw: list[list[float]] = []
    sorted_dists_global: list[list[float]] = []
    sorted_dists_q1000: list[list[float]] = []

    for i in range(n):
        raw_pairs = sorted(
            ((distance_raw[i][j], j) for j in range(n) if j != i),
            key=lambda item: (item[0], item[1]),
        )
        q_pairs = sorted(
            ((distance_q1000_int[i][j] * scale_q1000, j) for j in range(n) if j != i),
            key=lambda item: (item[0], item[1]),
        )
        sorted_neighbors_raw.append([j for _, j in raw_pairs])
        sorted_neighbors_global.append([j for _, j in raw_pairs])
        sorted_neighbors_q1000.append([j for _, j in q_pairs])
        raw_vals = [dist for dist, _ in raw_pairs]
        global_vals = [dist * scale_global for dist, _ in raw_pairs]
        q_vals = [dist for dist, _ in q_pairs]
        sorted_dists_raw.append(raw_vals)
        sorted_dists_global.append(global_vals)
        sorted_dists_q1000.append(q_vals)

    tiny_thresholds = [1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6]
    topk_gap_rows: list[dict[str, object]] = []
    worst_gap_rows: list[dict[str, object]] = []

    def process_gap_representation(
        name: str,
        neighbor_lists: list[list[int]],
        distance_lists: list[list[float]],
    ) -> dict[int, list[float]]:
        gap_map: dict[int, list[float]] = {}
        for k in TOPK_LIST:
            gaps: list[float] = []
            adjacent: list[float] = []
            case_rows: list[dict[str, object]] = []
            for i in range(n):
                dists = distance_lists[i]
                gap = dists[k] - dists[k - 1]
                gaps.append(gap)
                adjacent.extend(dists[t + 1] - dists[t] for t in range(len(dists) - 1))
                case_rows.append(
                    {
                        "representation": name,
                        "k": k,
                        "query_index": i,
                        "query_sample_id": sample_ids[i],
                        "query_label": labels[i],
                        "neighbor_k_index": neighbor_lists[i][k - 1],
                        "neighbor_k_sample_id": sample_ids[neighbor_lists[i][k - 1]],
                        "neighbor_k_label": labels[neighbor_lists[i][k - 1]],
                        "neighbor_kplus1_index": neighbor_lists[i][k],
                        "neighbor_kplus1_sample_id": sample_ids[neighbor_lists[i][k]],
                        "neighbor_kplus1_label": labels[neighbor_lists[i][k]],
                        "D_k": dists[k - 1],
                        "D_kplus1": dists[k],
                        "gap_k": gap,
                    }
                )
            gap_map[k] = gaps
            topk_gap_rows.append(build_gap_summary(name, k, gaps, adjacent, tiny_thresholds))
            case_rows.sort(key=lambda row: (row["gap_k"], row["query_index"]))
            worst_gap_rows.extend(case_rows[:20])
        return gap_map

    gaps_raw = process_gap_representation("raw", sorted_neighbors_raw, sorted_dists_raw)
    gaps_global = process_gap_representation("global", sorted_neighbors_global, sorted_dists_global)
    gaps_q1000 = process_gap_representation("q1000", sorted_neighbors_q1000, sorted_dists_q1000)

    pred_raw: list[int] = []
    pred_global: list[int] = []
    pred_q1000: list[int] = []
    pred_rows: list[dict[str, object]] = []

    full_rank_equal_raw_global = 0
    full_rank_equal_raw_q1000 = 0
    full_rank_equal_global_q1000 = 0
    topk_set_overlap_raw_global = 0.0
    topk_set_overlap_raw_q1000 = 0.0
    topk_set_overlap_global_q1000 = 0.0
    topk_rank_overlap_raw_global = 0.0
    topk_rank_overlap_raw_q1000 = 0.0
    topk_rank_overlap_global_q1000 = 0.0
    rank_changed_but_same_pred = 0
    pred_changed_cases: list[dict[str, object]] = []

    for i in range(n):
        neigh_raw = sorted_neighbors_raw[i]
        neigh_global = sorted_neighbors_global[i]
        neigh_q1000 = sorted_neighbors_q1000[i]

        pred_r = majority_vote([label_ids[idx] for idx in neigh_raw[:CURRENT_PROJECT_K]])
        pred_g = majority_vote([label_ids[idx] for idx in neigh_global[:CURRENT_PROJECT_K]])
        pred_q = majority_vote([label_ids[idx] for idx in neigh_q1000[:CURRENT_PROJECT_K]])
        pred_raw.append(pred_r)
        pred_global.append(pred_g)
        pred_q1000.append(pred_q)

        if neigh_raw == neigh_global:
            full_rank_equal_raw_global += 1
        if neigh_raw == neigh_q1000:
            full_rank_equal_raw_q1000 += 1
        if neigh_global == neigh_q1000:
            full_rank_equal_global_q1000 += 1

        topk_set_overlap_raw_global += topk_set_overlap(neigh_raw, neigh_global, CURRENT_PROJECT_K)
        topk_set_overlap_raw_q1000 += topk_set_overlap(neigh_raw, neigh_q1000, CURRENT_PROJECT_K)
        topk_set_overlap_global_q1000 += topk_set_overlap(neigh_global, neigh_q1000, CURRENT_PROJECT_K)

        topk_rank_overlap_raw_global += topk_rank_overlap(neigh_raw, neigh_global, CURRENT_PROJECT_K)
        topk_rank_overlap_raw_q1000 += topk_rank_overlap(neigh_raw, neigh_q1000, CURRENT_PROJECT_K)
        topk_rank_overlap_global_q1000 += topk_rank_overlap(neigh_global, neigh_q1000, CURRENT_PROJECT_K)

        if pred_r == pred_q and neigh_raw != neigh_q1000:
            rank_changed_but_same_pred += 1

        if pred_r != pred_q or pred_r != pred_g:
            pred_changed_cases.append(
                {
                    "query_index": i,
                    "sample_id": sample_ids[i],
                    "true_label": labels[i],
                    "pred_raw": label_name_by_id[pred_r],
                    "pred_global": label_name_by_id[pred_g],
                    "pred_q1000": label_name_by_id[pred_q],
                }
            )

        pred_rows.append(
            {
                "query_index": i,
                "sample_id": sample_ids[i],
                "true_label": labels[i],
                "pred_raw": label_name_by_id[pred_r],
                "pred_global": label_name_by_id[pred_g],
                "pred_q1000": label_name_by_id[pred_q],
                "raw_correct": int(pred_r == label_ids[i]),
                "global_correct": int(pred_g == label_ids[i]),
                "q1000_correct": int(pred_q == label_ids[i]),
                "raw_global_same_pred": int(pred_r == pred_g),
                "raw_q1000_same_pred": int(pred_r == pred_q),
                "global_q1000_same_pred": int(pred_g == pred_q),
                "top5_raw_indices": " ".join(str(x) for x in sorted_neighbors_raw[i][:CURRENT_PROJECT_K]),
                "top5_global_indices": " ".join(str(x) for x in sorted_neighbors_global[i][:CURRENT_PROJECT_K]),
                "top5_q1000_indices": " ".join(str(x) for x in sorted_neighbors_q1000[i][:CURRENT_PROJECT_K]),
            }
        )

    ordered_label_ids = list(range(len(label_order)))
    cm_raw = confusion_matrix(label_ids, pred_raw, ordered_label_ids)
    cm_global = confusion_matrix(label_ids, pred_global, ordered_label_ids)
    cm_q1000 = confusion_matrix(label_ids, pred_q1000, ordered_label_ids)

    pair_diff_abs_max = 0.0
    pair_diff_min = math.inf
    for i in range(n):
        local_min = sorted_dists_global[i][0]
        local_max = sorted_dists_global[i][-1]
        pair_diff_abs_max = max(pair_diff_abs_max, local_max - local_min)
        if local_min < pair_diff_min:
            pair_diff_min = local_min

    alpha_max = HE_COMPARE_ABS_INPUT_LIMIT / pair_diff_abs_max if pair_diff_abs_max > 0 else math.inf
    alpha_gap_stats = [gap * alpha_max for gap in gaps_global[CURRENT_PROJECT_K]]
    alpha_gap_stats_sorted = sorted(alpha_gap_stats)

    feature_rows = []
    constant_feature_count = 0
    for j in range(d):
        mean = feature_sum[j] / n
        variance = max(feature_sumsq[j] / n - mean * mean, 0.0)
        std = math.sqrt(variance)
        is_constant = int(feature_min[j] == feature_max[j])
        constant_feature_count += is_constant
        feature_rows.append(
            {
                "feature_index": j,
                "feature_name": feature_names[j],
                "min": feature_min[j],
                "max": feature_max[j],
                "mean": mean,
                "std": std,
                "zero_count": feature_zero_count[j],
                "zero_ratio": feature_zero_count[j] / n,
                "is_constant": is_constant,
            }
        )

    sample_rows = []
    norm2_raw_sorted = sorted(norm2_raw)
    l2_raw = [math.sqrt(x) for x in norm2_raw]
    l2_raw_sorted = sorted(l2_raw)
    for i in range(n):
        sample_rows.append(
            {
                "sample_index": i,
                "sample_id": sample_ids[i],
                "label": labels[i],
                "min": sample_min[i],
                "max": sample_max[i],
                "mean": sample_mean[i],
                "std": sample_std[i],
                "zero_count": sample_zero_count[i],
                "zero_ratio": sample_zero_count[i] / d,
                "l2_norm": l2_raw[i],
                "squared_l2_norm": norm2_raw[i],
            }
        )

    def distance_summary(sorted_vals: list[float], raw_vals: list[float]) -> dict[str, object]:
        return {
            "min": sorted_vals[0],
            "max": sorted_vals[-1],
            "mean": statistics.fmean(raw_vals),
            "std": statistics.pstdev(raw_vals),
            "quantiles": quantiles_from_sorted(sorted_vals, GLOBAL_QUANTILES),
            "has_negative": any(x < 0 for x in raw_vals),
            "has_nan": any(math.isnan(x) for x in raw_vals),
            "has_inf": any(math.isinf(x) for x in raw_vals),
        }

    label_counts = Counter(labels)
    summary = {
        "metadata": {
            "analysis_date": "2026-08-01",
            "seed": args.seed,
            "analysis_mode": "deterministic_leave_one_out_plaintext",
            "data_csv": str(args.data_csv),
            "labels_csv": str(args.labels_csv),
            "output_dir": str(args.output_dir),
        },
        "audit": {
            **audit.__dict__,
            "notes": {
                "feature_selection_applied": False,
                "pca_applied": False,
                "variance_filter_applied": False,
                "value_clipping_applied": False,
                "per_feature_normalization_applied": False,
                "global_uniform_scaling_applied_in_this_analysis": True,
                "divide_by_fixed_dimension_applied_in_this_analysis": True,
                "distance_self_excluded": True,
            },
        },
        "dataset": {
            "shape": [n, d],
            "labels_shape": [n],
            "label_order": label_order,
            "label_counts": dict(label_counts),
            "nan_count": nan_count,
            "inf_count": inf_count,
            "missing_count": missing_count,
            "global_min": global_min,
            "global_max": global_max,
            "global_mean": global_mean,
            "global_std": global_std,
            "global_quantiles": global_quantiles,
            "zero_count": zero_count,
            "zero_ratio": zero_count / total_values,
            "constant_feature_count": constant_feature_count,
            "sample_l2_quantiles": quantiles_from_sorted(l2_raw_sorted, GLOBAL_QUANTILES),
            "sample_squared_l2_quantiles": quantiles_from_sorted(norm2_raw_sorted, GLOBAL_QUANTILES),
            "global_scaling_constant_candidates": {
                "S_used_for_x_global": S,
                "ceil_global_max": S_ceil,
                "next_power_of_two": S_power2,
            },
        },
        "distances": {
            "raw": distance_summary(pair_distances_raw_sorted, pair_distances_raw),
            "global": distance_summary(pair_distances_global_sorted, pair_distances_global),
            "q1000": {
                **distance_summary(pair_distances_q1000_sorted, pair_distances_q1000),
                "integer_distance_min": min(distance_q1000_int[i][j] for i in range(n) for j in range(n) if i != j),
                "integer_distance_max": max(distance_q1000_int[i][j] for i in range(n) for j in range(n) if i != j),
            },
            "ranking_consistency": {
                "full_ranking_equal_rate_raw_vs_global": full_rank_equal_raw_global / n,
                "full_ranking_equal_rate_raw_vs_q1000": full_rank_equal_raw_q1000 / n,
                "full_ranking_equal_rate_global_vs_q1000": full_rank_equal_global_q1000 / n,
            },
        },
        "classification": {
            "k": CURRENT_PROJECT_K,
            "accuracy_raw": sum(int(pred_raw[i] == label_ids[i]) for i in range(n)) / n,
            "accuracy_global": sum(int(pred_global[i] == label_ids[i]) for i in range(n)) / n,
            "accuracy_q1000": sum(int(pred_q1000[i] == label_ids[i]) for i in range(n)) / n,
            "topk_set_overlap_rate_raw_vs_global": topk_set_overlap_raw_global / n,
            "topk_set_overlap_rate_raw_vs_q1000": topk_set_overlap_raw_q1000 / n,
            "topk_set_overlap_rate_global_vs_q1000": topk_set_overlap_global_q1000 / n,
            "topk_full_order_rate_raw_vs_global": topk_rank_overlap_raw_global / n,
            "topk_full_order_rate_raw_vs_q1000": topk_rank_overlap_raw_q1000 / n,
            "topk_full_order_rate_global_vs_q1000": topk_rank_overlap_global_q1000 / n,
            "ranking_changed_but_same_prediction_count_raw_vs_q1000": rank_changed_but_same_pred,
            "prediction_changed_cases_count": len(pred_changed_cases),
            "prediction_changed_cases": pred_changed_cases,
            "confusion_matrix_raw": cm_raw,
            "confusion_matrix_global": cm_global,
            "confusion_matrix_q1000": cm_q1000,
        },
        "he_range": {
            "candidate_compare_abs_input_limit_inferred_from_repo_tests": HE_COMPARE_ABS_INPUT_LIMIT,
            "inference_source": (
                "HECompare/tests/openfhe_tcga_compare_resolution_test.cpp scans "
                "TCGA D5/D6 boundary gaps across scale_sign settings."
            ),
            "single_slot_input_range": [0.0, global_max / S],
            "subtraction_range": [-(global_max / S), global_max / S],
            "squared_term_range": [0.0, (global_max / S) ** 2],
            "sum_before_divide_range": [0.0, max(pair_distances_global) * d],
            "distance_after_divide_range": [min(pair_distances_global), max(pair_distances_global)],
            "distance_difference_range": [-pair_diff_abs_max, pair_diff_abs_max],
            "topk_gap_stats_global_k5": {
                "min": min(gaps_global[CURRENT_PROJECT_K]),
                "1%": quantile_from_sorted(sorted(gaps_global[CURRENT_PROJECT_K]), 0.01),
                "5%": quantile_from_sorted(sorted(gaps_global[CURRENT_PROJECT_K]), 0.05),
                "50%": quantile_from_sorted(sorted(gaps_global[CURRENT_PROJECT_K]), 0.5),
            },
            "alpha_max_without_exceeding_candidate_compare_limit": alpha_max,
            "alpha_gap_stats_k5": {
                "min": alpha_gap_stats_sorted[0],
                "mean": statistics.fmean(alpha_gap_stats),
                "std": statistics.pstdev(alpha_gap_stats),
                "quantiles": quantiles_from_sorted(alpha_gap_stats_sorted, GLOBAL_QUANTILES),
            },
        },
    }

    feature_csv = args.output_dir / "feature_stats.csv"
    sample_csv = args.output_dir / "sample_stats.csv"
    topk_gap_csv = args.output_dir / "topk_gap_stats.csv"
    worst_gap_csv = args.output_dir / "worst_gap_cases.csv"
    predictions_csv = args.output_dir / "predictions_comparison.csv"
    query_knn_reference_csv = args.output_dir / "query_knn_reference.csv"
    summary_json = args.output_dir / "summary.json"
    report_md = args.output_dir / "report.md"

    write_csv(
        feature_csv,
        [
            "feature_index",
            "feature_name",
            "min",
            "max",
            "mean",
            "std",
            "zero_count",
            "zero_ratio",
            "is_constant",
        ],
        feature_rows,
    )
    write_csv(
        sample_csv,
        [
            "sample_index",
            "sample_id",
            "label",
            "min",
            "max",
            "mean",
            "std",
            "zero_count",
            "zero_ratio",
            "l2_norm",
            "squared_l2_norm",
        ],
        sample_rows,
    )
    write_csv(topk_gap_csv, list(topk_gap_rows[0].keys()), topk_gap_rows)
    write_csv(worst_gap_csv, list(worst_gap_rows[0].keys()), worst_gap_rows)
    write_csv(predictions_csv, list(pred_rows[0].keys()), pred_rows)
    query_knn_rows = []
    for i in range(n):
        row = {
            "query_index": i,
            "sample_id": sample_ids[i],
            "label": labels[i],
        }
        for rank in range(1, 11):
            neighbor_idx = sorted_neighbors_global[i][rank - 1]
            row[f"neighbor_{rank}_index"] = neighbor_idx
            row[f"neighbor_{rank}_sample_id"] = sample_ids[neighbor_idx]
            row[f"neighbor_{rank}_label"] = labels[neighbor_idx]
            row[f"D_{rank}_global"] = sorted_dists_global[i][rank - 1]
        row["gap_5_global"] = sorted_dists_global[i][5] - sorted_dists_global[i][4]
        query_knn_rows.append(row)
    write_csv(query_knn_reference_csv, list(query_knn_rows[0].keys()), query_knn_rows)
    summary_json.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    report_lines = [
        "# TCGA-PANCAN RNA-Seq 明文审计与统计报告",
        "",
        "## 1. 范围与约束",
        "",
        "- 原始数据文件：`%s`" % args.data_csv,
        "- 标签文件：`%s`" % args.labels_csv,
        "- 使用全部 `20,531` 个基因特征；未做特征选择、PCA、方差筛选或只保留前若干特征。",
        "- 未做数值裁剪（clipping）。",
        "- 未做逐特征 Min-Max、逐特征 z-score 或其它逐特征归一化。",
        "- 本分析仅使用两种保持欧氏距离排序不变的处理：",
        "  - 全局等比例缩放：`x_global = x / S`，其中 `S = %s`。" % format_float(S),
        "  - 除以固定维度：`D_global = sum((x_global-q_global)^2) / d`，`d = %d`。" % d,
        "- 所有距离和 KNN 排序均排除了查询样本自身（leave-one-out）。",
        "",
        "## 2. 现有代码只读审计",
        "",
        "- `Machines/kona.cpp` 当前真实 `k = %d`。" % audit.current_k,
        "- Kona 当前实际读取的数据文件：",
        "  - `%s`" % audit.kona_train_x_path,
        "  - `%s`" % audit.kona_train_y_path,
        "  - `%s`" % audit.kona_test_x_path,
        "  - `%s`" % audit.kona_test_y_path,
        "- 当前 Kona 文件划分：训练 `%d`，查询 `%d`，特征 `%d`。"
        % (audit.kona_split_train, audit.kona_split_test, audit.kona_num_features),
        "- 矩阵方向：`rows = samples`，`columns = gene features`。",
        "- `Scripts/import_tcga_pancan_to_kona.py` 对原始浮点值执行的是：`round(scale * x)`，当前 `scale = %d`。"
        % audit.import_scale,
        "- 已确认未执行：log、Min-Max、z-score、裁剪。",
        "- 已确认执行：乘 `10000` 后取整，仅发生在 Kona 导入文件，不发生在本次原始明文分析。",
        "",
        "### 当前 Kona label 对应关系",
        "",
        "| label_id | label_name |",
        "|---|---|",
    ]
    for item in audit.label_map:
        report_lines.append(f"| {item['label_id']} | {item['label_name']} |")

    report_lines.extend(
        [
            "",
            "## 3. 数据基本信息",
            "",
            "- 数据矩阵 shape：`(%d, %d)`" % (n, d),
            "- 标签 shape：`(%d,)`" % n,
            "- 类别分布：`%s`" % ", ".join(f"{k}={v}" for k, v in label_counts.items()),
            "- NaN 数量：`%d`" % nan_count,
            "- Inf 数量：`%d`" % inf_count,
            "- 缺失值数量：`%d`" % missing_count,
            "- 全局 min/max/mean/std：`%s / %s / %s / %s`"
            % (
                format_float(global_min),
                format_float(global_max),
                format_float(global_mean),
                format_float(global_std),
            ),
            "- 零值比例：`%s`" % format_float(zero_count / total_values),
            "- 常数特征数量：`%d`" % constant_feature_count,
            "- 建议公开全局缩放常数：`S = ceil(max) = %d`；备选下一个 2 的幂：`%d`。"
            % (S_ceil, S_power2),
            "",
            "### 全局分位数",
            "",
            "| quantile | value |",
            "|---|---|",
        ]
    )
    for key, value in global_quantiles.items():
        report_lines.append(f"| {key} | {format_float(value)} |")

    report_lines.extend(
        [
            "",
            "### 样本 L2 范数分位数",
            "",
            "| quantile | L2 | squared L2 |",
            "|---|---|---|",
        ]
    )
    l2_quantiles = quantiles_from_sorted(l2_raw_sorted, GLOBAL_QUANTILES)
    norm2_quantiles = quantiles_from_sorted(norm2_raw_sorted, GLOBAL_QUANTILES)
    for key in l2_quantiles:
        report_lines.append(
            f"| {key} | {format_float(l2_quantiles[key])} | {format_float(norm2_quantiles[key])} |"
        )

    report_lines.extend(
        [
            "",
            "## 4. 三种表示",
            "",
            "- `x_raw = x`",
            "- `x_global = x / S`，本报告使用 `S = %d`。" % S_ceil,
            "- `x_q1000 = round(1000 * x_global)`",
            "- `D_q1000` 同时保留了整数距离 `sum((x_q1000-q_q1000)^2)` 与除回 `1000^2 * d` 后的归一化距离。",
            "",
            "## 5. 全样本平方欧氏距离统计",
            "",
            "| representation | min | max | mean | std | negative | NaN | Inf |",
            "|---|---|---|---|---|---|---|---|",
        ]
    )
    for rep_name, rep_summary in [
        ("raw", summary["distances"]["raw"]),
        ("global", summary["distances"]["global"]),
        ("q1000", summary["distances"]["q1000"]),
    ]:
        report_lines.append(
            "| %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                rep_name,
                format_float(rep_summary["min"]),
                format_float(rep_summary["max"]),
                format_float(rep_summary["mean"]),
                format_float(rep_summary["std"]),
                rep_summary["has_negative"],
                rep_summary["has_nan"],
                rep_summary["has_inf"],
            )
        )

    report_lines.extend(
        [
            "",
            "### 排序一致性",
            "",
            "- raw vs global 全排序一致率：`%s`"
            % format_float(summary["distances"]["ranking_consistency"]["full_ranking_equal_rate_raw_vs_global"]),
            "- raw vs q1000 全排序一致率：`%s`"
            % format_float(summary["distances"]["ranking_consistency"]["full_ranking_equal_rate_raw_vs_q1000"]),
            "- global vs q1000 全排序一致率：`%s`"
            % format_float(summary["distances"]["ranking_consistency"]["full_ranking_equal_rate_global_vs_q1000"]),
            "",
            "## 6. Top-k 边界分析",
            "",
            "详细统计见 `topk_gap_stats.csv`，最差 20 个查询见 `worst_gap_cases.csv`。",
            "",
            "| representation | k | gap_min | gap_mean | gap_q1% | gap_q50% | gap_q99% | equal_gap_count |",
            "|---|---|---|---|---|---|---|---|",
        ]
    )
    for row in topk_gap_rows:
        report_lines.append(
            "| %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                row["representation"],
                row["k"],
                format_float(row["gap_min"]),
                format_float(row["gap_mean"]),
                format_float(row["gap_quantile_1%"]),
                format_float(row["gap_quantile_50%"]),
                format_float(row["gap_quantile_99%"]),
                row["gap_equal_zero_count"],
            )
        )

    report_lines.extend(
        [
            "",
            "## 7. 分类与表示一致性（k = %d）" % CURRENT_PROJECT_K,
            "",
            "- raw 准确率：`%s`" % format_float(summary["classification"]["accuracy_raw"]),
            "- global 准确率：`%s`" % format_float(summary["classification"]["accuracy_global"]),
            "- q1000 准确率：`%s`" % format_float(summary["classification"]["accuracy_q1000"]),
            "- Top-5 集合重合率 raw vs global：`%s`"
            % format_float(summary["classification"]["topk_set_overlap_rate_raw_vs_global"]),
            "- Top-5 集合重合率 raw vs q1000：`%s`"
            % format_float(summary["classification"]["topk_set_overlap_rate_raw_vs_q1000"]),
            "- Top-5 完整排序一致率 raw vs global：`%s`"
            % format_float(summary["classification"]["topk_full_order_rate_raw_vs_global"]),
            "- Top-5 完整排序一致率 raw vs q1000：`%s`"
            % format_float(summary["classification"]["topk_full_order_rate_raw_vs_q1000"]),
            "- 排序变化但最终类别未变（raw vs q1000）数量：`%d`"
            % summary["classification"]["ranking_changed_but_same_prediction_count_raw_vs_q1000"],
            "- 最终预测类别发生变化的查询数量：`%d`"
            % summary["classification"]["prediction_changed_cases_count"],
            "",
            "### Confusion Matrix: raw",
            "",
            matrix_to_markdown(cm_raw, ordered_label_ids, label_name_by_id),
            "",
            "### Confusion Matrix: global",
            "",
            matrix_to_markdown(cm_global, ordered_label_ids, label_name_by_id),
            "",
            "### Confusion Matrix: q1000",
            "",
            matrix_to_markdown(cm_q1000, ordered_label_ids, label_name_by_id),
            "",
            "## 8. CKKS/FHEW 数值范围建议",
            "",
            "- 候选比较输入绝对值上限（基于现有仓库测试场景推断）：`%s`"
            % format_float(HE_COMPARE_ABS_INPUT_LIMIT),
            "- 单槽输入范围：`[%s, %s]`"
            % (
                format_float(summary["he_range"]["single_slot_input_range"][0]),
                format_float(summary["he_range"]["single_slot_input_range"][1]),
            ),
            "- 减法后范围：`[%s, %s]`"
            % (
                format_float(summary["he_range"]["subtraction_range"][0]),
                format_float(summary["he_range"]["subtraction_range"][1]),
            ),
            "- 平方项范围：`[%s, %s]`"
            % (
                format_float(summary["he_range"]["squared_term_range"][0]),
                format_float(summary["he_range"]["squared_term_range"][1]),
            ),
            "- 求和后除以 `d` 的最终距离范围：`[%s, %s]`"
            % (
                format_float(summary["he_range"]["distance_after_divide_range"][0]),
                format_float(summary["he_range"]["distance_after_divide_range"][1]),
            ),
            "- 两个最终距离之差的范围：`[%s, %s]`"
            % (
                format_float(summary["he_range"]["distance_difference_range"][0]),
                format_float(summary["he_range"]["distance_difference_range"][1]),
            ),
            "- 不超过候选比较输入范围时的最大统一放大系数 `alpha`：`%s`"
            % format_float(summary["he_range"]["alpha_max_without_exceeding_candidate_compare_limit"]),
            "",
            "## 9. 产物清单",
            "",
            "- `report.md`",
            "- `summary.json`",
            "- `sample_stats.csv`",
            "- `feature_stats.csv`",
            "- `topk_gap_stats.csv`",
            "- `worst_gap_cases.csv`",
            "- `predictions_comparison.csv`",
            "- `query_knn_reference.csv`",
            "- `Scripts/analyze_tcga_pancan_plain.py`",
            "",
            "## 10. 复现命令",
            "",
            "```bash",
            "cd /home/u7231/kona-work/Kona",
            "python3 Scripts/analyze_tcga_pancan_plain.py \\",
            f"  --data-csv '{args.data_csv}' \\",
            f"  --labels-csv '{args.labels_csv}' \\",
            f"  --output-dir '{args.output_dir}' \\",
            f"  --seed {args.seed}",
            "```",
            "",
            "说明：本任务没有用到随机抽样；`seed` 仅作为复现元数据落盘。",
        ]
    )

    report_md.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print(f"wrote {report_md}")
    print(f"wrote {summary_json}")
    print(f"wrote {sample_csv}")
    print(f"wrote {feature_csv}")
    print(f"wrote {topk_gap_csv}")
    print(f"wrote {worst_gap_csv}")
    print(f"wrote {predictions_csv}")
    print(f"wrote {query_knn_reference_csv}")


if __name__ == "__main__":
    main()
