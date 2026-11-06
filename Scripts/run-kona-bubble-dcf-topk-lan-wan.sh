#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
TARGET="kona-mainpath-esd-topk-dcf-bench.x"
LOG_DIR="KNN-experiment-res/topk-only-kona-bubble-dcf-lan-wan"
LOG_DIR="${KONA_LOG_DIR:-${LOG_DIR}-$(date -u +%Y%m%dT%H%M%S)-$$}"
SOURCE_REVISION=$(git rev-parse HEAD)
SOURCE_DIRTY=$(git status --porcelain | wc -l)
TRIALS="${KONA_TRIALS:-3}"
TIMEOUT_SECONDS="${KONA_TIMEOUT_SECONDS:-600}"
PORT_BASE="${KONA_PORT_BASE:-20000}"
read -r -a DATASETS <<< "${KONA_DATASETS:-arcene tcga-pancan spambase dota2-games-results}"
PROFILES=(lan wan)

cd "$(git rev-parse --show-toplevel)"
cleanup() {
    bash Scripts/container-network.sh --clear --dev lo || true
}
trap cleanup EXIT

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

mkdir -p "$LOG_DIR"
profile_index=0
for profile in "${PROFILES[@]}"; do
    if [[ "$profile" == "lan" ]]; then
        bash Scripts/container-network.sh lan lo
    else
        bash Scripts/container-network.sh wan lo
    fi
    for index in "${!DATASETS[@]}"; do
        dataset="${DATASETS[$index]}"
        port=$((PORT_BASE + profile_index * 1000 + index * 100))
        pair_status=0
        docker exec "$CONTAINER" bash -lc "
            set -e
            cd '$ROOT'
            mkdir -p '$LOG_DIR/$profile'
            KONA_SOURCE_REVISION='$SOURCE_REVISION' KONA_SOURCE_DIRTY='$SOURCE_DIRTY' taskset -c 0 bash Scripts/kona-bench-log.sh timeout --kill-after=5 '$TIMEOUT_SECONDS' ./'$TARGET' -p 0 -pn '$port' -h localhost \
                -d '$dataset' -k 5 -q 1 -t '$TRIALS' --network-profile '$profile' \
                > '$LOG_DIR/$profile/${dataset}-P0.log' 2>&1 &
            p0=\$!
            KONA_SOURCE_REVISION='$SOURCE_REVISION' KONA_SOURCE_DIRTY='$SOURCE_DIRTY' taskset -c 2 bash Scripts/kona-bench-log.sh timeout --kill-after=5 '$TIMEOUT_SECONDS' ./'$TARGET' -p 1 -pn '$port' -h localhost \
                -d '$dataset' -k 5 -q 1 -t '$TRIALS' --network-profile '$profile' \
                > '$LOG_DIR/$profile/${dataset}-P1.log' 2>&1 &
            p1=\$!
            set +e
            wait \"\$p0\"; s0=\$?
            wait \"\$p1\"; s1=\$?
            echo '$profile $dataset P0_EXIT='\"\$s0\"' P1_EXIT='\"\$s1\"
            exit \$((s0 || s1))
        " || pair_status=$?
        mkdir -p "$LOG_DIR/$profile"
        docker cp "$CONTAINER:$ROOT/$LOG_DIR/$profile/${dataset}-P0.log" \
            "$LOG_DIR/$profile/${dataset}-P0.log"
        docker cp "$CONTAINER:$ROOT/$LOG_DIR/$profile/${dataset}-P1.log" \
            "$LOG_DIR/$profile/${dataset}-P1.log"
        if (( ${pair_status:-0} != 0 )); then exit "$pair_status"; fi
    done
    profile_index=$((profile_index + 1))
done

cleanup
trap - EXIT
for profile in "${PROFILES[@]}"; do
    for dataset in "${DATASETS[@]}"; do
        echo "=== $profile $dataset ==="
        tail -16 "$LOG_DIR/$profile/${dataset}-P0.log"
    done
done
