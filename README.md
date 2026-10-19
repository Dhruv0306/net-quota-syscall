# net-quota-syscall

A custom Linux system call, `net_quota(pid, data_size, period_sec)`, that lets a
process limit the network data of one of its descendants. The limit counts
payload bytes sent and received on one combined counter, the window starts at
the target's first use of the network, and a kernel timer revokes it when the
period ends. When the limit is reached, send/receive fail with `EDQUOT` until
then.

Target platform: Ubuntu 24.04 on x86_64, Linux kernel 7.2.10 (custom build).

```
net-quota-syscall/
├── kernel-code/     net_quota/ directory, headers and patches for the kernel tree
└── user-code/       wrapper library, CLI, tests and design notes
```

## Quick start

1. Build and boot the patched kernel: [kernel-code/README.md](kernel-code/README.md)
2. Build the user-space side and run the tests:

```bash
cd user-code
make
sudo ./tests/run_tests.sh
```

3. Read [user-code/docs/design.md](user-code/docs/design.md) for the decisions
   and known limitations.

## Status

The kernel patches apply cleanly on v7.2.10 and the user-space code compiles
without warnings. The kernel side has not been built or booted yet, so the
tests have not been run against it.

## Branches

Work happens on `Dhruv_Patel/<feature-or-fix-name>` branches.
