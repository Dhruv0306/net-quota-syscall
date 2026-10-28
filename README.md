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

Run on the test machine on 2026-10-27 with a kernel built from `main`
(`sudo ./tests/run_tests.sh`). The second run ended with `ALL TESTS PASSED` and
a clean kernel log:

| What | Result |
|---|---|
| Change 1: the timer starts when the limit is set | `test_timer` passed |
| Change 2: a child does not inherit the quota | `test_fork` passed |
| Change 3: a send larger than what is left is refused whole | `test_basic`, `test_udp`, `test_threads` passed |
| Other call paths: `sendmsg`/`recvmsg`, `sendmmsg`/`recvmmsg`, `sendfile`, `splice`, `MSG_PEEK`, non-blocking, io_uring send/receive/zero-copy send | `test_calls` and `test_uring` passed (the IPv6 case was skipped: no IPv6 server) |
| `TCP_ZEROCOPY_RECEIVE` | **not counted** (`test_zerocopy` finding): data read this way is not charged. Gap G11 |
| A real program stopped at the limit (`test_realprog.sh`) | passed on the rerun: `dd` stopped at exactly 20000 bytes with `EDQUOT`. The first run failed because of the test (dd was a forked child, which starts unrestricted); it now `exec`s dd and also checks the forked case on purpose |
| Change 3, receive side | waiting for the professor's wording |

Next steps and the open gaps are in [docs/handoff.md](docs/handoff.md) and
[docs/gap/README.md](docs/gap/README.md).

## Branches

Work happens on `Dhruv_Patel/<feature-or-fix-name>` branches.
