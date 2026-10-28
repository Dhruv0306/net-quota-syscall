# Open gaps

One list of what is still open in the project, so nothing lives only in
someone's head. Every item has a kind, a status and the next action. Items are
closed by editing this page in the same change that fixes them.

Kinds: **behaviour** (what the system does), **proof** (a claim not yet
tested), **doc** (a document is wrong or missing), **process** (how the work is
done).

Last reviewed: 2026-10-28, after the third hardware run (all tests passed, patch 0006 in).

## Open

| ID | Kind | Gap | Next action | Needs |
|---|---|---|---|---|
| G1 | behaviour | The receive side of professor change 3 ("drop the entire receiving packet") is not decided: how the size is judged and whether dropped data stays queued or is discarded. Receives are still shortened. | Record the professor's wording in [professor-change-3-drop-oversized-request.md](professor-change-3-drop-oversized-request.md), then implement it and update `test_udp` | the professor's clarification |
| G2 | behaviour | A restricted process escapes its limit by forking: the child starts unrestricted (change 2). A real example from the 2026-10-27 run: `nq_run ... bash -c 'dd ...'` left dd unlimited because bash forks it. A socket handed to an unrestricted process escapes the same way. | Decide whether this is the intended rule. If not, the options are in `handoff.md` section 8 (a shared counter closes the fork case, not the descriptor-passing case) | the professor |

## Closed in the 2026-10-23 review

| ID | What was wrong | Fixed in |
|---|---|---|
| D1 | `how-it-works.md` flowchart sent an oversized TCP send down the "shorten" path | `how-it-works.md` section 3 |
| D2 | "revoked for good" suggested a process could never be restricted again; in fact an ancestor can set a new quota after expiry | `how-it-works.md`, `design.md`, two code comments |
| D3 | README status named the removed inheritance test and did not show what was compiled versus run | `README.md`, `kernel-code/README.md` |
| D4 | `kernel-code/README.md` said `make defconfig` and had `make modules_install && make modules_install install` | `kernel-code/README.md` |
| D5 | `design.md` did not say where each rule came from, and missed limitations (orphans, uncounted paths, descriptor passing, unused remainder, multi-part calls) | `design.md` decision log, limitations 7 to 12 |
| D6 | `ESRCH` for an exiting target was not in the error table | `design.md` |
| D7 | No way to update an existing build to a newer `main`, and no pointer to the removal guides | `setup-and-build.md` |
| D8 | `handoff.md` was not in the repository | `docs/handoff.md` |
| D9 | No single list of open items | this page |
| D10 | No test used `sendmsg`, `recvmsg`, `sendmmsg`, `recvmmsg`, `sendfile`, `splice`, io_uring, `MSG_PEEK`, non-blocking sockets or IPv6, and nothing checked `TCP_ZEROCOPY_RECEIVE`, although the docs said the hooks cover them | tests `test_calls`, `test_uring`, `test_zerocopy`. Written, compiled and smoke-run; **results come with the G3 run** |
| D11 | The series had three patches (0002, 0006, 0007) editing `kernel/fork.c` one on top of the other, so a dry run of the whole series on a fresh tree failed on 0007 ("2 out of 2 hunks FAILED"), and 0002 and 0006 did not build alone | folded into one `0002`, 0006 and 0007 removed; `fork.c` is byte for byte the same as before. See `kernel-code/README.md` and `setup-and-build.md` |
| D12 | No run on the real kernel after the three professor changes and the new tests | run of 2026-10-27: all test programs passed except the two items in G3 and the finding in G11; kernel log clean |
| D13 | `test_realprog.sh` failed on the hardware run: dd ran as a child of the restricted bash, which starts unrestricted (change 2) | test now `exec`s dd, and a second check shows the forked case on purpose; `nq_run.c` and `design.md` say so |
| D14 | `test_realprog.sh` failed on the first run of 2026-10-27 | passed on the rerun after the test was changed to `exec` dd; the whole suite ended with `ALL TESTS PASSED` |
| D15 | `TCP_ZEROCOPY_RECEIVE` was not counted (was G11) | decided 2026-10-28: refuse the option under a quota. Patch `0006` (`net/ipv4/tcp.c`) returns `EOPNOTSUPP`; `test_zerocopy` now expects the refusal and then a counted `recv()`. Compiled with `W=1` on 7.2.12; **passed on the test machine on 2026-10-28** |
| D16 | A zero-length send with nothing left returns `EDQUOT` (was G7) | decided 2026-10-28: intended. Documented as `design.md` limitation 12 and checked in `test_calls` |
| D17 | Four patch headers said `From: Claude` (was G9) | rewritten: all six patches have Dhruv as author and Claude as co-author; headers read `[PATCH n/6]` |
| D18 | Orphaned descendants cannot be restricted (was G6) | accepted 2026-10-28; `design.md` limitation 7 |
| D19 | Old `user-code/*.logs` (was G8) | removed 2026-10-28; run `run_tests.sh` for a current log |
| D20 | IPv6 had never run (was G3): the test machine has no IPv6 address | the runner gives the loopback interface `fd00:6e71::1` for the run; the IPv6 case of `test_calls` passed on 2026-10-28 |
| D21 | Only two of the five debug options were confirmed (was G4) | the tested kernel has all five on (`PROVE_LOCKING`, `DEBUG_ATOMIC_SLEEP`, `DEBUG_SPINLOCK`, `DEBUG_OBJECTS`, `DEBUG_OBJECTS_TIMERS`); the runs found nothing |
| D22 | Patch 0006 and the changed tests had not run | the third run (2026-10-28) passed: `test_zerocopy` (refusal, then a counted `recv()`), the zero-length check in `test_calls`, and the rest |
