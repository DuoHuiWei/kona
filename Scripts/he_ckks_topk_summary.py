#!/usr/bin/env python3
import csv
from collections import Counter
from pathlib import Path


ROOT = Path("/home/u7231/kona-work/Kona")
INPUT_CSV = ROOT / "KNN-experiment-res" / "he_ckks_tcga_query0_distances.csv"
OUT_DIR = ROOT / "KNN-experiment-res"

MINIMAL_CSV = OUT_DIR / "he_ckks_tcga_query0_minimal.csv"
SORTED_CSV = OUT_DIR / "he_ckks_tcga_query0_sorted_by_distance.csv"
TOPK_PATHS = {
    5: OUT_DIR / "he_ckks_tcga_query0_topk_5.csv",
    8: OUT_DIR / "he_ckks_tcga_query0_topk_8.csv",
    50: OUT_DIR / "he_ckks_tcga_query0_topk_50.csv",
}


def load_rows():
    with INPUT_CSV.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        raise RuntimeError("input csv is empty")
    return rows


def write_csv(path: Path, fieldnames, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def majority_vote(rows):
    counts = Counter(row["label"] for row in rows)
    top_label, top_votes = max(counts.items(), key=lambda x: (x[1], x[0]))
    return top_label, top_votes, counts


def main():
    rows = load_rows()
    query_label = rows[0]["query_label"]
    minimal_rows = [
        {
            "train_idx": row["train_idx"],
            "train_sample_id": row["train_sample_id"],
            "label": row["label"],
            "ckks_distance": row["ckks_distance"],
        }
        for row in rows
    ]
    write_csv(
        MINIMAL_CSV,
        ["train_idx", "train_sample_id", "label", "ckks_distance"],
        minimal_rows,
    )

    sorted_rows = sorted(
        rows,
        key=lambda row: (float(row["ckks_distance"]), int(row["train_idx"])),
    )
    sorted_export_rows = [
        {
            "rank": idx + 1,
            "train_idx": row["train_idx"],
            "train_sample_id": row["train_sample_id"],
            "label": row["label"],
            "ckks_distance": row["ckks_distance"],
            "plaintext_distance": row["plaintext_distance"],
            "abs_error": row["abs_error"],
        }
        for idx, row in enumerate(sorted_rows)
    ]
    write_csv(
        SORTED_CSV,
        [
            "rank",
            "train_idx",
            "train_sample_id",
            "label",
            "ckks_distance",
            "plaintext_distance",
            "abs_error",
        ],
        sorted_export_rows,
    )

    print(f"QUERY_LABEL={query_label}")
    print(f"TOTAL_ROWS={len(rows)}")
    print(f"MINIMAL_CSV={MINIMAL_CSV}")
    print(f"SORTED_CSV={SORTED_CSV}")

    for k, path in TOPK_PATHS.items():
        topk = sorted_export_rows[:k]
        write_csv(
            path,
            [
                "rank",
                "train_idx",
                "train_sample_id",
                "label",
                "ckks_distance",
                "plaintext_distance",
                "abs_error",
            ],
            topk,
        )
        voted_label, top_votes, counts = majority_vote(topk)
        correct = voted_label == query_label
        counts_str = ",".join(
            f"{label}:{counts[label]}" for label in sorted(counts.keys())
        )
        print(
            f"K={k} TOPK_CSV={path} VOTED_LABEL={voted_label} "
            f"TOP_VOTES={top_votes} COUNTS={counts_str} CORRECT={'YES' if correct else 'NO'}"
        )


if __name__ == "__main__":
    main()
