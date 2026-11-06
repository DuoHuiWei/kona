#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
ROOT="/usr/src/Garnet"
PROFILE="${1:-lan}"
REPEATS="${REPEATS:-5}"
INNER_REPEATS="${INNER_REPEATS:-3}"
COMPARATORS="${COMPARATORS:-1024 2048 4096 8192 16384}"
CPU0="${CPU0:-0}"
CPU1="${CPU1:-2}"
source "$(dirname "${BASH_SOURCE[0]}")/bench-cpu-policy.sh"
kona_require_fixed_bench_cpus "$CPU0" "$CPU1"
PORT_BASE="${PORT_BASE:-41000}"
SEED="${SEED:-20260902}"

case "$PROFILE" in
  lan) bash Scripts/container-network.sh lan lo ;;
  wan) bash Scripts/container-network.sh wan lo ;;
  *) echo "profile must be lan or wan" >&2; exit 2 ;;
esac

docker exec "$CONTAINER" bash -lc '
  c0=$(cat /sys/devices/system/cpu/cpu'"$CPU0"'/topology/core_id)
  p0=$(cat /sys/devices/system/cpu/cpu'"$CPU0"'/topology/physical_package_id)
  c1=$(cat /sys/devices/system/cpu/cpu'"$CPU1"'/topology/core_id)
  p1=$(cat /sys/devices/system/cpu/cpu'"$CPU1"'/topology/physical_package_id)
  echo "CPU_CHECK cpu0='"$CPU0"' package=$p0 core=$c0 cpu1='"$CPU1"' package=$p1 core=$c1"
  test "$c0:$p0" != "$c1:$p1"
'

bash Scripts/codex-container-build.sh
docker exec "$CONTAINER" bash -lc "cd '$ROOT' && make kona-pcr-dcf-compare-swap-bench.x"

RUN_ID="pcr-dcf-compare-swap-${PROFILE}-$(date +%Y%m%d-%H%M%S)"
OUT_DIR="$ROOT/KNN-experiment-res/$RUN_ID"
docker exec "$CONTAINER" bash -lc "mkdir -p '$OUT_DIR'"
docker exec "$CONTAINER" bash -lc "printf '%s\\n' 'profile=$PROFILE cpu0=$CPU0 cpu1=$CPU1 repeats=$REPEATS inner_repeats=$INNER_REPEATS comparators=$COMPARATORS seed=$SEED' > '$OUT_DIR/metadata.txt'"

run_one() {
  local mode="$1" m="$2" trial="$3" order="$4" port="$5"
  docker exec "$CONTAINER" bash -lc "
    set -e
    taskset -c '$CPU0' '$ROOT/kona-pcr-dcf-compare-swap-bench.x' 0 -pn '$port' -h localhost \\
      --mode '$mode' --comparators '$m' --repeats 1 --inner-repeats '$INNER_REPEATS' --seed '$SEED' \\
      > '$OUT_DIR/${m}_${trial}_${order}_${mode}_P0.log' 2>&1 &
    p0=\$!
    taskset -c '$CPU1' '$ROOT/kona-pcr-dcf-compare-swap-bench.x' 1 -pn '$port' -h localhost \\
      --mode '$mode' --comparators '$m' --repeats 1 --inner-repeats '$INNER_REPEATS' --seed '$SEED' \\
      > '$OUT_DIR/${m}_${trial}_${order}_${mode}_P1.log' 2>&1 &
    p1=\$!
    s0=0; s1=0
    wait \$p0 || s0=\$?
    wait \$p1 || s1=\$?
    test \$s0 -eq 0
    test \$s1 -eq 0
  "
}

for m in $COMPARATORS; do
  for trial in $(seq 1 "$REPEATS"); do
    if (( trial % 2 == 1 )); then
      first=pcr; second=dcf
    else
      first=dcf; second=pcr
    fi
    run_one "$first" "$m" "$trial" first "$PORT_BASE"
    PORT_BASE=$((PORT_BASE + 1))
    run_one "$second" "$m" "$trial" second "$PORT_BASE"
    PORT_BASE=$((PORT_BASE + 1))
    echo "completed profile=$PROFILE m=$m trial=$trial first=$first second=$second"
  done
done

LOCAL_OUT_DIR="KNN-experiment-res/$RUN_ID"
mkdir -p "$LOCAL_OUT_DIR"
docker cp "$CONTAINER:$OUT_DIR/." "$LOCAL_OUT_DIR/"
echo "RESULT_DIR=$LOCAL_OUT_DIR"
