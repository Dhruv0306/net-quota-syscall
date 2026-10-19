#!/usr/bin/env bash
# Runs all net_quota tests. Needs root (network namespace) and the binaries
# from "make" in ../build, and must be run on the net-quota kernel.
#
#   sudo ./tests/run_tests.sh
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/../build"
SERVER_IP=10.200.0.2
PORT=5001
failed=0

cleanup() {
	[ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
	"$HERE/setup_netns.sh" down
}
trap cleanup EXIT

if [ "$(id -u)" -ne 0 ]; then
	echo "run as root: sudo $0" >&2
	exit 2
fi

"$HERE/setup_netns.sh" down
"$HERE/setup_netns.sh" up || exit 2
ip netns exec nqsrv "$BUILD/net_server" "$SERVER_IP" "$PORT" &
SERVER_PID=$!
sleep 1

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
