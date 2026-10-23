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

| What | Compiled | Run on the real kernel |
|---|---|---|
| Everything up to the professor's changes | yes | yes: all 9 test programs passed, kernel log clean |
| Change 1: the timer starts when the limit is set | yes, `W=1`, no warnings | **not yet** |
| Change 2: a child does not inherit the quota | yes, `W=1`, no warnings | **not yet** |
| Change 3: a send larger than what is left is refused whole | yes, `W=1`, no warnings | **not yet** |
| Change 3, receive side | waiting for the professor's wording | |

The first job is to boot a kernel built from the current `main` and run
`sudo ./tests/run_tests.sh`, then record the result here. Details and the next
steps are in [docs/handoff.md](docs/handoff.md).

## Branches

Work happens on `Dhruv_Patel/<feature-or-fix-name>` branches.
