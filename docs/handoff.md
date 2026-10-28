# Handoff: net_quota system call

Written 2026-10-23, after the branch `Dhruv_Patel/no-fork-inheritance` was merged
into `main`, and updated the same day after `Dhruv_Patel/drop-oversized-send`
(the third professor change, sends only) was merged. It tells a newcomer what exists, what has been proven and what has
not, why things are the way they are, and what to do next. Read it first; the
other pages go deeper:

| Page | What it covers |
|---|---|
| [how-it-works.md](how-it-works.md) | how data flows through user space and the kernel, with diagrams |
| [setup-and-build.md](setup-and-build.md) | every command from kernel source to a booted kernel |
| [../user-code/docs/design.md](../user-code/docs/design.md) | behaviour rules, decision log, implementation notes, known limitations |
| [gap/README.md](gap/README.md) | register of open gaps, and the pending part of the third professor change |
| [removing-net-quota.md](removing-net-quota.md) | taking the system call out of the kernel source and rebuilding |
| [removing-custom-kernel.md](removing-custom-kernel.md) | removing the installed custom kernel and its GRUB entry |

## 1. What this is

A course project for Advanced Operating Systems (CS G623, BITS Pilani K K Birla
Goa Campus). The assignment, in short: add a system call that takes a pid, a
data size and a time period, and limits how much network data that process may
use for that time. It only works if the pid is a descendant of the caller
(child, grandchild, ...). When the limit is reached the kernel stops further
network use, and when the period ends the limit is lifted by itself.

The result is a new system call, `net_quota(pid, data_size, period_sec)`,
number **472** on x86_64, in a custom **Linux 7.2.10** kernel on **Ubuntu 24.04**,
plus a user-space wrapper, command-line tools and tests.

Repository: `github.com/Dhruv0306/net-quota-syscall`.

## 2. Current behaviour

This is what the code does on `main` now. Where a rule came from the professor
rather than from the original assignment text, it says so.

