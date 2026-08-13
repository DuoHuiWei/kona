#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_HE_CONTAINER:-kona-he-dev}"
HOST_ROOT="$(git rev-parse --show-toplevel)"
CONTAINER_ROOT="${KONA_HE_CONTAINER_ROOT:-/usr/src/Garnet}"
BUILD_DIR="${CONTAINER_ROOT}/build/hecompare"
LOG_DIR="${HOST_ROOT}/KNN-experiment-res"
RUN_LOG="${LOG_DIR}/he_ckks_tcga_distance_remaining_from_13.log"
CSV_FILE="${LOG_DIR}/he_ckks_tcga_query0_distances.csv"
PID_FILE="${LOG_DIR}/he_ckks_tcga_distance_remaining_from_13.pid"
STATUS_FILE="${LOG_DIR}/he_ckks_tcga_distance_remaining_from_13.status"
START_TRAIN_INDEX="${HE_CKKS_DETACHED_START_TRAIN_INDEX:-13}"

cd "$HOST_ROOT"
mkdir -p "$LOG_DIR"

echo "[1/4] Check container"
docker start "$CONTAINER" >/dev/null 2>&1 || true

echo "[2/4] Sync source into HE container"
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

echo "[3/4] Configure and build HECompare benchmark"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    rm -rf build/hecompare
    bash Scripts/build-he.sh
"

echo "[4/4] Start detached remaining-distance benchmark"
rm -f "$RUN_LOG" "$PID_FILE" "$STATUS_FILE"
printf 'STARTED_AT=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" > "$STATUS_FILE"
printf 'MODE=remaining_from_train_idx_%s_to_end\n' "$START_TRAIN_INDEX" >> "$STATUS_FILE"
printf 'OUTPUT_CSV=%s\n' "$CSV_FILE" >> "$STATUS_FILE"
printf 'START_TRAIN_INDEX=%s\n' "$START_TRAIN_INDEX" >> "$STATUS_FILE"
docker exec "$CONTAINER" bash -lc "
    set -e
    cd '$CONTAINER_ROOT'
    nohup '$BUILD_DIR/openfhe-tcga-ckks-distance-benchmark' \
        '/workspace/Kona/HECompare/testdata/tcga-pancan-raw/data.csv' \
        '/workspace/Kona/HECompare/testdata/tcga-pancan-raw/labels.csv' \
        '/workspace/Kona/KNN-experiment-res/he_ckks_tcga_query0_distances.csv' \
        '$START_TRAIN_INDEX' \
        0 \
        1 \
        > '/workspace/Kona/KNN-experiment-res/he_ckks_tcga_distance_remaining_from_13.log' 2>&1 < /dev/null &
    echo \$! > '/workspace/Kona/KNN-experiment-res/he_ckks_tcga_distance_remaining_from_13.pid'
"

echo "Detached benchmark started."
echo "PID file: $PID_FILE"
echo "Log file: $RUN_LOG"
echo "CSV file: $CSV_FILE"
echo "Status file: $STATUS_FILE"
echo
echo "Watch progress with:"
echo "  tail -f '$RUN_LOG'"
echo "  tail -n 5 '$CSV_FILE'"
