#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
TARGET="kona-mainpath-esd-topk-dcf-bench.x"
LOG_DIR="KNN-experiment-res/topk-only-kona-bubble-dcf"
LOG_DIR="${KONA_LOG_DIR:-${LOG_DIR}-$(date -u +%Y%m%dT%H%M%S)-$$}"
SOURCE_REVISION=$(git rev-parse HEAD)
SOURCE_DIRTY=$(git status --porcelain | wc -l)
TRIALS="${KONA_TRIALS:-3}"
TIMEOUT_SECONDS="${KONA_TIMEOUT_SECONDS:-600}"
PORT_BASE="${KONA_PORT_BASE:-13000}"
read -r -a DATASETS <<< "${KONA_DATASETS:-arcene tcga-pancan spambase dota2-games-results}"

cd "$(git rev-parse --show-toplevel)"
bash Scripts/codex-container-build.sh
docker exec "$CONTAINER" bash -lc "cd '$ROOT' && make -j8 '$TARGET'"
docker exec "$CONTAINER" bash -lc "
    set -e
    mkdir -p '$ROOT/Player-Data/Knn-Data/knn-1'
    for dataset in ${DATASETS[*]}; do
        case \"\$dataset\" in spambase|dota2-games-results) ;; *) continue ;; esac
        mkdir -p '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data'
        cp -a '$ROOT/ConstantPP/data/'\"\$dataset\"'-data/.' \
            '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data/'
    done
"

for index in "${!DATASETS[@]}"; do
    dataset="${DATASETS[$index]}"
    port=$((PORT_BASE + index * 100))
    pair_status=0
    docker exec "$CONTAINER" bash -lc "
        set -e
        cd '$ROOT'
        mkdir -p '$LOG_DIR'
        KONA_SOURCE_REVISION='$SOURCE_REVISION' KONA_SOURCE_DIRTY='$SOURCE_DIRTY' taskset -c 0 bash Scripts/kona-bench-log.sh timeout --kill-after=5 '$TIMEOUT_SECONDS' ./'$TARGET' -p 0 -pn '$port' -h localhost \
            -d '$dataset' -k 5 -q 1 -t '$TRIALS' \
            > '$LOG_DIR/${dataset}-P0.log' 2>&1 &
        p0=\$!
        KONA_SOURCE_REVISION='$SOURCE_REVISION' KONA_SOURCE_DIRTY='$SOURCE_DIRTY' taskset -c 2 bash Scripts/kona-bench-log.sh timeout --kill-after=5 '$TIMEOUT_SECONDS' ./'$TARGET' -p 1 -pn '$port' -h localhost \
            -d '$dataset' -k 5 -q 1 -t '$TRIALS' \
            > '$LOG_DIR/${dataset}-P1.log' 2>&1 &
        p1=\$!
        set +e
        wait \"\$p0\"; s0=\$?
        wait \"\$p1\"; s1=\$?
        echo '$dataset P0_EXIT='\"\$s0\"' P1_EXIT='\"\$s1\"
        exit \$((s0 || s1))
    " || pair_status=$?
    mkdir -p "$LOG_DIR"
    docker cp "$CONTAINER:$ROOT/$LOG_DIR/${dataset}-P0.log" \
        "$LOG_DIR/${dataset}-P0.log"
    docker cp "$CONTAINER:$ROOT/$LOG_DIR/${dataset}-P1.log" \
        "$LOG_DIR/${dataset}-P1.log"
    tail -16 "$LOG_DIR/${dataset}-P0.log"
    if (( pair_status != 0 )); then exit "$pair_status"; fi
done

exit "${pair_status:-0}"
