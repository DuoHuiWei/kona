# Kona Codex Agent Guide

## Environment Contract

- Editable source repository: `/home/u7231/kona-work/Kona`
- Existing Docker build container: `kona-dev`
- Source directory inside the container: `/usr/src/Garnet`
- Persistent source changes must be made in the WSL Git repository only.
- Do not edit source files only inside the container.
- Do not install Kona/Garnet dependencies directly in WSL.
- Do not create a new Docker image or container unless the user explicitly requests it.
- Do not use `sudo` for Docker commands.

## Required Workflow

All code edits happen in WSL:

```bash
cd /home/u7231/kona-work/Kona
```

Before compiling or testing, sync and build through the existing container:

```bash
bash Scripts/codex-container-build.sh
```

Run commands inside the build environment with:

```bash
docker exec kona-dev bash -lc 'cd /usr/src/Garnet && <command>'
```

For long implementation tasks, start by confirming the container and baseline build:

```bash
docker ps
bash Scripts/codex-container-build.sh
```

Then make source edits in WSL, re-run `bash Scripts/codex-container-build.sh`, and execute tests through `docker exec kona-dev ...`.

## Container Build Notes

- `Scripts/codex-container-build.sh` is the source-of-truth sync/build entrypoint.
- The script copies the current WSL repository state into `/usr/src/Garnet` in `kona-dev`.
- Container-only edits are not persistent and must not be used as final source changes.
- The current verified build target is `kona.x`.
- If a normal sandbox command reports Docker socket `permission denied`, treat it as an execution-permission issue, not a source or dependency issue; use the approved Docker command path for the existing `kona-dev` container.

## Module Rules

- Prefer directly reusing existing Kona/Garnet components and code paths whenever possible instead of re-implementing logic from scratch.
- When an existing component is functionally correct but not parallel enough for the current experiment, it is acceptable to add a new parallelized version; keep the new version close in logic and interface to the original so the comparison remains fair.
- For new protocol or benchmark modules, preserve real logic, real communication, and real byte-count effects. Masks, randomness, and Beaver-style `a/b/c` values may be simplified to zero only when the user explicitly allows L2-style checking.
- For the basic modules written in this project, the default requirement is:
  - logic must be real
  - communication must be real
  - byte counts must be real
  - only masks, randomness, and similar auxiliary secret material may be simplified to zero when the user explicitly allows it
- If a new module deviates from the original flow, document the deviation clearly in comments or docs so later performance comparisons stay interpretable.

## Benchmark Rules

- Default to running one scheme at a time, as requested by the user on 2026-11-04. Do not start another scheme while its experiment is active.
- Freeze measured source files, shared launchers, and build scripts throughout a formal experiment. Do not develop or refactor a different scheme through shared files while the user is running a batch; keep drafts outside the active source tree until the run ends.
- If an experiment is interrupted, preserve completed results and verify source/binary/input identities before recovering missing trials. Never disable source-change checks to accept mixed builds.

- Fixed CPU mapping for this project: P0 uses logical CPU 0; P1 uses logical CPU 2. Single-process setup/structure experiments use CPU 0. Do not automatically select replacement CPUs or accept other CPU overrides.
- Preserve the location of existing affinity controls (C++ or launcher). For newly added or subsequently modified experiments, prefer binding in the C++ bench before networking, worker creation, and timing via `Machines/kona-bench-affinity.hpp`; use the launcher/command only when the bench cannot control it, and document why. Conflicting inherited affinity must fail rather than be silently overridden.
- Affinity is not physical-core exclusivity. Record topology and isolation evidence; never claim strict core exclusivity merely because taskset/sched_setaffinity succeeded. See `docs/bench_cpu_affinity.md` and section 13 of `docs/bench_recording_protocol.md`.
- Every benchmark/test entry should record detailed metrics, not just total runtime.
- Benchmark output should include whenever meaningful:
  - dataset / scale / backend
  - total runtime
  - average runtime
  - per-item or per-compare time
  - communication rounds
  - transmitted bytes / sent bytes
  - protocol-specific counters such as `call_evaluate_nums`
  - phase-level timing breakdown, for example read/setup/compute/preprocess phases
- Prefer benchmark designs that call original components directly and only wrap them with timing / accounting.
- If there is any ambiguity about what should be measured or which phases belong in a benchmark, ask the user before proceeding.
- For comparative benchmarks, prefer interleaved execution over running all trials of one scheme before the next. Report raw trial order plus mean/median/std/CV/min/max when results are noise-sensitive.
- Use structure-invariant control cases when available. If comparator count, depth, bytes, rounds, and backend split are unchanged, do not attribute timing differences to algorithmic changes without same-run interleaved evidence.
- Treat cross-date benchmark comparisons as provisional. Strong performance claims should come from the same script, same build, same network profile, same CPU affinity, and interleaved runs.
- When a regression appears, inspect stage-level timing first; do not tune thresholds or change algorithms before locating whether the issue is PCR, DCF, swap, communication, or system scheduling.

## Baseline Command Pattern

Run Kona Arcene P0/P1 in the existing container:

```bash
docker exec kona-dev bash -lc '
cd /usr/src/Garnet
mkdir -p KNN-experiment-res
rm -f KNN-experiment-res/codex_baseline_P0_kona.log KNN-experiment-res/codex_baseline_P1_kona.log
./kona.x 0 -pn 10000 -h localhost > KNN-experiment-res/codex_baseline_P0_kona.log 2>&1 &
pid0=$!
./kona.x 1 -pn 10000 -h localhost > KNN-experiment-res/codex_baseline_P1_kona.log 2>&1 &
pid1=$!
wait "$pid0"
s0=$?
wait "$pid1"
s1=$?
echo "P0_EXIT=$s0 P1_EXIT=$s1"
exit $((s0 || s1))
'
```

