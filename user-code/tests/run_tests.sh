#!/usr/bin/env bash
# Runs all net_quota tests. Needs root and the binaries from "make" in
# ../build, and must be run on the net-quota kernel.
#
#   sudo ./tests/run_tests.sh              # uses this machine's own IPv4 address
#   sudo SERVER_IP=192.168.1.20 ./tests/run_tests.sh
#
# Loopback (127.x) is not counted by net_quota, so the test server must listen
# on a non-loopback address. The machine's own address is enough: the quota
# looks at the destination address, not the network interface. If the machine
# has no such address, the script falls back to a network namespace joined by
# a veth pair (needs CONFIG_VETH in the kernel, see setup_netns.sh).
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/../build"
PORT=5001
failed=0
USE_NETNS=0

cleanup() {
	[ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
	[ "$USE_NETNS" -eq 1 ] && "$HERE/setup_netns.sh" down
}
trap cleanup EXIT

if [ "$(id -u)" -ne 0 ]; then
	echo "run as root: sudo $0" >&2
	exit 2
fi

SERVER_IP="${SERVER_IP:-$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)}"
if [ -n "$SERVER_IP" ]; then
	"$BUILD/net_server" "$SERVER_IP" "$PORT" &
else
	echo "no global IPv4 address found, using a network namespace"
	USE_NETNS=1
	SERVER_IP=10.200.0.2
	"$HERE/setup_netns.sh" down
	"$HERE/setup_netns.sh" up || exit 2
	ip netns exec nqsrv "$BUILD/net_server" "$SERVER_IP" "$PORT" &
fi
SERVER_PID=$!
sleep 1
echo "test server on $SERVER_IP:$PORT"

run() {
	echo "=== $* ==="
	"$@" || failed=1
	echo
}

run "$BUILD/test_errors"
run "$BUILD/test_basic" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_inherit" --server "$SERVER_IP" --port "$PORT"

if [ "$failed" -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit "$failed"
