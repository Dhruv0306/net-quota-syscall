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
| `NQ_E_NOT_DESCENDANT` | ESRCH | pid is missing or not in the caller's subtree |
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
  splice and io_uring socket operations.
- **Window:** `period_sec` seconds, started by the target's first counted
  send/receive, not by the syscall. A kernel `timer_list` then revokes the
  quota; afterwards the process is unrestricted for good.
- **Limit reached:** every further send/receive fails with EDQUOT until the
  window ends. A transfer that only partly fits is shortened to what is left
  (partial transfer) for receives and for stream sends. A UDP-style send that
  does not fit is refused whole instead, because a truncated datagram would
  be a corrupt message.
- **Existing quota:** never overwritten. The call fails with EEXIST and
  changes nothing, including the counter and the timer. An expired quota is
  gone, so a new one can be set.
- **fork():** a child process inherits its own quota: same limit and period,
  fresh counter, its own window from its first use. A parent whose quota has
  expired passes nothing on. Threads share the parent's quota.
- **Exit:** the quota and its timer are removed when the last thread exits, so
  a reused PID starts without a limit.
- **Messages:** the kernel only returns the error number; the text comes from
  the user-space `nq_strerror()` using the `NQ_MSG_*` defines.

## Implementation notes

- State is `struct net_quota_info` inside `signal_struct`, guarded by a
  spinlock taken with `spin_lock_bh()` because the timer runs in softirq.
- A transfer reserves its bytes before it runs and refunds what it did not
  move, so two threads cannot overshoot the limit together. A generation
  counter stops a refund from touching a quota that was revoked or replaced
  while the transfer slept.
- A process without a quota pays one load and a branch per send/receive.
- The descendant check walks up through `real_parent` instead of down the
  children lists. Same answer, O(depth), no allocation, handles threads.

## Known limitations

1. **Inheritance multiplies the limit.** Every fork gets a full copy, so a
   restricted process can spread traffic over children and move more than
   `limit` bytes in total.
2. **Loopback is decided by the destination address**, not the interface.
   Traffic to the machine's own non-loopback address is counted, which is
   what the tests rely on.
3. A receive on an unconnected socket from a loopback sender is charged
   up-front and refunded afterwards; it can be refused if the quota is
   already used up.
4. A window starts at the first counted transfer even if that transfer then
   fails.
5. x86_64 only (no 32-bit or compat entry), requires `CONFIG_NET`, maximum
   period 7 days.
6. Zero-copy and multishot variants of io_uring have not been checked.
7. The kernel code has not been compiled or booted yet.