Inspect logs with:

```bash
docker exec kona-dev bash -lc 'cd /usr/src/Garnet && sed -n "1,160p" KNN-experiment-res/codex_baseline_P0_kona.log'
docker exec kona-dev bash -lc 'cd /usr/src/Garnet && sed -n "1,160p" KNN-experiment-res/codex_baseline_P1_kona.log'
```

## Last Verified Baseline

The following baseline was observed after running `bash Scripts/codex-container-build.sh` and the two-party Kona Arcene test in `kona-dev`:

- Build status: success
- P0 exit: `0`
- P1 exit: `0`
- Party 0 total time: `0.0761799 seconds`
- DCF evaluation total time: `0.068294 seconds`
- `call_evaluate_nums`: `2018`
- P0 communication: `0.071456 MB`
- P0 rounds: `89 online round`
- P1 total time: `0.0762989 seconds`
- P1 communication: `0.071448 MB`
- P1 rounds: `88 online round`
- P1 accuracy: `1`

## PCR/Cong Implementation Guardrails

- Prefer additive-share and `Z2<64>` style consistent with existing Kona code.
- Reuse existing Kona communication patterns (`octetStream`, `RealTwoPartyPlayer`, send/receive) when adding components.
- Reuse Kona multiplication where requested by the user; do not silently replace it with a different backend.
- If adding share conversion or PCR/Cong components, integrate them from WSL source files and validate only through the `kona-dev` container.
- For backend changes such as DCF replacement, prefer a reversible integration path until the user explicitly asks to remove the old path.

## Current Experiment Entries (2026-11-04)

- The current ConstantPP / SecKNN / original KONA / pruned-hybrid / Shao workflow has exactly two maintained experiment files: `Machines/knn-bench.cpp` and `Scripts/run-knn-experiment.py`.
- Launch the Python script directly; do not add shell launchers, per-scheme scripts, benchmark wrappers, or new per-scheme build targets. Extend its scheme/scope arguments when needed.
- Existing protocol modules, independent component regressions, network/build/log utilities and historical result archives remain separate dependencies, not additional experiment launch entries.
- Freeze related source, scripts and build identity throughout an active batch. Do not edit/rebuild them during a user's experiment. Run schemes serially.

- Shao classifier: NHSS distance, pure Rotated-MSB Cong on all layers, batched DCF category vote. Do not apply PCR/DCF hybrid width cutoffs to this scheme.

- ConstantPP performance experiments use `--secmax-mode paper-local-l2` by default. Preserve existing recorded results; legacy is only for an explicitly requested historical/control comparison. Do not silently switch or relabel prior runs.

- Clarified KONA performance baseline: the user means the pre-reinstall cached/batched-DCF mainpath (`Machines/kona-mainpath-esd-topk-dcf-bench.cpp`), not the native per-evaluation key-file implementation. Full-flow runs must explicitly select `--kona-backend batched-dcf` and verify that exact mode in raw logs. Keep `original` only as an explicitly named native control; never silently substitute it when a batch-DCF scope is unsupported.
- SecKNN correction (explicit user instruction, 2026-11-06): preserve `KNN_party_SecKNN::top_1()` native serial `(i,last)` scan for both Top-k and vote maximum, including strict-comparison tie order. The historical layered/tournament substitution is disabled in maintained entries. Independent send/receive overlap is permitted; replacing the selection graph is not. Results marked `secknn-layer-batched` / `topk_mode=layered` are historical algorithm variants and must not be presented as original SecKNN with engineering-only changes. This supersedes the earlier acceptance of layered mode.

## Independent-message transport (2026-11-04)

- Formal WAN is 40 Mbps per direction with 20 ms added one-way delay. Temporary 10 ms diagnostic settings must be cleared; preserve historical result labels.
- In performance paths, when both outgoing messages are ready and neither depends on the incoming message, use `RealTwoPartyPlayer::exchange_concurrent` to reuse the framed nonblocking Exchanger. Keep dependent response steps in their original order.
- Preserve payload, chunk size, algorithms and comparison/swap policy. The new API counts two directional actions and one logical exchange; overlapping communication time is recorded once. Do not double sent bytes or infer a rate-limit error from one party's bytes divided by a serialized two-direction phase duration.
- Retain explicitly named native historical controls; the current KONA performance baseline remains explicit `--kona-backend batched-dcf`. Freeze networking/exchanger/octetStream source identities during experiments.

## Parameter and round accounting (2026-11-06)

- Maintained shared defaults live in `Machines/kona-bench-config.hpp`; C++ components and the Python organizer must read them instead of duplicating threshold/chunk literals.
- Exactly two round meanings: `theoretical_rounds` counts dependency stages without chunk expansion; `exchange_rounds` counts actual communication exchanges. `transport_actions` is a raw send/receive action count used only for checks, never a third round metric.
- Resolve backend overrides in common argument parsing, verify effective configuration in logs, and reject unsupported combinations instead of silently ignoring them. Preserve protocol graphs, swaps, tie behavior, and current thresholds.

2026-11-07确认：SecKNN保持原生串行Top-k、原生非缓存/非批量DCF比较器（包含原有重复密钥加载），以及原始频数统计和串行投票最大值；只保留独立消息同时收发的工程修改，不引入缓存、网络替换或k=1投票捷径。checkpoints必须显式传递并核对vote-order。C++对未知或不适用的参数必须在联网前拒绝；hybrid vote不接受选择比较器/阈值覆盖，启动器也不传这些无效参数。
