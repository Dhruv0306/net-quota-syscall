# kernel-code

Kernel side of the `net_quota` system call, for Linux **7.2.10** on x86_64
(tested against the sources only: see "Status" below).

```
kernel-code/
├── new-files/        new files; paths mirror the kernel source tree
│   ├── include/linux/net_quota.h        quota struct, lifecycle API
│   ├── include/net/net_quota.h          socket hooks (inline fast path)
│   ├── include/uapi/linux/net_quota.h   NQ_* return codes shared with user space
│   └── kernel/net_quota.c               syscall, descendant check, timer, hooks
├── patches/          edits to existing kernel files, one concern per patch
│   ├── 0001  include/linux/sched/signal.h   add the field to signal_struct
│   ├── 0002  kernel/fork.c                  init and inherit in copy_signal()
│   ├── 0003  kernel/exit.c                  clear the quota in do_exit()
│   ├── 0004  net/socket.c                   charge sends/receives
│   └── 0005  syscall table, syscalls.h, kernel/Makefile
└── config/
    ├── net_quota.config         tags the build as 7.2.10-netquota
    └── net_quota_debug.config   optional lockdep/atomic-sleep checks
```

## Status

The five patches apply cleanly, in order, on a pristine v7.2.10 tree
(`git am`), and every API they use was checked against the 7.2.10 headers.
The code has **not been compiled or booted yet**: the first build is the first
real test, so expect to fix a typo or two.

## Apply

```bash
git clone --depth 1 --branch v7.2.10 https://github.com/gregkh/linux.git
cd linux
git switch -c net-quota
cp -r ../net-quota-syscall/kernel-code/new-files/. .
git add -A && git commit -s -m "net_quota: add new files"
git am -s ../net-quota-syscall/kernel-code/patches/*.patch
```

`-s` adds your own Signed-off-by line, which the patches do not carry.

## Build and install (Ubuntu 24.04)

```bash
sudo apt install build-essential libncurses-dev bison flex libssl-dev \
                 libelf-dev dwarves bc rsync zstd

cp /boot/config-"$(uname -r)" .config
make olddefconfig
scripts/config --set-str SYSTEM_TRUSTED_KEYS "" \
               --set-str SYSTEM_REVOCATION_KEYS "" \
               --disable DEBUG_INFO_BTF
scripts/kconfig/merge_config.sh -m .config \
    ../net-quota-syscall/kernel-code/config/net_quota.config
# optional while developing:
# scripts/kconfig/merge_config.sh -m .config \
#     ../net-quota-syscall/kernel-code/config/net_quota_debug.config
make olddefconfig

make -j"$(nproc)"
sudo make modules_install install
sudo update-grub
```

Keep your original kernel in the boot menu and pick `7.2.10-netquota` after
rebooting. If Secure Boot is on, an unsigned kernel will not start: turn it
off or sign the image.

## Check that the syscall is there

```bash
uname -r                                  # 7.2.10-netquota
sudo grep sys_net_quota /proc/kallsyms    # __x64_sys_net_quota
```

Then build `user-code/` and run `./build/nq_cli 1 1000 1`. An answer of
"not a descendant" means the syscall exists; "function not implemented" means
you booted the wrong kernel.

## Design notes

- The quota lives in `signal_struct`, so every thread of a process shares it.
- Hooks are in `sock_sendmsg_nosec()` and `sock_recvmsg_nosec()`, the lowest
  common send/receive paths in 7.2.10, plus `sock_splice_read()` for
  receive-side splice. A process with no quota pays one load and one branch.
- The "is it a descendant" check walks *up* through `real_parent` from the
  target until it meets the caller, instead of walking the children subtree
  downwards. The answer is the same, it costs O(depth), needs no allocation
  and handles multi-threaded callers.
- All limitations and decisions are listed in `user-code/docs/design.md`.
