#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
CONTAINER_ROOT="/usr/src/Garnet"
LOG_DIR="KNN-experiment-res/distance-only"
PORT_BASE="${KONA_PORT_BASE:-12000}"
TARGET="kona-distance-only-bench.x"
DATASETS=(arcene tcga-pancan spambase dota2-games-results)

cd "$(git rev-parse --show-toplevel)"
bash Scripts/codex-container-build.sh
docker exec "$CONTAINER" bash -lc "cd '$CONTAINER_ROOT' && make -j8 '$TARGET'"

# Player-Data is intentionally excluded by codex-container-build.sh. Import the
# benchmark inputs from the WSL-sourced ConstantPP copy on every run so the
# container never becomes the source of truth for datasets.
docker exec "$CONTAINER" bash -lc "
    set -e
    mkdir -p '$CONTAINER_ROOT/Player-Data/Knn-Data/knn-1'
    for dataset in spambase dota2-games-results; do
        mkdir -p '$CONTAINER_ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data'
        cp -a '$CONTAINER_ROOT/ConstantPP/data/'\"\$dataset\"'-data/.' \
            '$CONTAINER_ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data/'
    done
"

for index in "${!DATASETS[@]}"; do
    dataset="${DATASETS[$index]}"
    port=$((PORT_BASE + index * 100))
    docker exec "$CONTAINER" bash -lc "
        set -e
        cd '$CONTAINER_ROOT'
        mkdir -p '$LOG_DIR'
        taskset -c 0 ./'$TARGET' -p 0 -pn '$port' -h localhost \
            -d '$dataset' -t 3 --network-env localhost-no-delay \
            > '$LOG_DIR/${dataset}-P0.log' 2>&1 &
        p0=\$!
        taskset -c 2 ./'$TARGET' -p 1 -pn '$port' -h localhost \
            -d '$dataset' -t 3 --network-env localhost-no-delay \
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
    docker cp "$CONTAINER:$CONTAINER_ROOT/$LOG_DIR/${dataset}-P0.log" \
        "$LOG_DIR/${dataset}-P0.log"
    docker cp "$CONTAINER:$CONTAINER_ROOT/$LOG_DIR/${dataset}-P1.log" \
        "$LOG_DIR/${dataset}-P1.log"
    sed -n '1,240p' "$LOG_DIR/${dataset}-P0.log"
done
