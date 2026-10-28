#!/usr/bin/env bash
# Runs all net_quota tests. Needs root and the binaries from "make" in
# ../build, and must be run on the net-quota kernel.
#
#   sudo ./tests/run_tests.sh              # uses this machine's own IPv4 address
#   sudo SERVER_IP=192.168.1.20 ./tests/run_tests.sh
#   sudo SERVER_IP6=2001:db8::5 ./tests/run_tests.sh   # IPv6 case of test_calls
# Without a global IPv6 address and without SERVER_IP6, the script gives the
# loopback interface a private address (fd00:6e71::1) for the run, so that the
# IPv6 case can still run: net_quota decides by destination address, and only
# ::1 counts as loopback. The address is removed again at the end.
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
LOOP_PORT=5002
UDP_PORT=5003
failed=0
USE_NETNS=0

# Lines that mean the kernel complained. Counted before and after, so that
# boot-time noise does not matter; only new lines count.
log_problems() { dmesg 2>/dev/null | grep -cE 'BUG:|WARNING:|Oops|lockdep|cut here|net_quota'; }

cleanup() {
	[ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
	[ -n "${LOOP_PID:-}" ] && kill "$LOOP_PID" 2>/dev/null
	[ -n "${UDP_PID:-}" ] && kill "$UDP_PID" 2>/dev/null
	[ -n "${V6_PID:-}" ] && kill "$V6_PID" 2>/dev/null
	[ -n "${V6_ADDED:-}" ] && ip -6 addr del "$V6_ADDED/128" dev lo 2>/dev/null
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
"$BUILD/net_server" 127.0.0.1 "$LOOP_PORT" &
LOOP_PID=$!
if [ "$USE_NETNS" -eq 1 ]; then
	ip netns exec nqsrv "$BUILD/net_server" "$SERVER_IP" "$UDP_PORT" udp &
else
	"$BUILD/net_server" "$SERVER_IP" "$UDP_PORT" udp &
fi
UDP_PID=$!
# IPv6 echo server for the IPv6 case of test_calls: only with a global IPv6
# address of this machine (or SERVER_IP6), not in the namespace fallback.
V6_ARGS=()
if [ "$USE_NETNS" -eq 0 ]; then
	SERVER_IP6="${SERVER_IP6:-$(ip -6 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)}"
	if [ -z "$SERVER_IP6" ] && [ "$(cat /proc/sys/net/ipv6/conf/all/disable_ipv6 2>/dev/null)" = 0 ] &&
	   ip -6 addr add fd00:6e71::1/128 dev lo nodad 2>/dev/null; then
		SERVER_IP6=fd00:6e71::1
		V6_ADDED=$SERVER_IP6
	fi
	if [ -n "$SERVER_IP6" ]; then
		"$BUILD/net_server" "$SERVER_IP6" "$PORT" &
		V6_PID=$!
		V6_ARGS=(--server6 "$SERVER_IP6")
	fi
fi
sleep 1
echo "test server on $SERVER_IP:$PORT"
LOG_BEFORE=$(log_problems)

run() {
	echo "=== $* ==="
	"$@" || failed=1
	echo
}

run "$BUILD/test_errors"
run "$BUILD/test_basic" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_fork" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_timer" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_loopback" --server "$SERVER_IP" --port "$PORT" --loop-port "$LOOP_PORT"
run "$BUILD/test_exit" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_udp" --server "$SERVER_IP" --port "$UDP_PORT"
run "$BUILD/test_threads" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_calls" --server "$SERVER_IP" --port "$PORT" ${V6_ARGS[@]+"${V6_ARGS[@]}"}
run "$BUILD/test_uring" --server "$SERVER_IP" --port "$PORT"
run "$BUILD/test_zerocopy" --server "$SERVER_IP" --port "$PORT"
run "$HERE/test_realprog.sh" "$BUILD" "$SERVER_IP" "$PORT"

LOG_AFTER=$(log_problems)
if [ "$LOG_AFTER" -gt "$LOG_BEFORE" ]; then
	echo "KERNEL LOG: new warnings since the tests started:"
	dmesg | grep -E 'BUG:|WARNING:|Oops|lockdep|cut here|net_quota' | tail -10
	failed=1
else
	echo "kernel log: no new warnings or oopses"
fi

if [ "$failed" -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit "$failed"
