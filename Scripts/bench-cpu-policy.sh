#!/usr/bin/env bash
# Fixed project policy. Source this file; do not select replacement CPUs.
kona_require_fixed_bench_cpus() {
    if [[ "${1:-}" != 0 || "${2:-}" != 2 ]]; then
        echo 'ERROR: fixed benchmark CPU mapping is P0=0, P1=2; alternate CPUs are not allowed.' >&2
        return 2
    fi
}
