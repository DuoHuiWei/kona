#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
HOST_ROOT="$(git rev-parse --show-toplevel)"
CONTAINER_ROOT="/usr/src/Garnet"
PORT_BASE="${KONA_PORT_BASE:-10000}"
LOG_DIR="${KONA_LOG_DIR:-KNN-experiment-res}"
TARGET="${KONA_ESD_TARGET:-kona-esd-bench.x}"
P0_LOG="$LOG_DIR/codex_esd_bench_P0.log"
P1_LOG="$LOG_DIR/codex_esd_bench_P1.log"

cd "$HOST_ROOT"

mkdir -p "$LOG_DIR"

echo "[1/4] Sync and baseline build via Scripts/codex-container-build.sh"
bash Scripts/codex-container-build.sh

echo "[2/4] Build $TARGET inside $CONTAINER"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    make -j8 '$TARGET'
"

echo "[3/4] Run $TARGET with one query against all train samples"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    mkdir -p '$LOG_DIR'
    rm -f '$P0_LOG' '$P1_LOG'
    ./'$TARGET' 0 -pn '$PORT_BASE' -h localhost > '$P0_LOG' 2>&1 &
    pid0=\$!
    ./'$TARGET' 1 -pn '$PORT_BASE' -h localhost > '$P1_LOG' 2>&1 &
    pid1=\$!
    wait \"\$pid0\"
    s0=\$?
    wait \"\$pid1\"
    s1=\$?
    echo \"P0_EXIT=\$s0 P1_EXIT=\$s1\"
    exit \$((s0 || s1))
"

echo "[4/5] Copy logs and CSV back to WSL workspace"
docker cp "$CONTAINER:$CONTAINER_ROOT/$P0_LOG" "$P0_LOG"
docker cp "$CONTAINER:$CONTAINER_ROOT/$P1_LOG" "$P1_LOG"
docker cp "$CONTAINER:$CONTAINER_ROOT/$LOG_DIR/codex_esd_distances.csv" \
    "$LOG_DIR/codex_esd_distances.csv"

echo "[5/5] Show benchmark summaries"
echo '===== P0 ====='
sed -n '1,80p' "$P0_LOG"
echo '===== P1 ====='
sed -n '1,80p' "$P1_LOG"
echo '===== DISTANCE CSV HEAD ====='
sed -n '1,12p' "$LOG_DIR/codex_esd_distances.csv"
