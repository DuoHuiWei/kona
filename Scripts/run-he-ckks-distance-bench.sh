#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_HE_CONTAINER:-kona-he-dev}"
HOST_ROOT="$(git rev-parse --show-toplevel)"
CONTAINER_ROOT="${KONA_HE_CONTAINER_ROOT:-/usr/src/Garnet}"
BUILD_DIR="${CONTAINER_ROOT}/build/hecompare"
LOG_DIR="${HOST_ROOT}/KNN-experiment-res"
LOG_FILE="${LOG_DIR}/he_ckks_tcga_distance_bench.log"
CSV_FILE="${LOG_DIR}/he_ckks_tcga_query0_distances.csv"
START_TRAIN_INDEX="${HE_CKKS_START_TRAIN_INDEX:-1}"
MAX_CANDIDATES="${HE_CKKS_MAX_CANDIDATES:-12}"
APPEND_OUTPUT="${HE_CKKS_APPEND_OUTPUT:-0}"

cd "$HOST_ROOT"
mkdir -p "$LOG_DIR"

echo "[1/5] Check container"
docker start "$CONTAINER" >/dev/null 2>&1 || true

echo "[2/5] Sync source into HE container"
tar \
    --exclude=".git" \
    --exclude="Player-Data" \
    --exclude="KNN-experiment-res" \
    --exclude="build" \
    --exclude="*.o" \
    --exclude="*.x" \
    -C "$HOST_ROOT" \
    -cf - . \
| docker exec -i "$CONTAINER" tar -C "$CONTAINER_ROOT" -xf -

echo "[3/5] Configure and build HECompare"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    rm -rf build/hecompare
    bash Scripts/build-he.sh
"

echo "[4/5] Run CKKS TCGA distance benchmark"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    mkdir -p KNN-experiment-res
    '$BUILD_DIR/openfhe-tcga-ckks-distance-benchmark' \
        '/workspace/Kona/HECompare/testdata/tcga-pancan-raw/data.csv' \
        '/workspace/Kona/HECompare/testdata/tcga-pancan-raw/labels.csv' \
        '/workspace/Kona/KNN-experiment-res/he_ckks_tcga_query0_distances.csv' \
        '$START_TRAIN_INDEX' \
        '$MAX_CANDIDATES' \
        '$APPEND_OUTPUT' \
        > '/workspace/Kona/KNN-experiment-res/he_ckks_tcga_distance_bench.log' 2>&1
"

echo "[5/5] Results are already written to the mounted workspace"

echo '===== HE CKKS BENCH ====='
sed -n '1,80p' "$LOG_FILE"
echo '===== HE CKKS CSV HEAD ====='
sed -n '1,8p' "$CSV_FILE"
