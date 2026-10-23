# net-quota-syscall

A custom Linux system call, `net_quota(pid, data_size, period_sec)`, that lets a
process limit the network data of one of its descendants. The limit counts
payload bytes sent and received on one combined counter, the window starts the
moment the limit is set, and a kernel timer revokes it when the period ends. When the limit is reached, send/receive fail with `EDQUOT` until
then.

Target platform: Ubuntu 24.04 on x86_64, Linux kernel 7.2.10 (custom build).

```
net-quota-syscall/
├── kernel-code/     net_quota/ directory, headers and patches for the kernel tree
├── user-code/       wrapper library, CLI, tests and design notes
└── docs/            setup and build guide, and how the data flows through the system
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

## Status

The patches apply cleanly on v7.2.10, and the kernel builds and boots as
`7.2.10-netquota`. The full test run passed (errors, limit and partial
transfer, inheritance, timer, loopback exclusion, exit cleanup, UDP, threads,
and a real program started with `nq_run`) with no new kernel log warnings.
The latest change, the timer starting when the limit is set, compiles cleanly
but has not been booted and tested yet.

## Branches

Work happens on `Dhruv_Patel/<feature-or-fix-name>` branches.
