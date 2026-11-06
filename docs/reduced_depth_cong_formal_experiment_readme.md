> 2026-11-03：本页以下为历史入口说明。当前模块与唯一bench见 [matrix_modules.md](matrix_modules.md)。旧可执行名/脚本仅兼容转发，原结果不改写。

# Formal old-Cong vs reduced-depth-Cong benchmark

This bundle contains:

- `kona-reduced-depth-cong-bench.cpp`
- `run-reduced-depth-cong-experiment.sh`
- `reduced-depth-cong-cases.csv`
- `Makefile.fragment`

It assumes the previously generated file is placed at:

```text
Machines/kona-reduced-depth-cong-topk.hpp
```

## Timing scope

Online timing starts after:

- deterministic input shares have already been prepared;
- network construction / structural planning has completed;
- DCF key/material has been loaded and `Compare64` constructed;
- padding buffers for the modified network are prepared.

Online timing includes:

- every secure comparison;
- every compare-and-swap in the per-block Cong phase;
- all real socket communication;
- DCF evaluation;
- local candidate extraction;
- matrix rank accumulation;
- secure `rank < K` selection.

Offline / setup fields are not included in `online_ms`.

`dcf_key_init_ms` is reported separately.

## Why the new network has an extra rank-mask comparator count

The original Cong network materializes K winning items at the tail.

The modified design finishes with an all-pairs matrix. Pairwise comparisons give
secure ranks, then one additional batch of `candidate_count` comparisons tests:

```text
rank < K
```

Therefore the formal modified-network count used by this benchmark is:

```text
block_comparators
+ matrix_comparators
+ rank_mask_comparators
```

not only the structural matrix pair count.

This is deliberately conservative: it does not hide the final secure selection
cost.

## Important output-scope note

Old Cong output:

```text
materialized_topk_tail
```

Modified Cong output:

```text
topk_membership_mask
```

The modified benchmark does not perform an oblivious compaction from the mask
into K contiguous secret-shared records. That operation is not part of the
frozen matrix-finalization rule and would change the algorithm being studied.

For the network trade-off experiment, compare the online cost up to secure
Top-k membership. If a later end-to-end KNN pipeline needs compact K records,
benchmark that compaction/voting integration separately.

## Core metrics

Always retain:

- `online_ms`
- `compute_ms`
- `comm_ms`
- `sent_bytes`
- `transport_rounds`
- `logical_rounds`
- `comparator_count`
- `structural_depth`

For the modified network additionally retain:

- `block_comparators`
- `matrix_comparators`
- `rank_mask_comparators`
- `candidate_count`
- `block_depth`
- `block_ms`
- `matrix_rank_ms`
- `rank_mask_ms`
- communication/bytes/rounds for each phase

For DCF additionally retain:

- `dcf_evaluate_calls`
- `dcf_evaluate_ms`
- `dcf_key_init_ms` (setup only)

These metrics let you distinguish:

1. latency saving from fewer interactive stages;
2. bandwidth cost from more same-stage comparisons;
3. local DCF evaluation cost from comparator growth;
4. chunking effects when a DCF batch exceeds its internal maximum size.

## Formal run examples

WAN, the most important first experiment:

```bash
bash Scripts/run-reduced-depth-cong-experiment.sh \
  --profile wan \
  --cases Scripts/reduced-depth-cong-cases.csv \
  --modes old-dcf,new-dcf \
  --repeats 5 \
  --cpu0 0 --cpu1 2
```

LAN DCF:

```bash
bash Scripts/run-reduced-depth-cong-experiment.sh \
  --profile lan \
  --cases Scripts/reduced-depth-cong-cases.csv \
  --modes old-dcf,new-dcf \
  --repeats 5 \
  --cpu0 0 --cpu1 2
```

PCR cross-check:

```bash
bash Scripts/run-reduced-depth-cong-experiment.sh \
  --profile lan \
  --cases Scripts/reduced-depth-cong-cases.csv \
  --modes old-pcr,new-pcr \
  --repeats 5 \
  --cpu0 0 --cpu1 2
```

The runner checks `/sys/.../topology/` and refuses to use two SMT siblings of
the same physical core.

Repository network profiles are reused:

- LAN: 1 Gbit/s + 0.5 ms one-way
- WAN: 40 Mbit/s + 20 ms one-way
