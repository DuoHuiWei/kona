> 2026-11-03：本页以下为历史入口说明。当前模块与唯一bench见 [matrix_modules.md](matrix_modules.md)。旧可执行名/脚本仅兼容转发，原结果不改写。

# Reduced-depth + Hybrid formal experiment

## Files

Place:

```text
Machines/kona-reduced-depth-hybrid-adapter.hpp
Machines/kona-reduced-depth-hybrid-bench.cpp
Scripts/run-reduced-depth-hybrid-experiment.sh
Scripts/reduced-depth-hybrid-cases-core.csv
```

`kona-reduced-depth-cong-topk.hpp` owns the public block plan, full matrix
pair list, and pruned matrix pair list so the benchmark statistics and
execution consume the same plan.

The repository generic `Machines/*.cpp -> *.x` Makefile rule should build:

```bash
make -j2 kona-reduced-depth-hybrid-bench.x
```

## Provisional switching rule

Frozen for this round:

```text
LAN threshold = 190 comparators/stage
WAN threshold = 6000 comparators/stage

width <= threshold  -> DCF
width >  threshold  -> PCR
```

"width" always means real comparator pairs:

```cpp
compare_idx_vec.size() / 2
```

The same policy is applied to:

- every old Cong level;
- every fused reduced-depth block level;
- the reduced-depth matrix-rank stage;
- the reduced-depth `rank < K` stage.

This keeps old/new comparison fair.

## Thresholds are parameterized

Defaults:

```bash
--lan-threshold 190
--wan-threshold 6000
```

Hard override:

```bash
--threshold 512
```

A non-zero `--threshold` ignores the profile threshold. This is useful later for
threshold sweeps without recompiling.

## Formal timing scope

Outside online timing:

- deterministic shared-input construction;
- network-plan construction;
- DCF `Compare64` construction / key loading.
- pre-timer two-party synchronization;
- public index/flattened-level/matrix-pair construction;
- correctness reveal and logging.

Inside online timing:

- hybrid PCR/DCF comparison;
- compare-and-swap;
- matrix comparison;
- rank accumulation;
- secure `rank < K`;
- real communication;
- DCF evaluate.

`dcf_key_init_ms` remains reported separately.

## New hybrid-specific statistics

Summary row:

```text
threshold

pcr_stages
dcf_stages
pcr_comparators
dcf_comparators
backend_switches

pcr_stage_ms
dcf_stage_ms
pcr_stage_comm_ms
dcf_stage_comm_ms
```

`noncomm_api_ms` is `online_ms - comm_ms`. It is the non-communication-API
remainder, not pure cryptographic compute time.

Transport fields:

```text
transport_rounds   raw one-way send/receive actions recorded by Player stats
logical_rounds     complete paired send+receive exchanges
structural_depth   comparison-network depth; not a communication count
```

The implementation also retains `transport_events` and `transport_actions` as
diagnostic fields. For this benchmark all online communication is a paired
send/receive exchange, so `transport_rounds=transport_events` and
`logical_rounds=transport_actions=transport_rounds/2`. This is a verified
property of the current adapter, not a definition to apply to an unpaired API.

Every secure stage also prints a `LEVEL` line:

```text
phase=
stage=
comparators=
backend=
stage_ms=
stage_comm_ms=
sent_bytes=
transport_events=
transport_actions=
transport_rounds=
logical_rounds=
```

These lines are retained in both `P0.log` and `P1.log` after the online timer
has stopped, so later you can inspect which levels changed backend after
reduced-depth fusion without logging inside the online interval.

## Matrix modes

```text
old-hybrid             Original Cong + Hybrid; output_scope=materialized_topk_tail
full-matrix-hybrid     reduced-depth blocks + full candidate matrix + Hybrid
pruned-matrix-hybrid   same blocks + pruned cross-block matrix + Hybrid
```

For the two matrix modes:

```text
total_comparators =
  block_comparators + matrix_cross_comparators + rank_mask_comparators

structural_depth = block_depth + 2
```

The pruned matrix compares candidates from different blocks only when their
one-based block positions satisfy `u + v <= K + 1`; each candidate starts with
the public rank contribution `u - 1`.

## Recommended first run

LAN is the important one now because pure DCF lost there while pure PCR mostly
won:

```bash
bash Scripts/run-reduced-depth-hybrid-experiment.sh \
  --profile lan \
  --cases Scripts/reduced-depth-hybrid-cases-core.csv \
  --modes old-hybrid,full-matrix-hybrid,pruned-matrix-hybrid \
  --repeats 3 \
  --cpu0 0 --cpu1 2
```

Then WAN:

```bash
bash Scripts/run-reduced-depth-hybrid-experiment.sh \
  --profile wan \
  --cases Scripts/reduced-depth-hybrid-cases-core.csv \
  --modes old-hybrid,full-matrix-hybrid,pruned-matrix-hybrid \
  --repeats 3 \
  --cpu0 0 --cpu1 2
```
