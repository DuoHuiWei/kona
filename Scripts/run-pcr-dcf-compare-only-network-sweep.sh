#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
PROFILE="${1:-super-dc}"
TRIALS="${TRIALS:-10}"
INNER_REPEATS="${INNER_REPEATS:-3}"
CPU0="${CPU0:-0}"
CPU1="${CPU1:-2}"
source "$(dirname "${BASH_SOURCE[0]}")/bench-cpu-policy.sh"
kona_require_fixed_bench_cpus "$CPU0" "$CPU1"
PORT_BASE="${PORT_BASE:-57000}"

case "$PROFILE" in
  super-dc)
    bash Scripts/container-network.sh super-dc lo
    SCALES="${SCALES:-4 8 12 16 20 30 40 50 60 70 80 90 100}"
    ;;
  mid-lan)
    bash Scripts/container-network.sh mid-lan lo
    SCALES="${SCALES:-500 550 600 650 700 750 800 850 900 950 1000}"
    ;;
  mid-wan)
    bash Scripts/container-network.sh mid-wan lo
    SCALES="${SCALES:-2000 2500 3000 3500 4000 4500 5000}"
    ;;
  *) echo "profile must be super-dc, mid-lan, or mid-wan" >&2; exit 2 ;;
esac

docker exec "$CONTAINER" bash -lc "
  c0=\$(cat /sys/devices/system/cpu/cpu$CPU0/topology/core_id)
  p0=\$(cat /sys/devices/system/cpu/cpu$CPU0/topology/physical_package_id)
  c1=\$(cat /sys/devices/system/cpu/cpu$CPU1/topology/core_id)
  p1=\$(cat /sys/devices/system/cpu/cpu$CPU1/topology/physical_package_id)
  echo CPU_CHECK cpu0=$CPU0 package=\$p0 core=\$c0 cpu1=$CPU1 package=\$p1 core=\$c1
  test \"\$c0:\$p0\" != \"\$c1:\$p1\"
"

bash Scripts/codex-container-build.sh
docker exec "$CONTAINER" bash -lc "cd '$ROOT' && make kona-pcr-dcf-bench.x"

RUN_ID="pcr-dcf-compare-only-${PROFILE}-$(date +%Y%m%d-%H%M%S)"
CONTAINER_OUT="$ROOT/KNN-experiment-res/$RUN_ID"
LOCAL_OUT="KNN-experiment-res/$RUN_ID"
  docker exec "$CONTAINER" bash -lc "mkdir -p '$CONTAINER_OUT'"

for n in $SCALES; do
  for trial in $(seq 1 "$TRIALS"); do
    case "$PROFILE" in
      super-dc) bash Scripts/container-network.sh super-dc lo ;;
      mid-lan) bash Scripts/container-network.sh mid-lan lo ;;
      mid-wan) bash Scripts/container-network.sh mid-wan lo ;;
    esac
    docker exec "$CONTAINER" bash -lc "tc -s qdisc show dev lo > '$CONTAINER_OUT/qdisc_before_${n}_trial${trial}.txt' && tc -s class show dev lo > '$CONTAINER_OUT/class_before_${n}_trial${trial}.txt'"
    docker exec "$CONTAINER" bash -lc "
      set -e
      taskset -c '$CPU0' '$ROOT/kona-pcr-dcf-bench.x' 0 -pn '$PORT_BASE' -h localhost -n 1000 -l '$n' -r '$INNER_REPEATS' \\
        > '$CONTAINER_OUT/${n}_trial${trial}_P0.log' 2>&1 &
      p0=\$!
      taskset -c '$CPU1' '$ROOT/kona-pcr-dcf-bench.x' 1 -pn '$PORT_BASE' -h localhost -n 1000 -l '$n' -r '$INNER_REPEATS' \\
        > '$CONTAINER_OUT/${n}_trial${trial}_P1.log' 2>&1 &
      p1=\$!
      s0=0; s1=0
      wait \$p0 || s0=\$?
      wait \$p1 || s1=\$?
      test \$s0 -eq 0
      test \$s1 -eq 0
    "
    docker exec "$CONTAINER" bash -lc "tc -s qdisc show dev lo > '$CONTAINER_OUT/qdisc_after_${n}_trial${trial}.txt' && tc -s class show dev lo > '$CONTAINER_OUT/class_after_${n}_trial${trial}.txt'"
    echo "completed profile=$PROFILE m=$n trial=$trial"
    PORT_BASE=$((PORT_BASE + 10))
  done
done

docker exec "$CONTAINER" bash -lc "tc -s qdisc show dev lo > '$CONTAINER_OUT/qdisc_after.txt' && tc -s class show dev lo > '$CONTAINER_OUT/class_after.txt'"
mkdir -p "$LOCAL_OUT"
docker cp "$CONTAINER:$CONTAINER_OUT/." "$LOCAL_OUT/"
echo "RESULT_DIR=$LOCAL_OUT"
