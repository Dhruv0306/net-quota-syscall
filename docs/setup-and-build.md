# Set up, build and boot the net_quota kernel

Every command from the kernel source to a booted kernel with the new system
call, in the order they are run. It follows the course reading material
(untar, configure, `make`, `make modules_install`, `make install`, reboot),
for Linux 7.2.10 instead of 5.19.8.

Assumptions: Ubuntu 24.04 on x86_64, and an original kernel that stays in the
GRUB menu as a fallback. A bad kernel can fail to boot, so do not remove the
original kernel. The course advises trying this in a virtual machine first.

The work has two parts, as in the course:

| Part | What it does | Result after the reboot |
|---|---|---|
| A | builds and boots a plain 7.2.10 kernel, with no changes | `uname -r` prints `7.2.10` |
| B | adds the net_quota code, rebuilds and boots again | `uname -r` prints `7.2.10-netquota` |

Part A shows that the build tools, the install and GRUB work before any of our
code is involved, and it leaves a plain 7.2.10 kernel as a second fallback. If
you already built and booted a plain 7.2.10 kernel, skip to Part B.

---

## Part A. Build and boot a plain Linux 7.2.10 kernel

### A1. Download the kernel and open a root shell

- Download the 7.2.10 tarball (`linux-7.2.10.tar.xz`) from
  [kernel.org](https://www.kernel.org).
- Open a terminal and become the root (privileged) user:

```bash
sudo su        # if this does not work, try: su -
```

- When prompted, type the password.

### A2. Move the tarball to `/usr/src`

```bash
cd /usr/src

# Find the tarball. Under "sudo su", ~ may mean /root and not your own home folder,
# so use the full path of your Downloads folder if this finds nothing.
ls /home/<your current user name>/Downloads/linux-7.2.10.tar.xz

mv /home/<your current user name>/Downloads/linux-7.2.10.tar.xz .          # if it is in Downloads
mv /path/to/linux-7.2.10.tar.xz .             # if it is somewhere else

# Verify
ls -ld linux-7.2.10.tar.xz
```

### A3. Extract the source

```bash
tar -xvf linux-7.2.10.tar.xz
cd linux-7.2.10
```

### A4. Install the build dependencies

```bash
# Update the apt listings
apt update

# Install the build dependencies
apt install -y build-essential flex bison libssl-dev libelf-dev libncurses-dev bc dwarves pkg-config libdw-dev rsync kmod ccache libcap-dev debhelper dpkg-dev binutils gawk pahole cpio git
```

### A5. Start from the configuration of the running kernel

```bash
uname -r                           # the running kernel's version
cp /boot/config-$(uname -r) .config

# Ubuntu's configuration names a certificate file that is not in the upstream
# source and stops the build. Clear the two settings:
scripts/config --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS ""

make olddefconfig                  # fill in the settings the new kernel has and the old one lacks
```

### A6. Compile

```bash
make -j$(nproc)
```

A good run ends with `Kernel: arch/x86/boot/bzImage is ready`.

### A7. Install the modules and the kernel

```bash
ls -lh arch/x86/boot/bzImage            # verify the kernel image

make modules_install                    # install the kernel modules
ls -ld /lib/modules/7.2.10              # verify

make install                            # install the kernel

# Verify
ls -lh /boot/vmlinuz-7.2.10
ls -lh /boot/System.map-7.2.10
```

### A8. Update GRUB

```bash
update-grub

grep -E 'menuentry.*7.2.10' /boot/grub/grub.cfg      # verify
```

### A9. Reboot and check

Reboot the machine. At the GRUB menu choose **Advanced options for Ubuntu**,
then the 7.2.10 kernel. Once it has started:

```bash
uname -r                                # should print 7.2.10
```

---

## Part B. Add net_quota and boot the new kernel

### B1. Set two variables and get this repo

Still in a root shell (`sudo su`):

```bash
KSRC=/usr/src/linux-7.2.10            # the kernel source from Part A
NQ=/path/to/net-quota-syscall         # absolute path of this repo
```

`NQ` must be an absolute path, because the commands below change directory.

```bash
git clone https://github.com/Dhruv0306/net-quota-syscall.git "$NQ"   # skip if you already have it
head -5 $KSRC/Makefile                # VERSION = 7, PATCHLEVEL = 2, SUBLEVEL = 10
```

### B2. Copy the new files and apply the patches

`new-files/` holds the six new files, and the six patches make every change
to existing kernel files.

```bash
cd $KSRC
cp -r $NQ/kernel-code/new-files/. .
ls net_quota/                         # Makefile  net_quota.c  net_quota_syscall.h
```

Try the patches first without changing anything, then apply them:

```bash
for p in $NQ/kernel-code/patches/*.patch; do echo "== $p"; patch -p1 --dry-run < "$p" || break; done
for p in $NQ/kernel-code/patches/*.patch; do patch -p1 < "$p" || break; done
```

If the kernel source is a git repository, `git am $NQ/kernel-code/patches/*.patch`
does the same. Check the result:

```bash
grep -n net_quota Kbuild arch/x86/entry/syscalls/syscall_64.tbl \
  include/linux/syscalls.h include/asm-generic/syscalls.h
find . \( -name '*.orig' -o -name '*.rej' \)      # prints nothing
```

You should see `obj-y += net_quota/` in `Kbuild`, the line
`472  common  net_quota  sys_net_quota` in the table, and one declaration in
each of the two `syscalls.h` files. Any `.rej` file means a patch did not fit.

### B3. Configure

The `.config` from Part A is already in `$KSRC`: keep it. (For a fresh start,
which is what the course uses, run `make defconfig` first.) Add the net_quota
fragment:

```bash
cd $KSRC
scripts/kconfig/merge_config.sh -m .config $NQ/kernel-code/config/net_quota.config
make olddefconfig
grep -E '^CONFIG_(NET|INET|LOCALVERSION)=' .config
```

Expected: `CONFIG_NET=y`, `CONFIG_INET=y` and `CONFIG_LOCALVERSION="-netquota"`.
The fragment gives the build its own name, so it cannot overwrite the plain
7.2.10 kernel from Part A.

Optional while developing: `config/net_quota_debug.config` turns on lockdep and
sleeping-in-atomic checks. Merge it the same way, then run `make olddefconfig`.

### B4. Build

```bash
cd $KSRC
make -j"$(nproc)" 2>&1 | tee ~/build.log
```

It is a long build, because the change to `signal.h` makes most of the kernel
recompile. A good run ends with `Kernel: arch/x86/boot/bzImage is ready`. If it
stops, find the first error:

```bash
grep -n -m3 -E "error|Error" ~/build.log
```

### B5. Install the modules and the kernel

```bash
make modules_install
make install
```

The course runs `make modules_install && make modules_install install`, which
does the same. Together they:

| Installed | Where |
|---|---|
| Kernel modules | `/lib/modules/7.2.10-netquota/` |
| Kernel image | `/boot/vmlinuz-7.2.10-netquota` |
| Symbol map and config | `/boot/System.map-7.2.10-netquota`, `/boot/config-7.2.10-netquota` (when the installer copies them) |
| Initial RAM disk | `/boot/initrd.img-7.2.10-netquota` (through `update-initramfs`) |
| GRUB entry | through `update-grub` |

Verify:

```bash
ls -ld /lib/modules/7.2.10-netquota
ls -lh /boot/vmlinuz-7.2.10-netquota
```

Messages from `dkms` about other modules (for example VirtualBox) during the
install are warnings and do not stop the install.

### B6. Update GRUB

`make install` normally runs both steps by itself. Run them again to be sure,
or if the entry is missing:

```bash
update-initramfs -c -k 7.2.10-netquota
update-grub
```

The output of `update-grub` should include
`Found linux image: /boot/vmlinuz-7.2.10-netquota`. To check the menu file:

```bash
ls /boot | grep netquota
grep -E 'menuentry.*7.2.10-netquota' /boot/grub/grub.cfg | head -3
```

### B7. Reboot into the new kernel

```bash
reboot
```

At the GRUB menu choose **Advanced options for Ubuntu**, then
**Ubuntu, with Linux 7.2.10-netquota**. If the menu does not appear, hold
`Shift` while the machine starts (BIOS), or tap `Esc` (UEFI). With Secure Boot
on, an unsigned custom kernel will not start: turn Secure Boot off or sign the
kernel.

### B8. Check that it is the right kernel

```bash
uname -r                                  # 7.2.10-netquota
sudo grep sys_net_quota /proc/kallsyms    # __x64_sys_net_quota
```

Then build and try the user-space side:

```bash
cd $NQ/user-code
make
./build/nq_cli 1 1000 5                   # "Process 1 is not a descendant ..." = the syscall answers
./build/nq_demo                           # direct call returns 0, wrapper call says already set
sudo ./tests/run_tests.sh                 # about a minute; ends with: ALL TESTS PASSED
                                          # (it gives lo a private IPv6 address, fd00:6e71::1, for the IPv6 case and removes it again)
sudo dmesg | grep -E 'BUG:|WARNING:|Oops|lockdep|net_quota'    # prints nothing
```

The test runner also compares the kernel log before and after, and prints
`kernel log: no new warnings or oopses`. Record the result of every run on a
changed kernel in `docs/handoff.md`, section 4.

---

## After you change the kernel code

Edit the files in `$KSRC`, then only the changed parts are rebuilt:

```bash
cd $KSRC
make -j"$(nproc)" && make modules_install && make install && update-grub && reboot
```

A change to `signal.h` rebuilds almost everything; a change to
`net_quota/net_quota.c` rebuilds a few files. Copy the finished files back into
`kernel-code/new-files/`, and regenerate the patches for changed existing files
with `git format-patch`, so the repo stays the source of truth.

## Update an existing build to the current `main`

If `$KSRC` already has an older version of net_quota, do not start over. Find
out which patches are in the tree, then copy the new files and apply only the
missing patches.

```bash
cd $KSRC
grep -c net_quota include/linux/sched/signal.h        # 0001: expect 2 or more
grep -n  net_quota kernel/fork.c                      # 0002, see below
grep -c net_quota kernel/exit.c                       # 0003: expect 2 or more
grep -c net_quota net/socket.c                        # 0004: expect 1 or more
grep -c net_quota Kbuild                              # 0005: expect 1
grep -c net_quota net/ipv4/tcp.c                      # 0006: expect 2 (0 = not applied)
```

`kernel/fork.c` tells whether 0002 is on, and whether it is the current one.
Earlier versions of the series had three patches for this file (an inheriting
0002, 0006 and 0007); they are now one patch, 0002, and a tree built from the
old series already has the same final file.

| `grep -n net_quota kernel/fork.c` shows | State | What to do |
|---|---|---|
| nothing | 0002 is not on | apply 0002 |
| the `#include` and `net_quota_init` only | current | nothing |
| the `#include`, `net_quota_init` and `net_quota_inherit` | old 0002 only | delete the `net_quota_inherit(...)` line: `sed -i '/net_quota_inherit/d' kernel/fork.c` |
| the above plus a `net_quota_exit` line | old 0002 and old 0006 | delete the `net_quota_inherit(...)` line as above, then in `bad_fork_cleanup_signal:` put back the original three lines (see below) |

The original `bad_fork_cleanup_signal:` block, to put back in the last case
(remove the braces, the comment and the `net_quota_exit()` call):

```c
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
```

Then:

```bash
cd $KSRC
cp -r $NQ/kernel-code/new-files/. .                   # always: the logic is in these files
patch -p1 --dry-run < $NQ/kernel-code/patches/<missing patch>   # then without --dry-run
make -j"$(nproc)" && make modules_install && make install && update-grub && reboot
```

The other patches (0001, 0003, 0004, 0005, 0006) each edit files no other patch
touches, so each one can be checked and applied on its own. These changes touch
`net_quota.c` and `fork.c`, so only a few files rebuild.

## Go back to another kernel

Restart, choose **Advanced options for Ubuntu** and pick the kernel you want:
the original one, or the plain 7.2.10 from Part A. That is all that is needed
to stop using net_quota.

To delete the installed net_quota kernel and its GRUB entry, follow
[removing-custom-kernel.md](removing-custom-kernel.md): it lists what to check
first and what to do if the machine does not boot afterwards. To take the
system call out of the kernel *source* and build a kernel without it, follow
[removing-net-quota.md](removing-net-quota.md).

## When something goes wrong

| What you see | Cause and fix |
|---|---|
| `./build/nq_cli` says "not available: boot the net-quota kernel" | the syscall returned `ENOSYS`: you booted another kernel. Check `uname -r`, restart and pick the netquota entry |
| `patch` prints `Hunk FAILED` or leaves `.rej` files | the source is not exactly 7.2.10, or a patch was applied twice. Extract a fresh copy of the source and repeat B2 |
| `make` stops with `Killed` | out of memory. Run `make -j2` (it resumes where it stopped) |
| `No rule to make target 'debian/canonical-certs.pem'` | an Ubuntu `.config` was copied in and A5 was skipped. Run `scripts/config --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS ""`, then `make olddefconfig` |
| `make` asks for `pahole` or BTF errors | `apt install dwarves`, or `scripts/config --disable DEBUG_INFO_BTF` and `make olddefconfig` |
| The new kernel is not in the GRUB menu | run `update-grub` and look for the `Found linux image` line for `7.2.10-netquota` |
| The new kernel does not start | restart and pick another kernel, then check Secure Boot and `~/build.log` |
| Tests fail with `connection refused` | the test server could not start. Run `ip -4 addr` and pass an address: `sudo SERVER_IP=<address> ./tests/run_tests.sh` |

For how the system call works once it is running, see
[how-it-works.md](how-it-works.md).
