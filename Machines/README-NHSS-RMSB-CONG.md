# NHSS + Rotated MSB + Cong integration

## Current classifier and benchmark (2026-11-04)

Confirmed composition: **NHSS distance → Cong with Rotated-MSB on EVERY layer → batched DCF vote → winning-label secret share**. No network DCF switch or 6000 cutoff applies to Shao. Original KONA and the separate PCR/DCF pruned hybrid keep their own comparison policy.

Existing files:

- `kona-nhss-distance.hpp`: NHSS/NISED real local online distance, no online server communication.
- `kona-cong-rotated-msb-adapter.hpp`: original pure-RMSB Top-k adapter; prepared plan/execution also allows network construction before timing.
- `kona-nhss-cong-pipeline.hpp`: original NHSS→Cong wrapper; `classify()` now adds DCF vote and returns only winning-label share.
- `kona-mask-vote.hpp`: shared public-category masked counts and smallest-label tie rule. PCR and DCF both batch all category/predicate pairs together; DCF is used for Shao counts and winner selection, PCR remains used for the separate pruned hybrid.
- `kona-cong-rmsb-dataset-size-bench.cpp`: historical synthetic-distance, real-N pure-RMSB **Top-k-only** benchmark. No distance/vote is silently added here. It now binds CPU0/2 directly.
- `knn-bench.cpp --scheme shao`: current actual full/staged classifier benchmark, using real dataset labels.

Direct launch uses the existing organizer, no additional entry files:

```bash
python3 -u Scripts/run-knn-experiment.py --scheme shao
```

Defaults: Arcene N199, WAN only, k1/2/3/5/20/100, three repeats, rotated k order, C++ CPU0/2. Default staged plan has 3 real NHSS distance pairs plus 18 Top-k/vote pairs. Top-k/vote uses true dataset distances prepared as test shares OUTSIDE timing; it does not repeat NHSS distance work per k. Any total composed from these stages must be marked composed. `--scope full` instead runs real continuous NHSS→Top-k→vote per k, 18 WAN pairs, independently measures KNN online total and stages. NHSS offline keygen, sharing and query tokens are excluded from online time but may take substantial preparation time at full Arcene dimensions.

Existing deterministic dealer material, NHSS RNG seed and L2 compare/multiplication limitations remain; this is a research performance path, not a new security certification. The DCF vote is our integration choice; do not label it a verbatim reproduction of the paper's vote without a separate paper audit. Historical four-dataset results remain Top-k-only.


This bundle is intentionally split into **4 implementation headers + 1 correctness
test**.  It reuses the Cong network and compare-then-exchange implementation
already present in the `kona` branch.

## Files

| File | Role |
|---|---|
| `kona-rotated-msb-compare.hpp` | Shao/Zhang Rotated MSB comparison, adapted from subtractive to Kona additive `Z2<64>` shares; one bit-sliced batch per Cong level |
| `kona-nhss-distance.hpp` | NHSS five-algorithm core + NISED squared Euclidean distance; online server-to-server communication is zero |
| `kona-cong-rotated-msb-adapter.hpp` | plugs Rotated MSB into the existing generic Cong network and reuses existing Kona secret swap |
| `kona-nhss-cong-pipeline.hpp` | NHSS distance -> `(distance,label)` shares -> Cong -> Rotated MSB |
| `kona-nhss-distance-test.cpp` | dealer + two-share local correctness test for 1D and multidimensional NHSS distance |

Put all five files under `Kona/Machines/`.

## Architecture

```text
client / preprocessing
  NHSS.KeyGen
  NHSS.Share(x)
  x^2 subtractive shares
  NHSS.TokenGen(y)
  y^2 subtractive shares

                     ONLINE

S0 local                         S1 local
   \                                /
    \--- NHSS.LocalCalc + DDlog ---/
              no S0<->S1 traffic
                       |
                       v
        additive Z2<64> distance shares
                       |
                       v
                 Cong network
                       |
          whole comparator level
                       |
                       v
             Rotated MSB batch
                       |
                       v
          arithmetic comparison flags
                       |
                       v
     existing ss_vec_kona_l2 secret swap
```

