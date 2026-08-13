#!/usr/bin/env bash
set -euo pipefail

HOST_ROOT="$(git rev-parse --show-toplevel)"
LOG_DIR="${HOST_ROOT}/KNN-experiment-res"
RUN_LOG="${LOG_DIR}/he_ckks_tcga_distance_remaining_from_13.log"
CSV_FILE="${LOG_DIR}/he_ckks_tcga_query0_distances.csv"
PID_FILE="${LOG_DIR}/he_ckks_tcga_distance_remaining_from_13.pid"

cd "$HOST_ROOT"

if [[ -f "$PID_FILE" ]]; then
    PID="$(cat "$PID_FILE")"
    echo "PID=$PID"
    if docker exec kona-he-dev bash -lc "kill -0 $PID" >/dev/null 2>&1; then
        echo "STATUS=RUNNING"
    else
        echo "STATUS=NOT_RUNNING"
    fi
else
    echo "PID file not found: $PID_FILE"
fi

echo
echo "Last log lines:"
if [[ -f "$RUN_LOG" ]]; then
    tail -n 20 "$RUN_LOG"
else
    echo "missing log: $RUN_LOG"
fi

echo
echo "Last csv lines:"
if [[ -f "$CSV_FILE" ]]; then
    tail -n 5 "$CSV_FILE"
else
    echo "missing csv: $CSV_FILE"
fi
