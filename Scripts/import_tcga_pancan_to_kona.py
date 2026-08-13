#!/usr/bin/env python3
"""
Import the TCGA-PANCAN-HiSeq dataset into Kona's current text format.

Input directory must contain:
    data.csv
    labels.csv

Output directory:
    Player-Data/Knn-Data/knn-1/tcga-pancan-data/

Generated files:
    Knn-meta
    P0-0-X-Train
    P0-0-Y-Train
    P1-0-X-Test
    P1-0-Y-Test
    label_map.txt

The current Kona code reads integer features, so floating-point gene
expression values are scaled and rounded.
"""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path


DEFAULT_SOURCE = Path(
    "/mnt/c/Users/77231/Downloads/"
    "gene+expression+cancer+rna+seq/"
    "TCGA-PANCAN-HiSeq-801x20531.tar/"
    "TCGA-PANCAN-HiSeq-801x20531"
)
DEFAULT_OUTPUT = Path(
    "/home/u7231/kona-work/Kona/Player-Data/Knn-Data/knn-1/tcga-pancan-data"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert TCGA-PANCAN-HiSeq data into Kona dataset files."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE,
        help="Directory containing data.csv and labels.csv",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Destination Kona dataset directory",
    )
    parser.add_argument(
        "--test-count",
        type=int,
        default=1,
        help="Number of test samples to place into P1-0-X/Y-Test",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=20260724,
        help="Shuffle seed for train/test split",
    )
    parser.add_argument(
        "--scale",
        type=int,
        default=10000,
        help="Multiply each floating-point feature by this before rounding",
    )
    return parser.parse_args()


def read_labels(labels_csv: Path) -> tuple[list[str], list[str]]:
    sample_ids: list[str] = []
    labels: list[str] = []
    with labels_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader)
        if len(header) < 2:
            raise ValueError("labels.csv must have at least two columns")
        for row in reader:
            if not row:
                continue
            sample_ids.append(row[0].strip())
            labels.append(row[1].strip())
    return sample_ids, labels


def read_data(data_csv: Path, scale: int) -> tuple[list[str], list[list[int]]]:
    sample_ids: list[str] = []
    rows: list[list[int]] = []
    with data_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader)
        if len(header) < 2:
            raise ValueError("data.csv must have sample id + features")
        for row in reader:
            if not row:
                continue
            sample_ids.append(row[0].strip())
            features = [int(round(float(x) * scale)) for x in row[1:]]
            rows.append(features)
    return sample_ids, rows


def build_label_map(labels: list[str]) -> dict[str, int]:
    mapping: dict[str, int] = {}
    for label in labels:
        if label not in mapping:
            mapping[label] = len(mapping)
    return mapping


def write_matrix(path: Path, matrix: list[list[int]]) -> None:
    with path.open("w", encoding="utf-8") as f:
        for row in matrix:
            f.write(" ".join(str(x) for x in row))
            f.write("\n")


def write_vector(path: Path, values: list[int]) -> None:
    with path.open("w", encoding="utf-8") as f:
        for value in values:
            f.write(f"{value}\n")


def main() -> None:
    args = parse_args()
    data_csv = args.source_dir / "data.csv"
    labels_csv = args.source_dir / "labels.csv"

    if not data_csv.exists():
        raise FileNotFoundError(f"missing {data_csv}")
    if not labels_csv.exists():
        raise FileNotFoundError(f"missing {labels_csv}")

    label_sample_ids, label_names = read_labels(labels_csv)
    data_sample_ids, data_rows = read_data(data_csv, args.scale)

    if label_sample_ids != data_sample_ids:
        raise ValueError(
            "sample order mismatch between data.csv and labels.csv; "
            "please align them before import"
        )

    total_samples = len(data_rows)
    if args.test_count <= 0 or args.test_count >= total_samples:
        raise ValueError("test-count must satisfy 1 <= test_count < total_samples")

    label_map = build_label_map(label_names)
    encoded_labels = [label_map[label] for label in label_names]

    indices = list(range(total_samples))
    rng = random.Random(args.seed)
    rng.shuffle(indices)

    test_indices = sorted(indices[: args.test_count])
    train_indices = sorted(indices[args.test_count :])

    train_x = [data_rows[i] for i in train_indices]
    train_y = [encoded_labels[i] for i in train_indices]
    test_x = [data_rows[i] for i in test_indices]
    test_y = [encoded_labels[i] for i in test_indices]

    num_features = len(train_x[0])
    out_dir = args.output_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    with (out_dir / "Knn-meta").open("w", encoding="utf-8") as f:
        f.write(f"{num_features} {len(train_x)} {len(test_x)}\n")

    write_matrix(out_dir / "P0-0-X-Train", train_x)
    write_vector(out_dir / "P0-0-Y-Train", train_y)
    write_matrix(out_dir / "P1-0-X-Test", test_x)
    write_vector(out_dir / "P1-0-Y-Test", test_y)

    with (out_dir / "label_map.txt").open("w", encoding="utf-8") as f:
        for label, idx in label_map.items():
            f.write(f"{idx}\t{label}\n")

    print("Imported dataset into:", out_dir)
    print("features:", num_features)
    print("train samples:", len(train_x))
    print("test samples:", len(test_x))
    print("labels:", len(label_map))
    print("scale:", args.scale)
    print("seed:", args.seed)


if __name__ == "__main__":
    main()
