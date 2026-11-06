#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
TARGET="kona-topk-network-bench.x"
LOG_DIR="KNN-experiment-res/topk-only-cong-pcr"
PORT_BASE="${KONA_PORT_BASE:-15000}"
DATASETS=(arcene tcga-pancan spambase dota2-games-results)

cd "$(git rev-parse --show-toplevel)"
bash Scripts/codex-container-build.sh
docker exec "$CONTAINER" bash -lc "cd '$ROOT' && make -j8 '$TARGET'"
docker exec "$CONTAINER" bash -lc "
    set -e
    mkdir -p '$ROOT/Player-Data/Knn-Data/knn-1'
    for dataset in spambase dota2-games-results; do
        mkdir -p '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data'
        cp -a '$ROOT/ConstantPP/data/'\"\$dataset\"'-data/.' \
            '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data/'
    done
"

for index in "${!DATASETS[@]}"; do
    dataset="${DATASETS[$index]}"
    port=$((PORT_BASE + index * 100))
    docker exec "$CONTAINER" bash -lc "
        set -e
        cd '$ROOT'
        mkdir -p '$LOG_DIR'
        taskset -c 0 ./'$TARGET' -p 0 -pn '$port' -h localhost \
            --mode cong --dataset '$dataset' --topk 5 --trials 3 \
            > '$LOG_DIR/${dataset}-P0.log' 2>&1 &
        p0=\$!
        taskset -c 2 ./'$TARGET' -p 1 -pn '$port' -h localhost \
            --mode cong --dataset '$dataset' --topk 5 --trials 3 \
            > '$LOG_DIR/${dataset}-P1.log' 2>&1 &
        p1=\$!
        wait \"\$p0\"; s0=\$?
        wait \"\$p1\"; s1=\$?
        echo '$dataset P0_EXIT='\"\$s0\"' P1_EXIT='\"\$s1\"
        exit \$((s0 || s1))
    "
done

mkdir -p "$LOG_DIR"
for dataset in "${DATASETS[@]}"; do
    docker cp "$CONTAINER:$ROOT/$LOG_DIR/${dataset}-P0.log" \
        "$LOG_DIR/${dataset}-P0.log"
    docker cp "$CONTAINER:$ROOT/$LOG_DIR/${dataset}-P1.log" \
        "$LOG_DIR/${dataset}-P1.log"
    tail -18 "$LOG_DIR/${dataset}-P0.log"
done
