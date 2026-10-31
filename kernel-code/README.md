# kernel-code

Kernel side of the `net_quota` system call for Linux **7.2.12** on x86_64,
laid out the way the course reading material (CS G623, system call
implementation and kernel recompilation) does it: the new system call lives in
its own top-level directory with its own Makefile and header.

```
kernel-code/
├── new-files/                      the six new files; copy onto the root of the kernel source
│   ├── net_quota/                  the course's add_syscall/ directory
│   │   ├── net_quota.c               the system call and the quota logic
│   │   ├── net_quota_syscall.h       prototype of sys_net_quota
│   │   └── Makefile                  obj-y := net_quota.o
│   └── include/
│       ├── linux/net_quota.h         quota struct (lives inside signal_struct)
│       ├── net/net_quota.h           socket hooks, inline fast path
│       └── uapi/linux/net_quota.h    NQ_* return codes shared with user space
├── patches/                        edits to existing kernel files
│   ├── 0001  include/linux/sched/signal.h   quota field in signal_struct
│   ├── 0002  kernel/fork.c                  init the quota in copy_signal(); a child starts unrestricted
│   ├── 0003  kernel/exit.c                  clear the quota in do_exit()
│   ├── 0004  net/socket.c                   count sends and receives
│   ├── 0005  Kbuild, syscall_64.tbl, asm-generic/syscalls.h, linux/syscalls.h
│   └── 0006  net/ipv4/tcp.c                 refuse TCP_ZEROCOPY_RECEIVE under a quota
└── config/
    ├── net_quota.config            names the build 7.2.12-netquota
    └── net_quota_debug.config      optional lockdep / atomic-sleep checks
```

The full step-by-step guide, with the GRUB update, the reboot, the checks and
what to do when something fails, is
[docs/setup-and-build.md](../docs/setup-and-build.md). This page is the short
version.

## Status

A kernel built from all six patches and the new files was booted on the test
machine and every test in `user-code/tests` passed, with no new warnings in the
kernel log. That kernel had every option of `config/net_quota_debug.config` on
(lockdep, atomic-sleep, spinlock and timer-object checks), so those ran during
the tests too. Patch 0006 (refuse `TCP_ZEROCOPY_RECEIVE` under a quota) is proven
by `test_zerocopy`. The receive side of professor change 3 (judged by the data
waiting; `net_quota.c` only, no new patch) passed `test_udp` and the TCP receive
case of `test_calls`, also with lockdep on and a clean kernel log.

## Kernel version

These pages are written for **Linux 7.2.12**, the tree on the test machine.
Checked on a fresh v7.2.12 tree, with the setup guide's own commands:

- the six patches dry-run clean one by one (each edits different files), apply
  in order, and reversing them restores the nine patched files;
- `kernel/fork.o`, `kernel/exit.o`, `net/socket.o`, `net/ipv4/tcp.o` and
  `net_quota/net_quota.o` compile with `W=1` without warnings;
- with `config/net_quota.config` added, `make kernelrelease` prints
  `7.2.12-netquota`: the build name is the kernel version plus `-netquota`.

The patches also dry-run and apply cleanly on v7.2.10 and reverse cleanly there
(that series was compiled on 7.2.10 only in an earlier form). The hunks fit with
small offsets, so another 7.2.x is likely to work too, but only 7.2.10 and
7.2.12 were checked. On a different version, every `7.2.12` in these pages
becomes that version, and a `Hunk FAILED` from `patch` means the source is not
close enough: see the table at the end of `docs/setup-and-build.md`.

Until 2026-10-24 there were seven patches, and three of them (0002, 0006,
0007) edited `kernel/fork.c` one on top of the other, so a dry run of the whole
series against a fresh tree failed on 0007. They were folded into one 0002 that
adds the `#include` and the single `net_quota_init()` call; the resulting
`kernel/fork.c` is byte for byte the same as the old three produced. A tree that
was built from the old series needs nothing: its `fork.c` already is the final
one. `docs/setup-and-build.md` says how to bring a tree in any other state up to
date.

## Following the course steps on 7.2.12

The reading material was written for 5.19.8. Same recipe, three differences:

