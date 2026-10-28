# Removing the net_quota system call from the kernel

How to take `net_quota` (syscall 472) back out of a Linux 7.2.10 tree and end
up with a kernel that does not have it. Written for the setup in
`handoff.md` section 5: the source is the kernel.org tarball at
`/usr/src/linux-7.2.10`, **not a git repository**, patched with `patch -p1`.

The source-tree steps (part B, steps 2 and 3) were tested on a pristine 7.2.10
tree: all six new files copied in, patches 0001 to 0006 applied, then removed
as described below. (The reverse step was repeated with the six-patch series
on the nine patched files: all came back identical to the original.) The result was byte-for-byte identical to the original
tree (`diff -r` reported no difference). Rebuilding and booting the result was
not tested.

## Pick the lightest option that is enough

| Goal | Do this |
|---|---|
| Stop using the syscall, keep the machine as it is | **Part A**: boot the original Ubuntu kernel. Nothing to remove. |
| Get a kernel build without the syscall | **Part B**: remove it from the source tree and rebuild. |
| Also free the disk space and the GRUB entry | **Part B**, then **Part C**. |

## Part A: just boot a kernel that does not have it

The syscall exists only in the `7.2.10-netquota` kernel. Pick the original
Ubuntu entry in GRUB (under "Advanced options") and it is gone, because that
kernel never had it. Check with the command in step 7 below; it should say
"Function not implemented".

## Part B: remove it from the source tree

Run as root on the Ubuntu machine. Keep the project repo at
`/path/to/net-quota-syscall`.

### 1. Check the tree is as expected

```bash
cd /usr/src/linux-7.2.10
grep -rn "net_quota" Kbuild arch/x86/entry/syscalls/syscall_64.tbl \
    include/linux/syscalls.h include/asm-generic/syscalls.h \
    include/linux/sched/signal.h kernel/fork.c kernel/exit.c net/socket.c \
    net/ipv4/tcp.c
ls net_quota
```

You should see hits in all of those files and a `net_quota/` directory. If
some files show nothing, the tree has only part of the series applied. Still
follow step 2: a reverse that does not match is reported, and step 4 lists the
manual fallback.

### 2. Undo the patches

Every patch edits different files, so the order does not matter for
correctness. Going newest first is still a good habit, and it is what the loop
does.

```bash
cd /usr/src/linux-7.2.10
P=/path/to/net-quota-syscall/kernel-code/patches

for p in $(ls -r $P/000*.patch); do
    echo "== $p"
    patch -R -p1 --dry-run < "$p" || { echo "STOP: $p does not reverse cleanly"; break; }
    patch -R -p1 < "$p"
done
```

This takes 0006, 0005, 0004, 0003, 0002 and 0001 off, in that order. The
dry run before each real run means a patch that does not match stops the loop
before it changes anything.

### 3. Delete the six new files

```bash
cd /usr/src/linux-7.2.10
rm -rf net_quota
rm -f include/linux/net_quota.h include/net/net_quota.h include/uapi/linux/net_quota.h
```

Do this **after** step 2, not before. Removing the files while the patches are
still in place breaks the build, because `Kbuild`, `syscalls.h` and the socket
hooks still refer to them.

### 4. Check that nothing is left

```bash
cd /usr/src/linux-7.2.10
grep -rIn "net_quota\|NET_QUOTA" --include='*.c' --include='*.h' \
    --include='Kbuild' --include='*.tbl' --include='Makefile' . | head
find . -name '*.orig' -o -name '*.rej'
```

Both commands should print nothing. If something is left (a patch did not
reverse, for example because the tree had been edited by hand), remove it by
hand. The patches touch these places:

