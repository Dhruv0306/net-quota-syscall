#!/usr/bin/env bash
# test_realprog.sh <build-dir> <server-ip> <port>
#
# An ordinary program, dd writing to a TCP connection opened by bash's
# /dev/tcp, is started through nq_run. It knows nothing about quotas; the
# kernel stops it at the limit. A control run without nq_run shows that the
# limit, not the setup, is what stopped it.
set -u

BUILD="$1"; IP="$2"; PORT="$3"
LIMIT=20000
PUSH=60000
fail=0

ok()  { echo "  ok   [realprog] $*"; }
bad() { echo "  FAIL [realprog] $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 60 blocks of 1000 bytes through one TCP connection. $1 ip, $2 port, $3 out file.
push='exec 3<>"/dev/tcp/$1/$2"; dd if=/dev/zero bs=1000 count=60 >&3 2>"$3"; echo $? >"$3.rc"'

echo "test_realprog: dd pushes $PUSH bytes, limit $LIMIT bytes"

timeout 30 "$BUILD/nq_run" "$LIMIT" 20 bash -c "$push" _ "$IP" "$PORT" "$tmp/limited" 2>"$tmp/nq.err"

grep -q "limit of $LIMIT bytes set" "$tmp/nq.err" \
	&& ok "nq_run set the limit before dd started" \
	|| bad "nq_run did not report the limit being set: $(cat "$tmp/nq.err")"
grep -q "Disk quota exceeded" "$tmp/limited" \
	&& ok "dd failed with: Disk quota exceeded (the program never heard of quotas)" \
	|| bad "dd did not report EDQUOT: $(cat "$tmp/limited" 2>/dev/null)"
grep -q "^$LIMIT bytes" "$tmp/limited" \
	&& ok "dd got exactly $LIMIT bytes out before the limit" \
	|| bad "dd byte count is not $LIMIT: $(grep bytes "$tmp/limited" 2>/dev/null)"
if [ -s "$tmp/limited.rc" ] && [ "$(cat "$tmp/limited.rc")" -ne 0 ]; then
	ok "dd exited with an error status"
else
	bad "dd exited without an error"
fi

timeout 30 bash -c "$push" _ "$IP" "$PORT" "$tmp/free"
if [ "$(cat "$tmp/free.rc" 2>/dev/null)" = 0 ] && grep -q "^$PUSH bytes" "$tmp/free"; then
	ok "control run without a quota: all $PUSH bytes went through"
else
	bad "control run failed: $(cat "$tmp/free" 2>/dev/null)"
fi

echo "test_realprog: $([ "$fail" -eq 0 ] && echo PASSED || echo FAILED)"
exit "$fail"
