#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
TARGET="kona-topk-network-bench.x"
LOG_DIR="${KONA_LOG_DIR:-KNN-experiment-res/topk-only-cong-hybrid}"
PORT_BASE="${KONA_PORT_BASE:-17000}"
DATASETS=(arcene tcga-pancan spambase dota2-games-results)
read -r -a PROFILES <<< "${KONA_PROFILES:-lan wan}"

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
    for dataset in spambase dota2-games-results; do
        mkdir -p '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data'
        cp -a '$ROOT/ConstantPP/data/'\"\$dataset\"'-data/.' \
            '$ROOT/Player-Data/Knn-Data/knn-1/'\"\$dataset\"'-data/'
    done
"

mkdir -p "$LOG_DIR"
profile_index=0
for profile in "${PROFILES[@]}"; do
    if [[ "$profile" == "lan" ]]; then
        threshold=190
        bash Scripts/container-network.sh lan lo
    else
        threshold=2808
        bash Scripts/container-network.sh wan lo
    fi

    for index in "${!DATASETS[@]}"; do
        dataset="${DATASETS[$index]}"
        port=$((PORT_BASE + profile_index * 1000 + index * 100))
        docker exec "$CONTAINER" bash -lc "
            set -e
            cd '$ROOT'
            mkdir -p '$LOG_DIR/$profile'
            taskset -c 0 ./'$TARGET' -p 0 -pn '$port' -h localhost \
                --mode hybrid --dataset '$dataset' --topk 5 --trials 3 \
                --network-profile '$profile' --hybrid-threshold '$threshold' \
                > '$LOG_DIR/$profile/${dataset}-P0.log' 2>&1 &
            p0=\$!
            taskset -c 2 ./'$TARGET' -p 1 -pn '$port' -h localhost \
                --mode hybrid --dataset '$dataset' --topk 5 --trials 3 \
                --network-profile '$profile' --hybrid-threshold '$threshold' \
                > '$LOG_DIR/$profile/${dataset}-P1.log' 2>&1 &
            p1=\$!
            wait \"\$p0\"; s0=\$?
            wait \"\$p1\"; s1=\$?
            echo '$profile $dataset P0_EXIT='\"\$s0\"' P1_EXIT='\"\$s1\"
            exit \$((s0 || s1))
        "
        mkdir -p "$LOG_DIR/$profile"
        docker cp "$CONTAINER:$ROOT/$LOG_DIR/$profile/${dataset}-P0.log" \
            "$LOG_DIR/$profile/${dataset}-P0.log"
        docker cp "$CONTAINER:$ROOT/$LOG_DIR/$profile/${dataset}-P1.log" \
            "$LOG_DIR/$profile/${dataset}-P1.log"
    done
    profile_index=$((profile_index + 1))
done

cleanup
trap - EXIT

PARTY_SUMMARY="$LOG_DIR/party_online_summary.csv"
MAX_SUMMARY="$LOG_DIR/max_party_online_summary.csv"
printf 'profile,dataset,party,trial,online_ms\n' > "$PARTY_SUMMARY"
for profile in "${PROFILES[@]}"; do
    for dataset in "${DATASETS[@]}"; do
        for party in 0 1; do
            log="$LOG_DIR/$profile/${dataset}-P${party}.log"
            awk -v profile="$profile" -v dataset="$dataset" \
                -v party="$party" '
                /^trial=/ { trial = substr($0, 7) }
                /^topk_ms=/ { ms = substr($0, 9); \
                    print profile "," dataset "," party "," trial "," ms }
            ' "$log" >> "$PARTY_SUMMARY"
        done
    done
done

awk -F, 'BEGIN { OFS="," }
    NR == 1 { next }
    { key=$1 SUBSEP $2 SUBSEP $4; value[key,$3]=$5; seen[key]=1 }
    END {
        print "profile,dataset,trial,p0_online_ms,p1_online_ms,max_online_ms"
        for (key in seen) {
            split(key, fields, SUBSEP)
            p0=value[key,0]; p1=value[key,1]
            max=(p0 > p1 ? p0 : p1)
            print fields[1],fields[2],fields[3],p0,p1,max
        }
    }' "$PARTY_SUMMARY" > "$MAX_SUMMARY"

for profile in "${PROFILES[@]}"; do
    for dataset in "${DATASETS[@]}"; do
        echo "=== $profile $dataset ==="
        tail -24 "$LOG_DIR/$profile/${dataset}-P0.log"
    done
done
echo "PARTY_SUMMARY=$PARTY_SUMMARY"
echo "MAX_SUMMARY=$MAX_SUMMARY"
