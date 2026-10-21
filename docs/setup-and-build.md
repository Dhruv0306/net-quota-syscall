# Set up, build and boot the net_quota kernel

Every command from the kernel source to a booted kernel with the new system
call, in the order they are run. It follows the course reading material
(untar, configure, `make`, `make modules_install`, `make install`, reboot),
for Linux 7.2.10 instead of 5.19.8.

```mermaid
flowchart LR
  A["1. Get the kernel source<br/>and this repo"] --> B["2. Copy the new files<br/>apply the 5 patches"]
  B --> C["3. Configure<br/>merge net_quota.config"]
  C --> D["4. Build<br/>make"]
  D --> E["5. Install<br/>modules and kernel"]
  E --> F["6. Update GRUB"]
  F --> G["7. Reboot into<br/>7.2.10-netquota"]
  G --> H["8. Check and run<br/>the tests"]
```

Assumptions: Ubuntu 24.04 on x86_64, the kernel 7.2.10 tarball from
[kernel.org](https://www.kernel.org), and an original kernel that stays in the
GRUB menu as a fallback. The course advises trying this in a virtual machine
first. A bad kernel can fail to boot, so do not remove the original kernel.

## 0. Open a root shell and set two variables

```bash
sudo su
KSRC=/usr/src/linux-7.2.10            # the kernel source
NQ=/path/to/net-quota-syscall         # absolute path of this repo
```

`NQ` must be an absolute path, because the commands below change directory.

## 1. Get the kernel source and this repo

```bash
apt update
apt install -y build-essential flex bison libssl-dev libelf-dev bc git

cd /usr/src
# put linux-7.2.10.tar.xz (from kernel.org) here, then:
tar -xvf linux-7.2.10.tar.xz

git clone https://github.com/Dhruv0306/net-quota-syscall.git "$NQ"   # skip if you already have it
head -5 $KSRC/Makefile                # VERSION = 7, PATCHLEVEL = 2, SUBLEVEL = 10
```

## 2. Copy the new files and apply the patches

`new-files/` holds the six new files, and the five patches make every change
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

## 3. Configure

Keep your existing `.config` if you have built this kernel before. For a fresh
start, which is what the course uses, run `make defconfig` first.

```bash
cd $KSRC
# make defconfig                      # only for a fresh configuration
scripts/kconfig/merge_config.sh -m .config $NQ/kernel-code/config/net_quota.config
make olddefconfig
grep -E '^CONFIG_(NET|INET|LOCALVERSION)=' .config
```

Expected: `CONFIG_NET=y`, `CONFIG_INET=y` and `CONFIG_LOCALVERSION="-netquota"`.
The fragment gives the build its own name, so it cannot overwrite a plain
7.2.10 kernel you installed earlier.

Optional while developing: `config/net_quota_debug.config` turns on lockdep and
sleeping-in-atomic checks. Merge it the same way, then run `make olddefconfig`.

## 4. Build

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

## 5. Install the modules and the kernel

```bash
make modules_install && make modules_install install
```

This is the course's command. The second `modules_install` does nothing new;
`make install` alone would do. Together they:

| Installed | Where |
|---|---|
| Kernel modules | `/lib/modules/7.2.10-netquota/` |
| Kernel image | `/boot/vmlinuz-7.2.10-netquota` |
| Symbol map and config | `/boot/System.map-7.2.10-netquota`, `/boot/config-7.2.10-netquota` (when the installer copies them) |
| Initial RAM disk | `/boot/initrd.img-7.2.10-netquota` (through `update-initramfs`) |
| GRUB entry | through `update-grub` |

Messages from `dkms` about other modules (for example VirtualBox) during the
install are warnings and do not stop the install.

## 6. Update GRUB

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
grep -n "7.2.10-netquota" /boot/grub/grub.cfg | head -3
```

## 7. Reboot into the new kernel

```bash
reboot
```

At the GRUB menu choose **Advanced options for Ubuntu**, then
**Ubuntu, with Linux 7.2.10-netquota**. If the menu does not appear, hold
`Shift` while the machine starts (BIOS), or tap `Esc` (UEFI). With Secure Boot
on, an unsigned custom kernel will not start: turn Secure Boot off or sign the
kernel.

## 8. Check that it is the right kernel

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
sudo ./tests/run_tests.sh                 # ends with: ALL TESTS PASSED
sudo dmesg | grep -E 'BUG:|WARNING:|Oops|lockdep|net_quota'    # prints nothing
```

The test runner also compares the kernel log before and after, and prints
`kernel log: no new warnings or oopses`.

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

## Go back to the original kernel

Restart, choose **Advanced options for Ubuntu** and pick the original kernel.
To remove the net_quota kernel completely:

```bash
rm -f /boot/vmlinuz-7.2.10-netquota /boot/initrd.img-7.2.10-netquota \
      /boot/System.map-7.2.10-netquota /boot/config-7.2.10-netquota
rm -rf /lib/modules/7.2.10-netquota
update-grub
```

## When something goes wrong

| What you see | Cause and fix |
|---|---|
| `./build/nq_cli` says "not available: boot the net-quota kernel" | the syscall returned `ENOSYS`: you booted another kernel. Check `uname -r`, restart and pick the netquota entry |
| `patch` prints `Hunk FAILED` or leaves `.rej` files | the source is not exactly 7.2.10, or a patch was applied twice. Extract a fresh copy of the source and repeat step 2 |
| `make` stops with `Killed` | out of memory. Run `make -j2` (it resumes where it stopped) |
| `No rule to make target 'debian/canonical-certs.pem'` | an Ubuntu `.config` was copied in. Run `scripts/config --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS ""`, then `make olddefconfig` |
| `make` asks for `pahole` or BTF errors | `apt install dwarves`, or `scripts/config --disable DEBUG_INFO_BTF` and `make olddefconfig` |
| The new kernel is not in the GRUB menu | run `update-grub` and look for the `Found linux image` line for `7.2.10-netquota` |
| The new kernel does not start | restart and pick the original kernel, then check Secure Boot and `~/build.log` |
| Tests fail with `connection refused` | the test server could not start. Run `ip -4 addr` and pass an address: `sudo SERVER_IP=<address> ./tests/run_tests.sh` |

For how the system call works once it is running, see
[how-it-works.md](how-it-works.md).
