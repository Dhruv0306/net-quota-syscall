#!/usr/bin/env bash
# Creates a second network namespace joined to the host by a veth pair, so a
# server in it can be reached over a real (non-loopback) address.
# Loopback traffic is deliberately not counted by net_quota.
#
#   setup_netns.sh up      host 10.200.0.1 <-> namespace "nqsrv" 10.200.0.2
#   setup_netns.sh down
# Needs root.
set -euo pipefail

NS=nqsrv
HOST_IF=veth-nqh
NS_IF=veth-nqs
HOST_IP=10.200.0.1
NS_IP=10.200.0.2

case "${1:-}" in
up)
	ip netns add "$NS"
	ip link add "$HOST_IF" type veth peer name "$NS_IF"
	ip link set "$NS_IF" netns "$NS"
	ip addr add "$HOST_IP/24" dev "$HOST_IF"
	ip link set "$HOST_IF" up
	ip netns exec "$NS" ip addr add "$NS_IP/24" dev "$NS_IF"
	ip netns exec "$NS" ip link set "$NS_IF" up
	ip netns exec "$NS" ip link set lo up
	;;
down)
	ip netns del "$NS" 2>/dev/null || true
	ip link del "$HOST_IF" 2>/dev/null || true
	;;
*)
	echo "usage: $0 up|down" >&2
	exit 2
	;;
esac
