#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${KONA_CONTAINER:-kona-dev}"
VERSION="20261108-serialized-v4"
PACING=serialized
PROFILE=""
DEV=lo
ACTION=apply
DRY_RUN=0
DOCKER_EXEC=(docker exec -i)
# Explicit opt-in for an existing container without NET_ADMIN in its normal
# exec processes. This does not recreate or change the container definition.
case "${KONA_NETWORK_PRIVILEGED:-0}" in
    0) ;;
    1) DOCKER_EXEC+=(--privileged) ;;
    *) echo 'KONA_NETWORK_PRIVILEGED must be 0 or 1' >&2; exit 2 ;;
esac

usage() {
    cat <<'EOF'
Usage: bash Scripts/container-network.sh PROFILE [DEV] [--dry-run]
       bash Scripts/container-network.sh PROFILE [DEV] --pacing burst
       bash Scripts/container-network.sh --profile PROFILE --dev DEV [--dry-run]
       bash Scripts/container-network.sh --list
       bash Scripts/container-network.sh --show [--dev DEV]
       bash Scripts/container-network.sh --clear [--dev DEV] [--dry-run]

Profiles (rate/delay per direction):
  lan       1 Gbps    / 0.5 ms
  wan       40 Mbps   / 20 ms
  mid-lan   500 Mbps  / 2 ms
  mid-wan   100 Mbps  / 8 ms
  super-dc  10 Gbps   / 0.05 ms

All profiles share the same HTB topology, burst/quantum, netem limit,
offload settings and IPv4 destination-port parity classification.
Default serialized pacing adds netem rate=PROFILE_RATE on EACH direction:
packet serialization cannot spend the HTB bucket's accumulated idle credit.
--pacing burst retains the historical HTB-only rate / delay-only netem mode.
This changes the network model, not comparator algorithms or Hybrid thresholds.
--dry-run prints the exact command and remote script without invoking Docker.
--clear removes the root qdisc; it does not restore prior offload settings.
If exec processes lack NET_ADMIN, explicitly set KONA_NETWORK_PRIVILEGED=1.
EOF
}

positional=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --profile) PROFILE="${2:?missing profile}"; shift 2 ;;
        --dev) DEV="${2:?missing device}"; shift 2 ;;
        --pacing) PACING="${2:?missing pacing}"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        --show) ACTION=show; shift ;;
        --clear) ACTION=clear; shift ;;
        --list|-h|--help) usage; exit 0 ;;
        --*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [[ $positional -eq 0 ]]; then PROFILE="$1";
            elif [[ $positional -eq 1 ]]; then DEV="$1";
            else echo 'Too many positional arguments' >&2; exit 2; fi
            positional=$((positional + 1)); shift ;;
    esac
done

case "$PACING" in
    serialized) ;;
    burst) VERSION="20261106-lan05-super005-v3" ;;
    *) echo 'Pacing must be serialized or burst' >&2; exit 2 ;;
esac

if [[ ! "$DEV" =~ ^[a-zA-Z0-9_.:-]+$ ]]; then
    echo 'Invalid interface name' >&2; exit 2
fi

PARENT_RATE=""
RATE=""
DELAY=""
if [[ "$ACTION" == apply ]]; then
    case "$PROFILE" in
        lan) PARENT_RATE=2gbit; RATE=1gbit; DELAY=0.5ms ;;
        wan) PARENT_RATE=80mbit; RATE=40mbit; DELAY=20ms ;;
        mid-lan) PARENT_RATE=1gbit; RATE=500mbit; DELAY=2ms ;;
        mid-wan) PARENT_RATE=200mbit; RATE=100mbit; DELAY=8ms ;;
        super-dc) PARENT_RATE=20gbit; RATE=10gbit; DELAY=0.05ms ;;
        *) echo "Unknown/missing profile: $PROFILE" >&2; usage >&2; exit 2 ;;
    esac
fi

# Pass values as positional arguments, never interpolate them into shell code.
# All non-rate/non-delay settings are defined ONCE here for every profile.
PROGRAM=$(cat <<'REMOTE'
set -euo pipefail
action=$1
dev=$2
parent_rate=$3
rate=$4
delay=$5
pacing=$6

show_config() {
    tc -s qdisc show dev "$dev"
    tc -s class show dev "$dev"
    tc -s filter show dev "$dev"
}
if [[ "$action" == show ]]; then show_config; exit 0; fi
if [[ "$action" == clear ]]; then
    tc qdisc del dev "$dev" root 2>/dev/null || true
    show_config
    exit 0
fi

if command -v ethtool >/dev/null 2>&1; then
    ethtool -K "$dev" gro off gso off tso off 2>/dev/null || true
fi
tc qdisc del dev "$dev" root 2>/dev/null || true
tc qdisc add dev "$dev" root handle 1: htb r2q 10000 default 30
# Fix quantum explicitly as well as r2q so HTB scheduling parameters do not
# silently scale with rate. These common values define a NEW profile version.
for spec in "1: 1:1 $parent_rate" "1:1 1:10 $rate" \
            "1:1 1:20 $rate" "1: 1:30 $parent_rate"; do
    read -r parent classid class_rate <<< "$spec"
    tc class add dev "$dev" parent "$parent" classid "$classid" htb \
        rate "$class_rate" ceil "$class_rate" burst 1mb cburst 1mb quantum 65536
done
# Install the same leaf qdiscs for every profile; delay=0ms injects no extra
# delay while preserving the topology used by the delayed profiles.
netem_rate=()
if [[ "$pacing" == serialized ]]; then netem_rate=(rate "$rate"); fi
tc qdisc add dev "$dev" parent 1:10 handle 10: netem limit 1000 delay "$delay" "${netem_rate[@]}"
tc qdisc add dev "$dev" parent 1:20 handle 20: netem limit 1000 delay "$delay" "${netem_rate[@]}"
tc filter add dev "$dev" protocol ip parent 1:0 prio 10 u32 \
    match ip dport 0 0x1 flowid 1:10
tc filter add dev "$dev" protocol ip parent 1:0 prio 11 u32 \
    match ip dport 1 0x1 flowid 1:20
show_config
REMOTE
)

echo "NETWORK_PROFILE version=$VERSION action=$ACTION profile=${PROFILE:-none} container=$CONTAINER dev=$DEV rate_per_direction=${RATE:-n/a} one_way_delay=${DELAY:-n/a} parent_rate=${PARENT_RATE:-n/a} pacing=$PACING"
if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%q ' "${DOCKER_EXEC[@]}" "$CONTAINER" bash -s -- \
        "$ACTION" "$DEV" "$PARENT_RATE" "$RATE" "$DELAY" "$PACING"
    printf '\n'
    printf '%s\n' "$PROGRAM"
else
    printf '%s\n' "$PROGRAM" | "${DOCKER_EXEC[@]}" "$CONTAINER" bash -s -- \
        "$ACTION" "$DEV" "$PARENT_RATE" "$RATE" "$DELAY" "$PACING"
fi
