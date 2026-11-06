#!/usr/bin/env bash
# Wrap one party; all environment inspection is outside protocol timers.
set -uo pipefail
printf 'record_type=environment\nstarted_utc=%s\n' "$(date -u +%FT%TZ)"
printf 'source_revision=%s\nsource_dirty=%s\n' "${KONA_SOURCE_REVISION:-UNKNOWN}" "${KONA_SOURCE_DIRTY:-UNKNOWN}"
printf 'run_id=%s\n' "${KONA_RUN_ID:-UNSPECIFIED}"
printf 'command='; printf '%q ' "$@"; printf '\n'
printf 'hostname=%s\n' "$(hostname)"
printf 'cpu_affinity='; taskset -pc $$
printf 'source_sha256='; sha256sum "${KONA_BENCH_SOURCE:-Machines/kona-mainpath-esd-topk-dcf-bench.cpp}"
printf 'binary_sha256='; sha256sum "${KONA_BENCH_BINARY:-./kona-mainpath-esd-topk-dcf-bench.x}"
if [[ -n "${KONA_DATASET_PATH:-}" ]]; then
    printf 'dataset_files_sha256_begin\n'
    for file in Knn-meta P0-0-X-Train P0-0-Y-Train P1-0-X-Test P1-0-Y-Test; do
        sha256sum "$KONA_DATASET_PATH/$file" || printf 'dataset_fingerprint_status=FAILED\n'
    done
    printf 'dataset_files_sha256_end\n'
fi
printf 'network_snapshot_begin\n' 
for kind in qdisc class filter; do
    tc -s "$kind" show dev lo 2>&1 || printf 'network_query_status=FAILED\n'
done
if command -v ethtool >/dev/null; then ethtool -k lo || printf 'offload_status=QUERY_FAILED\n'; else printf 'offload_status=UNAVAILABLE\n'; fi
printf 'network_snapshot_end\ntest_status=STARTED\n'
"$@"
status=$?
printf '\nprocess_exit=%s\ntest_status=%s\nfinished_utc=%s\n' "$status" "$([[ $status == 0 ]] && echo COMPLETED || echo FAILED)" "$(date -u +%FT%TZ)"
printf 'network_after_begin\n'
tc -s qdisc show dev lo 2>&1 || true
tc -s class show dev lo 2>&1 || true
printf 'network_after_end\n'
exit "$status"
