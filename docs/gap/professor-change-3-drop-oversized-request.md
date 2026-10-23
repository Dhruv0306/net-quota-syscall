# Professor change 3: drop a request that is larger than the remaining quota

Status: **sends implemented on branch `Dhruv_Patel/drop-oversized-send`;
receives wait for the professor's clarification.** This page records the
change, how it differs from what `main` does, the decisions the project owner
has made, and the receive rule that is still open. The send change has been
compiled with `W=1` against the full 7.2.10 tree (no warnings) but has **not**
been run on the real kernel yet.

## The change, as given by the professor

> Drop network request if more than remaining quota, eg if 100 kb limit is
> remaining and 1 mb request is made drop it, don't do it partially.

In short: a send or receive that needs more bytes than the quota has left must
fail as a whole. It must not move any bytes, and it must not be cut down to
fit.

## What `main` does today (before this change)

The rule lives in one place, `__net_quota_begin()` in
`kernel-code/new-files/net_quota/net_quota.c`. When a request is larger than
what is left, it behaves like this:

| Transfer | Request larger than what is left | Meets the professor's rule? |
|---|---|---|
| Send on a stream socket (TCP) | shortened to what is left, the call succeeds with a smaller count | **no** |
| Send on a message socket (UDP, ...) | refused whole with `EDQUOT`, nothing is cut | yes, already |
| Receive, any socket | shortened to what is left (the buffer is trimmed) | **no** |
| Nothing left at all | refused with `EDQUOT` | yes |

So the professor's rule is already met for datagram sends and for the
"nothing left" case. It is not met for TCP sends or for receives, which are
the cases the earlier decision "a transfer that only partly fits is allowed up
to the limit" (handoff section 7) deliberately chose.

## Where the current rule is written down

These places describe partial transfer and would need to change once the
behaviour is decided:

- `docs/handoff.md` (section 2 "Which calls" and "Partial transfer", section 7)
- `user-code/docs/design.md` (Behaviour rules)
- `docs/how-it-works.md` (flow two, a transfer under a quota)
- the comments in `net_quota.c` (file header and `__net_quota_begin()`),
  `include/net/net_quota.h` and `include/linux/net_quota.h`
- the tests that assert it: `test_basic` ("send 8000: shortened to the 4000
  bytes left") and `test_udp` ("recv of the 400-byte echo is cut to the 100
  bytes left")

## Decisions (project owner, 2026-10-23)

1. **Receives: not decided.** An earlier answer was "drop the entire
   receiving packet". The project owner has withdrawn that as a standing rule:
   it applies only as the professor clarified it, and the clarification has
   not been recorded here yet. Until it is, receives stay as they are today
   (shortened to what is left).
2. **A dropped request costs no quota.** Nothing is counted for it.
3. **Error code stays `EDQUOT`.** The caller is not told whether the request
   was merely too big or the quota was used up completely. This is on purpose:
   the kernel should not report on every request that is larger than what is
   left.
4. **Multi-part calls stay as they are.** For `sendfile`, `splice`, `sendmmsg`
   and io_uring, the parts that fit succeed and the first part that does not
   fit fails, so a caller may see a short count and then an error.
5. **Unused remainder is accepted.** A process can be left with a few bytes it
   cannot use, because every request is larger than what is left. Network use
   continues normally when the timer expires and the quota is revoked.

## Still to pin down: receives

Waiting for the professor's clarification, to be written here as given. When
it arrives, these points need an answer because of how a receive works: at the
moment of the call the kernel only knows the size of the caller's buffer, not
the size of the data waiting.

- how "larger than what is left" is judged for a receive: by the size of the
  data that is waiting (for a datagram socket, the next datagram; a stream
  socket has no packet boundaries) or by the size of the buffer, which would
  stop programs that read with a large buffer long before the limit;
- what "drop" means for the data: refuse the call and leave the data queued in
  the socket, or take it off the socket and discard it. Both cost no quota
  (decision 2).

## What the change would involve

- In `__net_quota_begin()`: replace "shorten to what is left" by "refuse with
  `NQ_E_QUOTA_EXCEEDED`" for TCP sends. Receives change only after the
  professor's clarification is recorded.
- If receives stop being trimmed as well, the trim-and-restore code
  (`iov_iter_truncate()` / `iov_iter_reexpand()`, the `trimmed` field of
  `struct net_quota_ticket`) becomes dead and can be removed.
- A new patch in `kernel-code/patches/` only if an existing kernel file has to
  change. The rule itself is in a new file, so `net_quota.c` and the headers
  may be enough.
- Update the tests above and the documents listed above. `test_basic` must
  now expect the 8000-byte send with 4000 left to be refused with `EDQUOT`.
  The `test_udp` receive check changes only if receives change.
- Tests that probably stay valid because their request sizes divide the limit
  evenly (to be confirmed by a run): `test_fork` (1500-byte sends, limit 6000)
  and `test_realprog.sh` (1000-byte `dd` blocks, limit 20000).
  `test_threads` has not been checked.

## Decision record

| Question | Decision | By | Date |
|---|---|---|---|
| 1 Receives | not decided: applies only as clarified by the professor; today's behaviour stays until then | project owner | 2026-10-23 |
| 2 Dropped request costs quota | no | project owner | 2026-10-23 |
| 3 Error code | keep `EDQUOT` | project owner | 2026-10-23 |
| 4 Multi-part calls | keep as they are | project owner | 2026-10-23 |
| 5 Unused remainder | accepted; use continues when the timer expires | project owner | 2026-10-23 |
