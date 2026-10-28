# net_quota: design decisions and limitations

## Interface

```c
long net_quota(pid_t pid, u64 data_size, unsigned int period_sec);
```

Syscall 472 on x86_64. Returns `0` or a negative `NQ_E_*` code:

| Code | errno | Meaning |
|---|---|---|
| `NQ_E_BAD_ARGS` | EINVAL | pid <= 0, size 0, period 0 or above 7 days, or pid is a thread id |
| `NQ_E_SELF_TARGET` | EPERM | the caller tried to restrict itself |
| `NQ_E_NOT_DESCENDANT` | ESRCH | pid is missing, not in the caller's subtree, or the target is exiting |
| `NQ_E_ALREADY_SET` | EEXIST | the target already has a quota; nothing is changed |
| `NQ_E_QUOTA_EXCEEDED` | EDQUOT | returned by send/recv when the limit is used up |

No privilege is needed: being an ancestor of the target is the permission.

## Behaviour

- **Who can be targeted:** a process (not one thread) that is a child,
  grandchild, ... of the caller. All threads of the target share one quota.
- **What is counted:** payload bytes, sent and received, on one counter. Only
  AF_INET/AF_INET6 sockets whose peer is not a loopback address; AF_UNIX and
  other families are ignored. `MSG_PEEK` is not counted.
- **Which calls:** the hooks sit in `sock_sendmsg_nosec()`,
  `sock_recvmsg_nosec()` and `sock_splice_read()`, which covers send, sendto,
  sendmsg, sendmmsg, write, recv, recvfrom, recvmsg, recvmmsg, read, sendfile,
  splice and io_uring socket operations. `TCP_ZEROCOPY_RECEIVE`, which would
  bypass them, is refused with `EOPNOTSUPP` for a process under a quota.
- **Window:** `period_sec` seconds, started by the syscall itself: the timer
  runs from the moment the limit is set, whether or not the target uses the
  network. A kernel `timer_list` then revokes the quota. Nothing is renewed:
  the process is unrestricted afterwards, until an ancestor sets a new quota,
  which starts a fresh window and a counter from zero. A quota that is never
  used expires too.
- **Limit reached:** every further send/receive fails with EDQUOT until the
  window ends. A send larger than what is left is refused whole with EDQUOT on
  every kind of socket (TCP and UDP alike): nothing is sent and no quota is
  charged, so a smaller send can still succeed. A receive larger than what is
  left is still shortened to it (partial transfer); the receive rule is open,
  see `docs/gap/`.
- **Existing quota:** never overwritten. The call fails with EEXIST and
  changes nothing, including the counter and the timer. An expired quota is
  gone, so a new one can be set.
- **fork():** a child process does not inherit the quota. It starts with no
  limit and no timer, whatever its parent has. Threads share the parent's
  quota.
- **Exit:** the quota and its timer are removed when the last thread exits, so
  a reused PID starts without a limit.
- **Messages:** the kernel only returns the error number; the text comes from
  the user-space `nq_strerror()` using the `NQ_MSG_*` defines.

## Decision log

Where each rule came from. The first rows are the original design. The
professor's changes were applied later, in this order.

| Rule | Origin | Status |
|---|---|---|
| Sent and received payload bytes share one counter | original design | in `main` |
| Only descendants can be restricted, no privilege needed | assignment text | in `main` |
| A quota that is set is never overwritten (`EEXIST`) | original design | in `main` |
| Loopback and Unix sockets are not counted | original design | in `main` |
| The timer starts when the limit is set, not at the first use | professor, change 1 | in `main`; not yet run on the real kernel |
| A child created by `fork()` does not inherit the quota | professor, change 2 | in `main`; not yet run on the real kernel |
| A send larger than what is left is refused whole, nothing is charged, error stays `EDQUOT` | professor, change 3 (decided 2026-10-23) | in `main`; not yet run on the real kernel |
| A receive larger than what is left | professor, change 3, wording pending | **open**: receives are still shortened, see `docs/gap/` |
| `TCP_ZEROCOPY_RECEIVE` fails with `EOPNOTSUPP` for a process under a quota | project owner, 2026-10-28 (after the test found it uncounted) | patch 0006; compiled, not yet run on the real kernel |
| A zero-length send with nothing left returns `EDQUOT` | project owner, 2026-10-28 | in `main` (intended) |

