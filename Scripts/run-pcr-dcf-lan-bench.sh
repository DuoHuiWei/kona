#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
PORT_BASE_START="${PORT_BASE_START:-12340}"
SCALES=(1000 4000 10000 20000)

echo "[1/4] rebuilding benchmark source into container"
bash Scripts/codex-container-build.sh

echo "[2/4] compiling kona-pcr-dcf-bench.x in $CONTAINER"
docker exec "$CONTAINER" bash -lc "cd '$ROOT' && make -j8 kona-pcr-dcf-bench.x"

echo "[3/4] running scales: ${SCALES[*]}"
port_base="$PORT_BASE_START"
for n in "${SCALES[@]}"; do
  log_dir="$ROOT/KNN-experiment-res/pcr-dcf-lan-$n"
  echo "  - n=$n port_base=$port_base"
  docker exec "$CONTAINER" bash -lc "
    set -e
    mkdir -p '$log_dir'
    rm -f '$log_dir'/P0.log '$log_dir'/P1.log
    cd '$ROOT'
    ./kona-pcr-dcf-bench.x 0 -pn $port_base -h localhost -n 1000 -l $n -r 1 > '$log_dir'/P0.log 2>&1 &
    pid0=\$!
    ./kona-pcr-dcf-bench.x 1 -pn $port_base -h localhost -n 1000 -l $n -r 1 > '$log_dir'/P1.log 2>&1 &
    pid1=\$!
    wait \"\$pid0\"
    s0=\$?
    wait \"\$pid1\"
    s1=\$?
    test \$s0 -eq 0
    test \$s1 -eq 0
  "
  port_base=$((port_base + 10))
done

echo "[4/4] summary"
for n in "${SCALES[@]}"; do
  log_dir="$ROOT/KNN-experiment-res/pcr-dcf-lan-$n"
  echo "==== n=$n ===="
  docker exec "$CONTAINER" bash -lc "sed -n '1,40p' '$log_dir'/P0.log"
done
