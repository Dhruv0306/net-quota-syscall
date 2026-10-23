# kernel-code

Kernel side of the `net_quota` system call for Linux **7.2.10** on x86_64,
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
│   ├── 0002  kernel/fork.c                  init and inherit in copy_signal() (inherit removed by 0007)
│   ├── 0003  kernel/exit.c                  clear the quota in do_exit()
│   ├── 0004  net/socket.c                   count sends and receives
│   ├── 0005  Kbuild, syscall_64.tbl, asm-generic/syscalls.h, linux/syscalls.h
│   ├── 0006  kernel/fork.c                  stop the quota timer when fork() fails
│   └── 0007  kernel/fork.c                  a child does not inherit the quota (undoes 0006's hook)
└── config/
    ├── net_quota.config            names the build 7.2.10-netquota
    └── net_quota_debug.config      optional lockdep / atomic-sleep checks
```

The full step-by-step guide, with the GRUB update, the reboot, the checks and
what to do when something fails, is
[docs/setup-and-build.md](../docs/setup-and-build.md). This page is the short
version.

## Status

The seven patches apply cleanly, in order, on a pristine v7.2.10. The code
before the professor's changes was built into a kernel, booted as
`7.2.10-netquota`, and passed every test in `user-code/tests`, with no new
warnings in the kernel log.

Three later changes compile without warnings against the full 7.2.10 tree with
`W=1` but have **not** been booted or run through the tests yet: the timer
starting when the limit is set (in `net_quota.c`), a child not inheriting the
quota (patches 0006 and 0007), and a send larger than what is left being
refused whole (in `net_quota.c`). The receive side of that last change waits
for the professor's wording. Nothing has been run with the debug options in
`config/net_quota_debug.config` (lockdep and friends).

The patches are an append-only history, so apply all seven, in order, before
building. Patch 0002 alone calls `net_quota_inherit()`, which the current
`net_quota.c` no longer has, and 0006 only makes sense with 0002; the tree
builds again once 0007 is on. The current state of `kernel/fork.c` is one
`#include` and one `net_quota_init()` call.

## Following the course steps on 7.2.10

The reading material was written for 5.19.8. Same recipe, three differences:

| Course step (5.19.8) | On 7.2.10 |
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
cd /usr/src/linux-7.2.10        # the tarball from kernel.org, already extracted
```

It is worth doing one plain recompilation (Part C without the new files) first
to check the toolchain, exactly as the course does.

## Part B: add the system call (course "Implementation of New System Call")

**Step 1 and 2: the directory and its files.** `new-files/` holds only the six
new files. The eight existing kernel files that have to change are not copied
here: they are changed by the patches in step 3, so there is one source of
truth for each change. Copy the new files in:

```bash
cp -r ~/net-quota-syscall/kernel-code/new-files/. /usr/src/linux-7.2.10/
ls net_quota/                   # Makefile  net_quota.c  net_quota_syscall.h
```

**Step 3: modify the existing files.** Either apply the patches:

```bash
cd /usr/src/linux-7.2.10
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
# The fragment below gives the build its own name, 7.2.10-netquota, so it
# cannot overwrite a plain 7.2.10 you built earlier
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
uname -r                                  # 7.2.10-netquota
sudo grep sys_net_quota /proc/kallsyms    # __x64_sys_net_quota
```

Then build `user-code/` (course "Implementation of User Space Programs"):
`./build/nq_demo` shows the direct `syscall(472, ...)` call and the wrapper
side by side, like the course's `add2Num`.

## Design notes

- The quota lives in `signal_struct`, so every thread of a process shares it.
- Hooks are in `sock_sendmsg_nosec()` and `sock_recvmsg_nosec()`, the lowest
  common send/receive paths in 7.2.10, plus `sock_splice_read()` for
  receive-side splice. A process with no quota pays one load and one branch.
- The descendant check walks *up* through `real_parent` from the target until
  it meets the caller. Same answer as walking the children subtree, in O(depth).
- All decisions and limitations are in `user-code/docs/design.md`; open gaps
  are in `docs/gap/README.md`.