| Course step (5.19.8) | On 7.2.12 |
|---|---|
| 3.1 add `add_syscall/` to `core-y` in the top-level `Makefile` | That line no longer exists. The list of top-level directories moved to the root file **`Kbuild`**: add `obj-y += net_quota/` after `obj-y += crypto/`. |
| 3.2 add `451 common add_syscall sys_add_syscall` | Next free number is **472**: `472 common net_quota sys_net_quota` (tab-separated). |
| 3.3 prototype in `include/asm-generic/syscalls.h` | Still exists, but x86 never includes it, so this edit has no effect on x86_64. Kept to follow the steps. |
| 3.4 prototype in `include/linux/syscalls.h` | Same edit, after `sys_rseq_slice_yield`. |

Unlike the add-two-numbers example, this system call also has to count
traffic, so four more existing files are edited (patches 0001 to 0004).

## Part A: prepare (course "Basic Preparation")

```bash
sudo su
apt install build-essential flex bison libssl-dev libelf-dev bc
cd /usr/src/linux-7.2.12        # the tarball from kernel.org, already extracted
```

It is worth doing one plain recompilation (Part C without the new files) first
to check the toolchain, exactly as the course does.

## Part B: add the system call (course "Implementation of New System Call")

**Step 1 and 2: the directory and its files.** `new-files/` holds only the six
new files. The eight existing kernel files that have to change are not copied
here: they are changed by the patches in step 3, so there is one source of
truth for each change. Copy the new files in:

```bash
cp -r ~/net-quota-syscall/kernel-code/new-files/. /usr/src/linux-7.2.12/
ls net_quota/                   # Makefile  net_quota.c  net_quota_syscall.h
```

**Step 3: modify the existing files.** Either apply the patches:

```bash
cd /usr/src/linux-7.2.12
for p in ~/net-quota-syscall/kernel-code/patches/*.patch; do patch -p1 < "$p" || break; done
```

or make the edits by hand, one patch file per concern (each `+` line in a patch
is a line to add). The files touched are `Kbuild`,
`arch/x86/entry/syscalls/syscall_64.tbl`, `include/asm-generic/syscalls.h`,
`include/linux/syscalls.h` (the four of the course) plus
`include/linux/sched/signal.h`, `kernel/fork.c`, `kernel/exit.c` and
`net/socket.c`.

## Part C: recompile (course "Recompilation of Linux Kernel")

```bash
# Start from the running kernel's .config (docs/setup-and-build.md, step A5,
# explains the two certificate settings to clear), or, for a fresh start as in
# the course, run: make defconfig
# The fragment below gives the build its own name, 7.2.12-netquota, so it
# cannot overwrite a plain 7.2.12 you built earlier
scripts/kconfig/merge_config.sh -m .config \
    ~/net-quota-syscall/kernel-code/config/net_quota.config
make olddefconfig

make -j"$(nproc)"
make modules_install && make install
```

`make install` installs the kernel image and runs `update-initramfs` and
`update-grub` (the course's `make modules_install install` does the same). Reboot, open the **Advanced
options for Ubuntu** entry in GRUB and choose the new kernel. Keep the
original kernel entry for fallback.

## Part D: check, then the user programs

```bash
uname -r                                  # 7.2.12-netquota
sudo grep sys_net_quota /proc/kallsyms    # __x64_sys_net_quota
```

Then build `user-code/` (course "Implementation of User Space Programs"):
`./build/nq_demo` shows the direct `syscall(472, ...)` call and the wrapper
side by side, like the course's `add2Num`.

## Design notes

- The quota lives in `signal_struct`, so every thread of a process shares it.
- Hooks are in `sock_sendmsg_nosec()` and `sock_recvmsg_nosec()`, the lowest
  common send/receive paths in 7.2.12, plus `sock_splice_read()` for
  receive-side splice. A process with no quota pays one load and one branch.
- The descendant check walks *up* through `real_parent` from the target until
  it meets the caller. Same answer as walking the children subtree, in O(depth).
- All decisions and limitations are in `user-code/docs/design.md`; open gaps
  are in `docs/gap/README.md`.