## Why the Rotated-MSB code differs slightly from the paper

The paper uses subtractive sharing, `m = m1 - m0`.  Kona's current Cong/PCR
path uses additive `Z2<64>` sharing.  Therefore, for a comparison `left < right`
each server first computes its local additive-share difference

```text
d_b = left_b - right_b
```

and the two bit-decomposed operands satisfy `d = d_0 + d_1 (mod 2^64)`.
The carry recurrence in Algorithm 2 is unchanged.

The implementation **does not reconstruct the comparison flag**.  This is the
SETSA / oblivious variant needed by Cong: the XOR-shared flag is converted to
an arithmetic share and fed into the existing compare-then-exchange code.

## Benchmark timing boundary

Keep the same timing rule as the rest of the repository:

**offline / excluded from online**
- `NHSS.KeyGen`
- `NHSS.Share(x)`
- `x^2` sharing
- `NHSS.TokenGen(y)`
- `y^2` sharing
- reading files / constructing benchmark inputs

**online**
- `NHSS.LocalCalc`
- `NHSS.DDlog`
- local distance aggregation
- Cong network
- Rotated MSB ANDs / B2A
- compare-then-exchange

NHSS NISED itself should report:
- `online_nhss_distance_ms`
- `distance_sent_bytes = 0`
- `distance_logical_rounds = 0`
- `distance_transport_rounds = 0`

Top-k communication is reported separately and then included in total online
pipeline statistics.

## Batch controls

The pipeline exposes two independent batch sizes:

- `dimension_batch_size`: dimensions processed per local NHSS block for one
  sample. `0` means all dimensions in one logical block. This never changes
  NHSS communication: distance sent bytes and rounds remain zero.
- `compare_batch_size`: comparator pairs processed per Rotated-MSB/secret-swap
  block inside one Cong level. `0` selects the repository default. Values
  larger than `KONA_DEFAULT_BATCH_CHUNK` are capped at that common limit.

The current common limit is `16384`. A Cong level at or below the effective
limit is processed whole; a larger level is split into consecutive blocks.
Comparators within a level are disjoint, so chunking changes transport batch
size/round count without changing the Cong network or comparison semantics.

## First validation sequence

1. Compile and run `kona-nhss-distance-test.cpp` first.
2. Add a tiny two-party Rotated-MSB comparison check using the same networking
   harness as `kona-pcr-compare-test.cpp`; test equal values, adjacent values,
   `0`, and values close to but below `2^63`.
3. Run the same clear `(N,k)` input through `Cong+PCR` and `Cong+RotatedMSB`.
   Open only the final test outputs and require identical top-k distances and
   payloads.
4. Only after correctness matches, add NHSS input preprocessing and compare
   `NHSS+Cong+RotatedMSB` against the existing Kona distance+Cong+PCR path.
5. Then run the formal LAN/WAN timing experiments.

## Dependencies / build note

`kona-nhss-distance.hpp` uses GMP C++ (`gmpxx`) in addition to GMP.  MP-SPDZ
already depends on GMP, but the executable that directly instantiates the NHSS
code may need `-lgmpxx -lgmp` in its link rule.

The random generator in this reference implementation is deterministic
`gmp_randclass` for reproducible experiments; it is not a production CSPRNG.

## Recommended experiment outputs

At minimum print:

```text
dataset
n
features
k

nhss_keygen_ms                 # offline
nhss_data_share_setup_ms       # offline
nhss_token_setup_ms            # offline

online_nhss_distance_ms
online_cong_rmsb_topk_ms
online_total_ms

distance_sent_bytes
topk_sent_bytes
sent_bytes

distance_logical_rounds
topk_logical_rounds
logical_rounds

distance_transport_rounds
topk_transport_rounds
transport_rounds
```

Run P0 and P1 as the existing benchmarks do, one process pinned to CPU 0 and
the other to CPU 2.
