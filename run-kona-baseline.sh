#!/usr/bin/env bash
set -euo pipefail

export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1
export NUMEXPR_NUM_THREADS=1

BIN="${KONA_BIN:-./kona-upstream.x}"
PORT_BASE="${KONA_PORT_BASE:-10000}"
HOSTNAME_ARG="${KONA_HOSTNAME:-localhost}"
OUT_DIR="${KONA_OUT_DIR:-KNN-experiment-res}"
mkdir -p "$OUT_DIR"

P0_LOG="$OUT_DIR/kona_upstream_arcene_P0.log"
P1_LOG="$OUT_DIR/kona_upstream_arcene_P1.log"
rm -f "$P0_LOG" "$P1_LOG"

if [[ ! -x "$BIN" ]]; then
  echo "Missing executable: $BIN" >&2
  exit 1
fi

launch_party() {
  local label="$1"
  local core="$2"
  local party="$3"
  local log="$4"

  taskset -c "$core" bash -c '
    label="$1"
    shift
    echo "$label PID: $$"
    echo "$label CPU affinity: $(taskset -pc $$)"
    exec "$@"
  ' bash "$label" "$BIN" "$party" -pn "$PORT_BASE" -h "$HOSTNAME_ARG" > "$log" 2>&1 &
}

print_launch_proof() {
  local label="$1"
  local log="$2"
  for _ in {1..100}; do
    if grep -q "^$label CPU affinity:" "$log" 2>/dev/null; then
      sed -n '1,2p' "$log"
      return 0
    fi
    sleep 0.05
  done
  echo "Missing affinity proof for $label in $log" >&2
  return 1
}

launch_party P0 0 0 "$P0_LOG"
pid0=$!
launch_party P1 2 1 "$P1_LOG"
pid1=$!

print_launch_proof P0 "$P0_LOG"
print_launch_proof P1 "$P1_LOG"

echo "P0 log: $P0_LOG"
echo "P1 log: $P1_LOG"

set +e
wait "$pid0"
s0=$?
wait "$pid1"
s1=$?
set -e

echo "P0_EXIT=$s0 P1_EXIT=$s1"
exit $((s0 || s1))
