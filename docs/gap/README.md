# Open gaps

One list of what is still open in the project, so nothing lives only in
someone's head. Every item has a kind, a status and the next action. Items are
closed by editing this page in the same change that fixes them.

Kinds: **behaviour** (what the system does), **proof** (a claim not yet
tested), **doc** (a document is wrong or missing), **process** (how the work is
done).

Last reviewed: 2026-10-27, after the second hardware run (all tests passed).

## Open

| ID | Kind | Gap | Next action | Needs |
|---|---|---|---|---|
| G1 | behaviour | The receive side of professor change 3 ("drop the entire receiving packet") is not decided: how the size is judged and whether dropped data stays queued or is discarded. Receives are still shortened. | Record the professor's wording in [professor-change-3-drop-oversized-request.md](professor-change-3-drop-oversized-request.md), then implement it and update `test_udp` | the professor's clarification |
| G2 | behaviour | A restricted process escapes its limit by forking: the child starts unrestricted (change 2). A real example from the 2026-10-27 run: `nq_run ... bash -c 'dd ...'` left dd unlimited because bash forks it. A socket handed to an unrestricted process escapes the same way. | Decide whether this is the intended rule. If not, the options are in `handoff.md` section 8 (a shared counter closes the fork case, not the descriptor-passing case) | the professor |
| G3 | proof | The IPv6 case of `test_calls` has not run: it was skipped on both hardware runs of 2026-10-27 (no IPv6 server). Everything else passed, including `test_realprog.sh`. | Run `SERVER_IP6=<global IPv6 of the machine> sudo ./tests/run_tests.sh`, or accept IPv6 as untested and say so in `design.md` | the test machine, if it has an IPv6 address |
| G4 | proof | The debug configuration (lockdep, sleeping-in-atomic checks) has not been run on purpose. Both 2026-10-27 runs left the kernel log clean, and the boot log says "RCU lockdep checking is enabled", which hints that lockdep may already have been on. | Check `grep -E 'PROVE_LOCKING\|DEBUG_ATOMIC_SLEEP' /boot/config-$(uname -r)`; if absent, build with `config/net_quota_debug.config` and rerun | the test machine |
| G11 | behaviour | **`TCP_ZEROCOPY_RECEIVE` is not counted** (`test_zerocopy`, 2026-10-27): data read through that `getsockopt()` bypasses the quota, so a program that asks for it can receive without limit. Options: refuse the option for a process that has a quota (the process falls back to `recv()`, which is counted), or charge what it takes. Either is a new patch in `net/ipv4/tcp.c`. | Decide, then implement and rerun `test_zerocopy` (it should then report counted) | decision |
| G6 | behaviour | Descendants whose parent has died are reparented to init or a subreaper and can no longer be restricted. | Decide whether to document it as accepted (it is, in `design.md` limitation 7) or change the check | decision |
| G7 | behaviour | A zero-length send with nothing left returns `EDQUOT`. Minor. | Decide whether `len == 0` should pass; add a test either way | decision |
| G8 | doc | `user-code/full_test.logs` is an old capture; it still contains the removed `test_inherit`. (`smoke_test.logs` and `demo_test.logs` show `nq_cli` and `nq_demo`, which did not change, so they are fine.) | Save a current run over it: `cd user-code && sudo ./tests/run_tests.sh 2>&1 | tee full_test.logs` | the test machine |
| G9 | process | Patches 0001, 0003, 0004 and 0005 still carry `From: Claude (AI-assisted)` in their headers, against the authorship rule. About 9 early commits on `main` list Claude as author. | Optional: rewrite the four patch headers (file content only, no history rewrite). Leave the commits | decision |

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
