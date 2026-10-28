# net-quota-syscall

A custom Linux system call, `net_quota(pid, data_size, period_sec)`, that lets a
process limit the network data of one of its descendants. The limit counts
payload bytes sent and received on one combined counter, the window starts the
moment the limit is set, and a kernel timer revokes it when the period ends. When the limit is reached, send/receive fail with `EDQUOT` until
then. A send larger than what is left is refused whole, never done partly.

Target platform: Ubuntu 24.04 on x86_64, Linux kernel 7.2.10 (custom build).

```
net-quota-syscall/
├── kernel-code/     net_quota/ directory, headers and patches for the kernel tree
├── user-code/       wrapper library, CLI, tests and design notes
└── docs/            handoff, setup and build, how it works, removal guides, open gaps
    └── gap/         register of open gaps and the pending professor change
```

## Quick start

1. Build and boot the patched kernel, every command from source to reboot:
   [docs/setup-and-build.md](docs/setup-and-build.md) (short version:
   [kernel-code/README.md](kernel-code/README.md))
2. Build the user-space side and run the tests:

```bash
cd user-code
make
sudo ./tests/run_tests.sh
```

3. Read [docs/how-it-works.md](docs/how-it-works.md) to see how input and output
   flow through user space and the kernel, with diagrams.
4. Read [user-code/docs/design.md](user-code/docs/design.md) for the decisions
   and known limitations.
5. New to the project: start with [docs/handoff.md](docs/handoff.md). Open
   issues are in [docs/gap/README.md](docs/gap/README.md). To take the system
   call out again: [docs/removing-net-quota.md](docs/removing-net-quota.md) and
   [docs/removing-custom-kernel.md](docs/removing-custom-kernel.md).

## Status

Third run on the test machine on 2026-10-28, with a kernel built from patches
0001 to 0006 and the new files (`./tests/run_tests.sh`, as root): `ALL TESTS
PASSED`, and the kernel log check printed "no new warnings or oopses".

| What | Result |
|---|---|
| Change 1: the timer starts when the limit is set | `test_timer` passed |
| Change 2: a child does not inherit the quota | `test_fork` passed |
| Change 3: a send larger than what is left is refused whole | `test_basic`, `test_udp`, `test_threads` passed |
| Other call paths: `sendmsg`/`recvmsg`, `sendmmsg`/`recvmmsg`, `sendfile`, `splice`, `MSG_PEEK`, non-blocking, a zero-length send, IPv6, io_uring send/receive/zero-copy send | `test_calls` and `test_uring` passed, each path counted exactly |
| `TCP_ZEROCOPY_RECEIVE` | was not counted on the first run (2026-10-27); patch 0006 refuses it with `EOPNOTSUPP` under a quota; `test_zerocopy` passed: refused, then a counted `recv()` of the same data |
| A real program stopped at the limit (`test_realprog.sh`) | passed: `dd` stopped at exactly 20000 bytes with `EDQUOT` |
| Debug options | the tested kernel had every option of `config/net_quota_debug.config` on (lockdep, atomic-sleep, spinlock, timer-object checks); nothing was logged |
| Change 3, receive side: judged by the data waiting, refused whole when it is more than what is left, data stays queued | compiled with `W=1`; `test_udp` and `test_calls` updated; **not yet run** |

What is still open is in [docs/gap/README.md](docs/gap/README.md); the history
and next steps are in [docs/handoff.md](docs/handoff.md).

## Branches

Work happens on `Dhruv_Patel/<feature-or-fix-name>` branches.
