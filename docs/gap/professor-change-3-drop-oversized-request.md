# Professor change 3: drop a request that is larger than the remaining quota

Status: **implemented** for sends (hardware run 2026-10-27 and 2026-10-28:
passed) and for receives (compiled with `W=1` against v7.2.12; `test_udp` and
`test_calls` updated; **not yet run on the real kernel**). This page records the
change, the decisions, and what the receive rule can and cannot do.

The list of open gaps is [README.md](README.md).

## The change, as given by the professor

> Drop network request if more than remaining quota, eg if 100 kb limit is
> remaining and 1 mb request is made drop it, don't do it partially.

A send or receive that needs more bytes than the quota has left must fail as a
whole. It must not move any bytes, and it must not be cut down to fit.

## Before this change

| Transfer | Request larger than what is left |
|---|---|
| Send on a stream socket (TCP) | shortened to what is left, the call succeeded with a smaller count |
| Send on a message socket (UDP, ...) | refused whole with `EDQUOT` |
| Receive, any socket | shortened to what is left (the buffer was trimmed) |

## Decisions (project owner, 2026-10-23 and 2026-10-28)

1. **Dropped request costs no quota.** Nothing is counted for it.
2. **Error code stays `EDQUOT`.** The caller is not told whether the request
   was merely too big or the quota was used up. This is on purpose: the kernel
   should not report on every request that is larger than what is left.
3. **Multi-part calls stay as they are.** For `sendfile`, `splice`, `sendmmsg`
   and io_uring, the parts that fit succeed and the first part that does not
   fit fails.
4. **Unused remainder is accepted.** A process can be left with a few bytes it
   cannot use. Network use continues normally when the timer expires.
5. **Receives are judged by the data waiting** (professor's clarification,
   answered on 2026-10-28): "larger than what is left" means the size of the
   incoming data that is waiting, not the size of the buffer; a refused receive
   leaves the data queued; and the rule is applied to TCP streams too, wherever
   there is a way to do it.

## What the code does now

| Case | Result |
|---|---|
| Send larger than what is left, any socket | refused whole, `EDQUOT`, nothing sent, nothing charged |
| UDP receive, a datagram is waiting | judged by the size of that datagram, whatever the buffer. More than what is left: refused whole, `EDQUOT`, datagram stays queued, nothing charged. Otherwise it is received (limited to that datagram) |
| TCP receive, data is waiting | judged by the bytes this call would take, the smaller of the buffer and the bytes queued (a stream has no packet boundaries, so this is the closest reading of the rule). More than what is left: refused whole, `EDQUOT`, data stays queued. A smaller buffer then works |
| Receive, nothing is waiting yet | the size is not known: the receive is shortened to what is left, as before |

The size comes from the protocol's own `SIOCINQ` (for TCP the bytes queued, for
UDP the size of the first datagram). On an unconnected UDP socket the sender of
a datagram that is about to be refused is looked up first with a zero-length
`MSG_PEEK`, because a datagram from a loopback sender is not counted.

## Limits of the receive rule (they are in `user-code/docs/design.md`, 14 to 16)

- **TCP streams:** the rule can be applied, but only to data that is already
  queued. When the call has to wait for data, nobody knows how much will
  arrive, so the buffer is cut to what is left and the rest stays queued. A
  stream also has no packets, so the unit judged is "what this call would take".
- **Other protocols** (SCTP, MPTCP, raw) and calls with `MSG_ERRQUEUE` or
  `MSG_OOB` are not judged in advance: they keep the old rule, a receive cut to
  what is left (a datagram is cut short and its rest discarded).
- **Head of the queue:** a UDP datagram that is larger than what is left stays at
  the head of the queue and blocks the later ones until the timer ends.
- **Race:** the data waiting is looked up just before the receive. The call is
  limited to what was waiting, so the quota is never exceeded, but the judgement
  can be slightly out of date. The lookup takes the socket lock briefly.

## Where it is written down and tested

- Code: `kernel-code/new-files/net_quota/net_quota.c`, `__net_quota_begin()`.
  No new kernel patch: the hooks of patch 0004 already call it.
- Docs: `user-code/docs/design.md`, `docs/how-it-works.md`, `docs/handoff.md`.
- Tests: `test_basic` and `test_threads` (sends), `test_udp` (an unconnected UDP
  socket: the datagram that does not fit is refused, whatever the buffer, stays
  queued, costs nothing), `test_calls` ("TCP receive larger than what is left":
  refused whole, data still queued, a recv of exactly what is left works).
