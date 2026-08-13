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
