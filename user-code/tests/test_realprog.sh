#!/usr/bin/env bash
# test_realprog.sh <build-dir> <server-ip> <port>
#
# An ordinary program, dd writing to a TCP connection opened by bash's
# /dev/tcp, is started through nq_run. It knows nothing about quotas; the
# kernel stops it at the limit. A control run without nq_run shows that the
# limit, not the setup, is what stopped it.
#
# nq_run restricts the process it starts. bash opens the connection and then
# *replaces itself* with dd (exec), so dd is that same process and the quota,
# which survives exec, is dd's. If bash instead ran dd as a child, dd would be
# a fork: a child does not inherit the quota (professor's change), so dd would
# not be limited at all. A last check shows that case on purpose.
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
# push_exec: dd replaces bash, its exit status is the exit status of nq_run.
# push:      dd is a child of bash; the status is written to $3.rc.
push_exec='exec 3<>"/dev/tcp/$1/$2"; exec dd if=/dev/zero bs=1000 count=60 >&3 2>"$3"'
push='exec 3<>"/dev/tcp/$1/$2"; dd if=/dev/zero bs=1000 count=60 >&3 2>"$3"; echo $? >"$3.rc"'

echo "test_realprog: dd pushes $PUSH bytes, limit $LIMIT bytes"

timeout 30 "$BUILD/nq_run" "$LIMIT" 20 bash -c "$push_exec" _ "$IP" "$PORT" "$tmp/limited" 2>"$tmp/nq.err"
limited_rc=$?

grep -q "limit of $LIMIT bytes set" "$tmp/nq.err" \
	&& ok "nq_run set the limit before dd started" \
	|| bad "nq_run did not report the limit being set: $(cat "$tmp/nq.err")"
grep -q "Disk quota exceeded" "$tmp/limited" \
	&& ok "dd failed with: Disk quota exceeded (the program never heard of quotas)" \
	|| bad "dd did not report EDQUOT: $(cat "$tmp/limited" 2>/dev/null)"
grep -q "^$LIMIT bytes" "$tmp/limited" \
	&& ok "dd got exactly $LIMIT bytes out before the limit" \
	|| bad "dd byte count is not $LIMIT: $(grep bytes "$tmp/limited" 2>/dev/null)"
if [ "$limited_rc" -ne 0 ] && [ "$limited_rc" -ne 124 ]; then
	ok "dd exited with an error status ($limited_rc)"
else
	bad "dd exited without an error (status $limited_rc)"
fi

# The same program run as a child of the restricted shell: not limited.
timeout 30 "$BUILD/nq_run" "$LIMIT" 20 bash -c "$push" _ "$IP" "$PORT" "$tmp/forked" 2>/dev/null
if [ "$(cat "$tmp/forked.rc" 2>/dev/null)" = 0 ] && grep -q "^$PUSH bytes" "$tmp/forked"; then
	ok "dd run as a child of the restricted shell is not limited: a child starts unrestricted"
else
	bad "dd as a child of the restricted shell was limited or failed: $(cat "$tmp/forked" 2>/dev/null)"
fi

timeout 30 bash -c "$push" _ "$IP" "$PORT" "$tmp/free"
if [ "$(cat "$tmp/free.rc" 2>/dev/null)" = 0 ] && grep -q "^$PUSH bytes" "$tmp/free"; then
	ok "control run without a quota: all $PUSH bytes went through"
else
	bad "control run failed: $(cat "$tmp/free" 2>/dev/null)"
fi

echo "test_realprog: $([ "$fail" -eq 0 ] && echo PASSED || echo FAILED)"
exit "$fail"
