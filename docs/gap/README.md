# Open gaps

One list of what is still open in the project, so nothing lives only in
someone's head. Every item has a kind, a status and the next action. Items are
closed by editing this page in the same change that fixes them.

Kinds: **behaviour** (what the system does), **proof** (a claim not yet
tested), **doc** (a document is wrong or missing), **process** (how the work is
done).

Last reviewed: 2026-10-23, after the three professor changes.

## Open

| ID | Kind | Gap | Next action | Needs |
|---|---|---|---|---|
| G1 | behaviour | The receive side of professor change 3 ("drop the entire receiving packet") is not decided: how the size is judged and whether dropped data stays queued or is discarded. Receives are still shortened. | Record the professor's wording in [professor-change-3-drop-oversized-request.md](professor-change-3-drop-oversized-request.md), then implement it and update `test_udp` | the professor's clarification |
| G2 | behaviour | A restricted process escapes its limit by forking: the child starts unrestricted (change 2). A socket handed to an unrestricted process escapes the same way. | Decide whether this is the intended rule. If not, the options are in `handoff.md` section 8 (a shared counter closes the fork case, not the descriptor-passing case) | the professor |
| G3 | proof | Changes 1, 2 and 3 (sends) have compiled with `W=1` but never run on the real kernel. | Boot a kernel built from `main`, run `sudo ./tests/run_tests.sh`, record the result in `handoff.md` section 4 and in the README status table. Include `test_calls`, `test_uring` and `test_zerocopy`: a `FAIL` in the first two, or a `NOT counted` finding in the third, is a new gap and goes into this table | the test machine |
| G4 | proof | The debug configuration (lockdep, sleeping-in-atomic checks) has never been run. | Build with `config/net_quota_debug.config`, run the tests, check `dmesg` | the test machine |
| G6 | behaviour | Descendants whose parent has died are reparented to init or a subreaper and can no longer be restricted. | Decide whether to document it as accepted (it is, in `design.md` limitation 7) or change the check | decision |
| G7 | behaviour | A zero-length send with nothing left returns `EDQUOT`. Minor. | Decide whether `len == 0` should pass; add a test either way | decision |
| G8 | doc | `user-code/*.logs` are old captures. `full_test.logs` contains the removed `test_inherit`. | Replace them with a run of the current code (comes with G3), or delete them | G3 |
| G9 | process | Patches 0001 to 0005 still carry `From: Claude (AI-assisted)` and `[PATCH n/5]`, against the authorship rule. About 9 early commits on `main` list Claude as author. | Optional: rewrite the five patch headers (file content only, no history rewrite). Leave the commits | decision |
| G10 | process | Patches 0002 and 0006 do not build alone with the current `net_quota.c`; only the whole series does. | Documented in `kernel-code/README.md` and `setup-and-build.md`. Optional: squash the series into a clean one | decision |

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