| Topic | Behaviour |
|---|---|
| Who can be restricted | a process (not a single thread) that is a child, grandchild, ... of the caller; never the caller itself; no special privilege needed |
| What is counted | payload bytes, sent and received, on one combined counter; only IPv4/IPv6 sockets whose peer is not a loopback address (`127.x`, `::1`); Unix sockets and `MSG_PEEK` are not counted |
| Which calls | everything that goes through the socket layer: send, sendto, sendmsg, sendmmsg, write, recv, read, sendfile, splice, io_uring socket operations. `TCP_ZEROCOPY_RECEIVE` bypasses that layer, so it fails with `EOPNOTSUPP` for a process under a quota (patch 0006) |
| Limit reached | send and receive fail with `EDQUOT` until the period ends |
| Oversized request | **a send larger than what is left is refused whole with `EDQUOT`, on TCP and UDP alike; nothing is sent and nothing is charged** (professor's third change). A receive larger than what is left is still shortened to it: the receive rule is waiting for the professor's wording |
| Timer | **starts when the limit is set** (professor's change; before, it started at the first network use) and revokes the quota when the period ends. Nothing is renewed automatically; an ancestor can set a new quota afterwards. A quota that is never used expires too |
| Existing quota | never overwritten: the call fails with `EEXIST` and changes nothing |
| fork() | **a child does not inherit the quota** and starts unrestricted (professor's change; before, the child got its own copy). Threads share the parent's quota |
| Exit | the quota and its timer are removed when the last thread exits; a reused pid starts without a limit |
| Errors | the kernel returns a number; the message text is built in user space (`nq_strerror()`), so the codes and texts are `#define`d once |

Error codes (`kernel-code/new-files/include/uapi/linux/net_quota.h`):

| Kernel returns | Constant | Meaning |
|---|---|---|
| `-EINVAL` | `NQ_E_BAD_ARGS` | bad pid, size or period, or a thread id |
| `-EPERM` | `NQ_E_SELF_TARGET` | the caller tried to restrict itself |
| `-ESRCH` | `NQ_E_NOT_DESCENDANT` | pid missing, not a descendant, or exiting |
| `-EEXIST` | `NQ_E_ALREADY_SET` | a quota is already set |
| `-EDQUOT` | `NQ_E_QUOTA_EXCEEDED` | returned by send/recv at the limit |

## 3. Repository layout

```
net-quota-syscall/
├── kernel-code/
│   ├── new-files/          the six new kernel files (copied onto the kernel source)
│   │   ├── net_quota/        net_quota.c (the system call and all logic), net_quota_syscall.h, Makefile
│   │   └── include/          linux/net_quota.h, net/net_quota.h, uapi/linux/net_quota.h
│   ├── patches/            0001 to 0006: every edit to an existing kernel file
│   └── config/             net_quota.config (names the build 7.2.10-netquota), net_quota_debug.config
├── user-code/
│   ├── src/                wrapper library, nq_cli, nq_run, nq_demo
│   ├── include/            wrapper API, message texts, macro wrapper
│   ├── tests/              the test programs and the runner
│   └── docs/design.md
└── docs/                   this page, how-it-works.md, setup-and-build.md,
    │                       removing-net-quota.md, removing-custom-kernel.md
    └── gap/                open gaps and the pending professor change
```

The six patches:

| Patch | File | Change |
|---|---|---|
| 0001 | `include/linux/sched/signal.h` | the quota lives in `signal_struct`, so all threads of a process share it |
| 0002 | `kernel/fork.c` | initialise the quota in `copy_signal()`; a child starts with no quota |
| 0003 | `kernel/exit.c` | stop the timer and clear the quota in `do_exit()` |
| 0004 | `net/socket.c` | hooks in `sock_sendmsg_nosec()`, `sock_recvmsg_nosec()`, `sock_splice_read()` |
| 0005 | `Kbuild`, `syscall_64.tbl`, `syscalls.h`, `asm-generic/syscalls.h` | build `net_quota/` and register syscall 472 |
| 0006 | `net/ipv4/tcp.c` | `TCP_ZEROCOPY_RECEIVE` fails with `EOPNOTSUPP` for a process under a quota (it bypassed the counting hooks) |

The series used to have seven patches: three of them (0002, 0006, 0007) edited
`kernel/fork.c` one on top of the other, so a dry run of the whole series on a
fresh tree failed on 0007. On 2026-10-24 they were folded into the one 0002
above. The resulting `fork.c` is byte for byte what the three produced, so a
tree built from the old series needs nothing. Every patch now edits different
files and can be checked and applied alone; each patch's dry run works on a
fresh tree. The two later changes (timer at set time, and refusing an
oversized send) are in `net_quota.c` only, so they need no patch.

All six patch headers have Dhruv as author and Claude as co-author, as section 5
requires.

## 4. Status: what is proven and what is not

Third run on the test machine on 2026-10-28, with a kernel built from patches
0001 to 0006 and the new files: `./tests/run_tests.sh | tee test_run.log`, as
root, ended with `ALL TESTS PASSED` and "kernel log: no new warnings or
oopses". (Earlier runs: 2026-10-27, first run with a test bug in
`test_realprog.sh` and the finding that `TCP_ZEROCOPY_RECEIVE` was not counted;
second run all passed.) `test_run.log` is ignored by git (`*.log`).

The tested kernel had every option of `config/net_quota_debug.config` on
(`PROVE_LOCKING`, `DEBUG_ATOMIC_SLEEP`, `DEBUG_SPINLOCK`, `DEBUG_OBJECTS`,
`DEBUG_OBJECTS_TIMERS`), so lockdep, atomic-sleep, spinlock and timer-object
checks ran during all tests. `dmesg | grep -E 'BUG:|WARNING:|Oops|lockdep|net_quota'`
after the 2026-10-27 runs showed only the boot line "RCU lockdep checking is
enabled".

| Change | Compiled | Run on the real kernel |
|---|---|---|
| Everything up to the professor's changes | yes | yes: passed |
| Timer starts when the limit is set (`net_quota.c`) | yes, `W=1`, no warnings | **yes: `test_timer` passed** |
| A child does not inherit the quota (patch 0002) | yes | **yes: `test_fork` passed** |
| A send larger than what is left is refused whole (`net_quota.c`) | yes | **yes: `test_basic`, `test_udp`, `test_threads` passed** (threads: 19600 of 20000, every thread ended with `EDQUOT`) |
| `sendmsg`/`recvmsg`, `sendmmsg`/`recvmmsg`, `sendfile` (also oversized), `splice` both ways, `MSG_PEEK`, non-blocking `recv`, zero-length send, IPv6 | yes | **yes: `test_calls` passed**, each path counted exactly. IPv6 ran on a private address (`fd00:6e71::1`) that the runner gives the loopback interface |
| io_uring send, receive, zero-copy send, oversized send | yes | **yes: `test_uring` passed**; the oversized send came back as `-122` (`EDQUOT`) |
| `TCP_ZEROCOPY_RECEIVE` (patch 0006) | yes, `W=1`, no warnings (7.2.12) | **yes: `test_zerocopy` passed**: refused with `EOPNOTSUPP`, the 32768 bytes stayed queued, and a normal `recv()` took them and was counted (4464 of 70000 left). On the first run, before the patch, the data was read without being charged |
| A real program stopped at the limit (`test_realprog.sh`) | yes | **yes:** dd stopped at exactly 20000 bytes with `Disk quota exceeded`, exit status 1; dd run as a child of the restricted shell was not limited, as designed |
| A receive larger than what is left | not changed | waiting for the professor's wording |

Nothing from the test plan is left to run. Still without a test: io_uring
multishot receive with provided buffers, and io_uring zero-copy receive (needs a
network card with header/data split, cannot run on a veth pair). What is open
is waiting on the professor (section 8, items 1 and 7).

## 5. The machine and the workflow

| Item | Value |
|---|---|
| Test machine | Ubuntu 24.04, x86_64, a real machine (not a VM) |
| Kernel source | the 7.2.10 tarball from kernel.org, extracted at `/usr/src/linux-7.2.10` (on 2026-10-24 the tree in use was `/usr/src/linux-7.2.12`; the patches dry-run clean, apply and compile with `W=1` without warnings on both, and the build name follows the version: `7.2.12-netquota`); **not a git repository**, so patches are applied with `patch -p1` |
| Configuration | the running kernel's `.config` plus `net_quota.config`, giving the name `7.2.10-netquota` |
| GRUB menu | the original Ubuntu kernel and `7.2.10-netquota`, and a plain 7.2.10 if one was built earlier |
| Git work | on Windows, `D:\github\net-quota-syscall`; the kernel build is on the Ubuntu machine |

Conventions:

- **Branches:** `Dhruv_Patel/<feature or fix name>`; keep merged branches.
- **Authorship:** commits and patches have Dhruv Rakeshbhai Patel as author and
  `Co-authored-by: Claude <noreply@anthropic.com>` as a trailer, never Claude
  as author.
- **Delivery:** changes are exchanged as `git format-patch` files and applied
  with `git am`. `.gitignore` ignores `*.patch` except in
  `kernel-code/patches/`, so a new patch there needs `git add -f`.
- **History:** a few early commits on `main` list Claude as author. They were
  left alone, because changing them means rewriting history on `main`.

## 6. How to change and test

After editing kernel code (on the Ubuntu machine, as root):

```bash
cd /usr/src/linux-7.2.10
cp -r /path/to/net-quota-syscall/kernel-code/new-files/. .         # new and changed new files
patch -p1 < /path/to/net-quota-syscall/kernel-code/patches/<new patch>   # only for a new patch
make -j"$(nproc)" && make modules_install && make install && update-grub && reboot
```

If the tree already has net_quota in an older state, follow "Update an existing
build" in `setup-and-build.md`.

After the reboot, pick the `7.2.10-netquota` entry and run:

```bash
uname -r                                  # 7.2.10-netquota
cd user-code && make
sudo ./tests/run_tests.sh                 # ends with: ALL TESTS PASSED
```

The runner takes about a minute (some tests wait for quotas to expire), starts
its own echo servers on the machine's non-loopback address, and compares the
kernel log before and after.

The tests, and what each one proves:

| Test | Proves |
|---|---|
| `test_errors` | every error path of the system call, and the message texts |
| `test_basic` | one counter for send and receive, an oversized send is refused whole and costs nothing, the exact remainder can still be sent, `EDQUOT` both ways, expiry |
| `test_fork` | a child of a restricted process is unrestricted; the ancestor can still restrict it separately |
| `test_timer` | the window starts when the limit is set, even for a process that uses the network late or never |
| `test_loopback` | loopback and Unix-socket traffic is not counted and costs no quota |
| `test_exit` | quotas disappear with their process in every state; a new process starts unrestricted |
| `test_udp` | datagrams are refused whole, a refusal costs nothing, receives are still cut (this changes if the receive rule changes) |
| `test_threads` | four threads share one quota with no byte lost or gained (28 whole 700-byte blocks = 19600 of 20000), and a thread id is rejected |
| `test_calls` | each of `sendmsg`/`recvmsg`, `sendmmsg`/`recvmmsg`, `sendfile` (also one larger than the quota), `splice` both ways, `MSG_PEEK` (not counted, not refused at the limit), non-blocking `recv` (EAGAIN costs nothing) and IPv6 is counted exactly: after each, a probe checks that exactly the expected bytes are left |
| `test_uring` | io_uring send, receive, zero-copy send, and an oversized send refused whole are counted exactly; skips with a message if io_uring is off |
| `test_zerocopy` | reports whether data read with `TCP_ZEROCOPY_RECEIVE` is counted: a `FINDING` line, exit 0 either way |
| `test_realprog.sh` | a real program (`dd`, run with `exec` from the shell so it is the restricted process) started with `nq_run` is stopped at exactly the limit; the same `dd` run as a child of the restricted shell is not limited (a child starts unrestricted) |

A way to check kernel changes **without booting**: download the full source
(`https://codeload.github.com/gregkh/linux/tar.gz/refs/tags/v7.2.10`), copy in
`new-files/`, apply the patches, then `make defconfig && make prepare` and
`make W=1 net_quota/net_quota.o kernel/fork.o kernel/exit.o net/socket.o`.
It checks syntax, types and compiler warnings (none were reported for the two
latest changes) without a reboot. It does not check behaviour or locking.

## 7. Decisions and why

The answers behind the rules in section 2, as the project owner gave them:

- Count sent and received bytes together, payload only.
- A send that does not fit in what is left is refused whole (professor's third
  change; it replaced the earlier "allowed up to the limit"). It costs no
  quota, the error stays `EDQUOT`, multi-part calls keep working part by part,
  and the leftover bytes simply stay unused until the timer ends. The receive
  rule is still open; the earlier rule (shorten to what is left) stays until
  the professor's wording is recorded.
- Time is given in seconds and converted to jiffies; a kernel timer does the
  expiry; expiry is for good.
- Never overwrite a quota that is set; return an error instead (an earlier idea
  of asking for confirmation was dropped).
- Loopback and Unix sockets are not counted; the test server therefore listens
  on the machine's own non-loopback address.
- Only the caller's descendants can be restricted, with no special privilege.
- The quota lives in `signal_struct`, not `task_struct`, so a multi-threaded
  target cannot dodge it with a second thread.
- The descendant check walks **up** through `real_parent` from the target
  instead of down the children lists: same answer, O(depth), no allocation.
- Return codes and messages are `#define`d, with the texts in user space.

The three later changes came from the professor: timer at set time, no
inheritance on `fork()`, and no partial sends. The inheritance change was kept on its own branch for
a while because it opens a hole (section 8); it was then merged at the project
owner's request.

## 8. Known limitations and open items

1. **A restricted process can escape its limit by forking.** The child starts
   with no quota, so a program that forks and does its network traffic in the
   child is not limited. Only an ancestor that knows the child's pid can
   restrict it. Alternatives, none built: let the child inherit its own copy
   (the previous behaviour, which still lets forking multiply the limit), or
   let parent and children share one counter, which closes the hole.
2. The debug options: every option of the debug fragment was on during the
   hardware runs and found nothing (section 4).
3. Loopback is decided by the destination address, not the interface, so
   traffic to the machine's own non-loopback address is counted.
4. x86_64 only, needs `CONFIG_NET`, maximum period 7 days.
5. Call paths: all of them pass on the real kernel. `TCP_ZEROCOPY_RECEIVE` is
   refused under a quota (patch 0006). Still without a test: io_uring multishot
   receive with provided buffers, and io_uring zero-copy receive (needs a
   network card with header/data split, cannot run on a veth pair).
6. Descendants whose parent has died are reparented and can no longer be
   restricted (accepted); accounting is by calling process, so a socket handed
   to an unrestricted process escapes it, like the fork hole.
7. The receive rule of the third change is open (section 7).
8. A zero-length send with nothing left returns `EDQUOT` (decided: intended).
9. The full list, with an action for each item, is `docs/gap/README.md`.

## 9. Gotchas

- `nq_cli` can only restrict descendants of itself, so it is only useful for
  checking that the call exists. Use `nq_run <bytes> <seconds> <command...>` to
  restrict a real program.
- Under `sudo su`, `~` may mean `/root`. Use full paths.
- Copying an Ubuntu `.config` into a fresh upstream tree stops the build on a
  missing certificate file. Clear `SYSTEM_TRUSTED_KEYS` and
  `SYSTEM_REVOCATION_KEYS` (step A5 of the guide).
- `make install` normally runs `update-initramfs` and `update-grub` itself.
- Moving files between Windows and Linux can turn line endings into CRLF, which
  breaks `patch`. Use `git clone` or `git pull` on the Ubuntu machine.
- `git am` prints "whitespace errors" for the patch files in
  `kernel-code/patches/`: they contain patches, and Git is looking at the
  diff's own context lines. It is harmless.
- The patch loop of `setup-and-build.md` step B2 first ran each dry run on its
  own; with the old three `fork.c` patches that failed on 0007 (it needs the
  two before it). With the current six patches the loop works as written.
- `nq_run` restricts only the process it starts. `nq_run 20000 20 bash -c 'dd ...'`
  leaves dd unrestricted, because bash forks it and a child starts without a
  quota. Use `exec dd ...` inside the shell, or start the program directly.
  `test_realprog.sh` shows both.
- A quota that is set but never used now expires after its period, like any
  other; `test_timer` used to assert the opposite and was rewritten.

## 10. Suggested next steps

1. Get the professor's wording for the receive side of the third change, and
   record it in `docs/gap/professor-change-3-drop-oversized-request.md`. Then
   implement it (and update `test_udp`, which checks that a receive is cut).
2. Decide what to do about the fork hole (section 8, item 1), ideally with the
   professor. The `test_realprog.sh` result is a real-world example of it.
3. Optional: tests for io_uring multishot receive and io_uring zero-copy
   receive (section 4), or leave them documented as untested.