| Patch | File | What to remove |
|---|---|---|
| 0001 | `include/linux/sched/signal.h` | the `net_quota` member of `signal_struct` and its `#include` |
| 0002 | `kernel/fork.c` | the `net_quota_init()` call in `copy_signal()` and its `#include` |
| 0003 | `kernel/exit.c` | the `net_quota_exit()` call in `do_exit()` and its `#include` |
| 0004 | `net/socket.c` | the quota hooks in `sock_sendmsg_nosec()`, `sock_recvmsg_nosec()`, `sock_splice_read()` and the `#include` |
| 0005 | `Kbuild` | the `obj-y += net_quota/` line |
| 0005 | `arch/x86/entry/syscalls/syscall_64.tbl` | the line for syscall `472` |
| 0005 | `include/linux/syscalls.h`, `include/asm-generic/syscalls.h` | the `sys_net_quota` declarations |
| 0006 | `net/ipv4/tcp.c` | the `#include <net/net_quota.h>` and the `if (net_quota_active()) return -EOPNOTSUPP;` check at the top of the `TCP_ZEROCOPY_RECEIVE` case |

### 5. Take the version tag off the build name

The project's config fragment sets `CONFIG_LOCALVERSION="-netquota"`, so the
rebuilt kernel would still call itself `7.2.10-netquota` although the syscall
is gone. That is confusing and would overwrite the old files in `/boot`. Clear
it:

```bash
cd /usr/src/linux-7.2.10
scripts/config --set-str LOCALVERSION ""
make olddefconfig
```

If you would rather keep the old name for now, skip this step. Then the new
kernel replaces the old one under the same name.

### 6. Rebuild and install

```bash
cd /usr/src/linux-7.2.10
make -j"$(nproc)" && make modules_install && make install && update-grub && reboot
```

Because `syscall_64.tbl` and several headers changed, `make` rebuilds a large
part of the tree, so expect a full-length build.

### 7. Confirm the syscall is gone

After the reboot, pick the new entry in GRUB and run:

```bash
uname -r
python3 -c 'import ctypes,os; l=ctypes.CDLL(None,use_errno=True); r=l.syscall(472,1,1,1); print(r, os.strerror(ctypes.get_errno()))'
```

The second command should print `-1 Function not implemented`. On the
net_quota kernel the same call prints a different error (for example
"Operation not permitted" or "No such process", depending on the arguments),
which is how you can tell the two kernels apart.

## Part C: remove the installed net_quota kernel

Only do this once you have booted another kernel that works. Never remove the
kernel that is running (`uname -r`).

```bash
update-initramfs -d -k 7.2.10-netquota
rm -f  /boot/vmlinuz-7.2.10-netquota /boot/System.map-7.2.10-netquota \
       /boot/config-7.2.10-netquota
rm -rf /lib/modules/7.2.10-netquota
update-grub
```

A fuller version of this part, with the checks and the fallbacks, is in
`removing-custom-kernel.md`.

Then check that the GRUB menu no longer lists `7.2.10-netquota`. If step 5 was
skipped and the rebuilt kernel has the same name, do not run Part C for that
name, or you will delete the kernel you just built.

## The project's user-space side

The tests and tools in `user-code/` only do something on the net_quota kernel.
On any other kernel they fail with "Function not implemented". Nothing in the
kernel needs them. To tidy up, run `make clean` in `user-code/`. If you copied
the built binaries anywhere else, remove those copies as well.

## Things that go wrong

- **Do not delete only the new files.** The patched kernel files still call
  them, so the build stops with undefined references or missing headers.
- **A tree from the old seven-patch series** (before 2026-10-24) has exactly the
  same files as one from the six patches, so the loop above works on it too.
- **Windows line endings** break `patch`, for reverse as much as for forward
  runs. Use `git clone` or `git pull` on the Ubuntu machine for the patch
  files.
- **`patch` reports "Reversed (or previously applied) patch detected"** during
  the dry run: that patch is not in the tree, so skip it and go on.
- **A fresh tree is the cleanest way.** Extract a new 7.2.10 tarball, copy in
  the running kernel's `.config` (with the `SYSTEM_TRUSTED_KEYS` fix from
  `setup-and-build.md`) and build. That tree never had net_quota in it, and
  steps 1 to 4 are not needed.