The full text of change 3, the answers given to the five questions it raised,
and what is still open are in `docs/gap/professor-change-3-drop-oversized-request.md`.
The list of open gaps is `docs/gap/README.md`.

## Implementation notes

- State is `struct net_quota_info` inside `signal_struct`, guarded by a
  spinlock taken with `spin_lock_bh()` because the timer runs in softirq.
- A transfer reserves its bytes before it runs and refunds what it did not
  move, so two threads cannot overshoot the limit together. A generation
  counter stops a refund from touching a quota that was revoked or replaced
  while the transfer slept.
- A process without a quota pays one load and a branch per send/receive.
- The timer is armed when the limit is set, so something must stop it before
  the process's `signal_struct` is freed. `do_exit()` does it when the last
  thread exits. The exit path also sets a `dead` flag under the quota lock, so
  a `net_quota()` call that races with the exit cannot arm a timer that nobody
  would stop; it gets `-ESRCH` instead.
- The descendant check walks up through `real_parent` instead of down the
  children lists. Same answer, O(depth), no allocation, handles threads.

## Known limitations

1. **A restricted process can escape its limit by forking.** The child starts
   with no quota, so a program that forks and does its network traffic in the
   child, or in several children, is not limited at all. Only an ancestor that
   knows the child's pid can restrict it, with a call of its own. This follows
   from the rule that children start unrestricted.
2. **Loopback is decided by the destination address**, not the interface.
   Traffic to the machine's own non-loopback address is counted, which is
   what the tests rely on.
3. A receive on an unconnected socket from a loopback sender is charged
   up-front and refunded afterwards; it can be refused if the quota is
   already used up.
4. x86_64 only (no 32-bit or compat entry), requires `CONFIG_NET`, maximum
   period 7 days.
5. io_uring multishot receive (provided buffers) and io_uring zero-copy receive
   have no test: the first needs more set-up than `test_uring` does, the second
   needs a network card with header/data split.
6. Tested on one machine only (Ubuntu 24.04, x86_64, kernel 7.2.10 from
   `make defconfig`), and not with the lockdep and atomic-sleep checks of
   `config/net_quota_debug.config`.
7. **Orphans** (accepted by the project owner, 2026-10-28): the descendant
   check is made when the limit is set. If an intermediate process has died,
   its children have been reparented to init or a subreaper and are no longer
   descendants of the original caller, so they cannot be restricted. A quota
   that is already set is not affected.
8. **Traffic that is not counted.** By design, only IPv4/IPv6 sockets are
   counted. **`TCP_ZEROCOPY_RECEIVE`** through `getsockopt()` takes data
   straight from the receive queue, bypasses the hooks, and is **not counted**:
   `test_zerocopy` read 32768 bytes this way on the real kernel (2026-10-27)
   without being charged. Only a program that asks for it does this, but one
   that does can receive without limit. The other call paths (`sendmsg`,
   `sendmmsg`, `sendfile`, `splice`, io_uring send/receive and zero-copy send,
   `MSG_PEEK`, non-blocking calls) are counted exactly, as `test_calls` and
   `test_uring` show on the same run. IPv6 is covered by a test but that case
   has not run yet.
9. **Accounting is by the calling process, not by the socket.** A restricted
   process can hand a socket to an unrestricted process (a descriptor sent over
   a Unix socket, or one inherited by a child, which starts unrestricted), and
   that process's traffic is not counted. This is the same class of escape as
   limitation 1.
10. **A send that does not fit is refused, not cut** (change 3). A TCP stream
    can therefore be left with a few bytes it cannot use, until the timer
    expires. `EDQUOT` does not tell the caller whether the request was too big
    or the quota is used up; this is intended.
11. **Multi-part calls** (`sendfile`, `splice`, `sendmmsg`, io_uring) are
    charged per internal transfer. The parts that fit succeed and the first
    one that does not fit fails, so a caller may see a short count and then an
    error.
12. A zero-length send with nothing left returns `EDQUOT`, because the
    nothing-left check runs before the length is looked at. The project owner
    decided on 2026-10-28 that this stays; `test_calls` checks it.
13. **Real programs:** `nq_run` restricts the one process it starts. If that
    process is a shell or wrapper that runs the real program as a child, the
    child is unrestricted (limitation 1). Use `exec` inside the shell or start
    the program directly. `test_realprog.sh` checks both forms.
