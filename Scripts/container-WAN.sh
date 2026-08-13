#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
DEV="${1:-lo}"
BASE_RATE="40mbit"
ONE_WAY_DELAY="20ms"

docker exec "$CONTAINER" bash -lc "
set -euo pipefail
tc qdisc del dev '$DEV' root 2>/dev/null || true
tc qdisc add dev '$DEV' root handle 1: tbf rate '$BASE_RATE' burst 256kb latency 500ms
tc qdisc add dev '$DEV' parent 1:1 handle 10: netem delay '$ONE_WAY_DELAY'
"

echo "Applied container WAN profile on $CONTAINER:$DEV for all traffic rate=$BASE_RATE one_way_delay=$ONE_WAY_DELAY"
