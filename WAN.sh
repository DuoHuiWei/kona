#!/usr/bin/env bash
set -euo pipefail

# Kona WAN profile:
# - bandwidth: 40 Mbit/s
# - one-way delay: 20 ms on loopback
# This matches the paper-style WAN setting of about 40 ms RTT and 40 Mbps.

DEV="${1:-lo}"
BASE_RATE="40mbit"
ONE_WAY_DELAY="20ms"

sudo tc qdisc del dev "$DEV" root 2>/dev/null || true

sudo tc qdisc add dev "$DEV" root handle 1: htb default 7
for classid in 1 2 3 4 5 6 7; do
  sudo tc class add dev "$DEV" parent 1:0 classid 1:$classid htb rate "$BASE_RATE"
done

sudo tc qdisc add dev "$DEV" parent 1:1 handle 11: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:2 handle 12: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:3 handle 13: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:4 handle 14: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:5 handle 15: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:6 handle 16: netem delay "$ONE_WAY_DELAY"
sudo tc qdisc add dev "$DEV" parent 1:7 handle 17: netem delay "$ONE_WAY_DELAY"

sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip sport 10000 0xffff flowid 1:1
sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip dport 10000 0xffff flowid 1:2
sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip sport 10001 0xffff flowid 1:3
sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip dport 10001 0xffff flowid 1:4
sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip sport 10002 0xffff flowid 1:5
sudo tc filter add dev "$DEV" protocol ip parent 1:0 prio 1 u32 match ip dport 10002 0xffff flowid 1:6

echo "Applied WAN profile on $DEV: rate=$BASE_RATE one_way_delay=$ONE_WAY_DELAY"
