# How net_quota works

This page follows the data through the system: what goes in, what each layer
does with it, and what comes out. It has three flows (setting a quota, a
transfer under a quota, and the life of a quota), then the data structure,
how an error number becomes a message, and two worked examples.

The diagrams are Mermaid, which GitHub draws directly in the page. For the
decisions behind the behaviour and the known limitations, see
[user-code/docs/design.md](../user-code/docs/design.md).

## In one paragraph

A parent process calls `net_quota(pid, bytes, seconds)` to put a limit on one
of its descendants. The kernel stores the limit in that process. From then on,
every network send and receive of the process passes through a check in the
kernel that counts the payload bytes and refuses the call once the limit is
reached. A kernel timer lifts the limit when the period is over.

## 1. The big picture

```mermaid
flowchart TB
  subgraph US["User space"]
    direction TB
    APP["Parent program<br/>nq_run, nq_cli, the tests"]
    LIB["Wrapper library<br/>nq_set_quota and nq_strerror"]
    TARGET["Restricted program<br/>any send, recv, read, write"]
  end
  subgraph KS["Kernel space (Linux 7.2.10)"]
    direction TB
    ENTRY["System call entry<br/>number 472"]
    SYS["sys_net_quota<br/>net_quota/net_quota.c"]
    STATE["Quota state<br/>signal_struct.net_quota"]
    HOOK["Socket hooks<br/>net/socket.c"]
    TIMER["Expiry timer"]
    NET["TCP/IP stack and network card"]
  end
  APP -->|"pid, bytes, seconds"| LIB
  LIB -->|"syscall 472"| ENTRY
  ENTRY --> SYS
  SYS -->|"install the limit"| STATE
  TARGET -->|"send or recv"| HOOK
  HOOK <-->|"check and charge"| STATE
  HOOK --> NET
  STATE -->|"setting the limit starts it"| TIMER
  TIMER -->|"period over: clear"| STATE
```

Two separate paths meet in the quota state:

- **Setup path** (left side, runs once): the parent writes the limit.
- **Enforcement path** (right side, runs on every transfer): the restricted
  program reads and updates the counter.

The parent and the restricted program are different processes. The only thing
they share is the quota state inside the kernel.

## 2. Flow one: setting a quota

```mermaid
sequenceDiagram
  autonumber
  participant P as Parent program
  participant W as Wrapper (user space)
  participant K as sys_net_quota (kernel)
  participant Q as Target's quota state
  P->>W: nq_set_quota(pid, bytes, seconds)
  W->>K: syscall(472, pid, bytes, seconds)
  K->>K: check pid, size and period
  K->>K: find the task, it must be a process and not the caller
  K->>K: walk up real_parent, is the caller an ancestor?
  K->>Q: net_quota_install(limit, period)
  alt a quota is already set
    Q-->>K: refused
    K-->>W: -EEXIST
  else no quota yet
    Q-->>K: state NONE becomes ACTIVE, timer starts
    K-->>W: 0
  end
  W-->>P: 0, or -1 with errno set
  P->>P: nq_strerror(errno) gives the message
```

What goes in and what comes out at each step:

| Step | Layer | Input | Does | Output |
|---|---|---|---|---|
| 1 | Parent program | pid, bytes, seconds typed or passed by the caller | calls the wrapper | the wrapper's result |
| 2 | Wrapper `nq_set_quota()` in `user-code/src/net_quota.c` | the three numbers | `syscall(472, ...)` through libc | 0, or -1 and `errno` |
| 3 | Kernel entry | system call number 472 and three register values | the CPU switches to kernel mode; the number selects `sys_net_quota` in `syscall_64.tbl` | calls `SYSCALL_DEFINE3(net_quota, ...)` |
| 4 | Checks in `net_quota/net_quota.c` | pid, size, period | rejects bad values, a thread id, the caller itself, a non-descendant | `-EINVAL`, `-EPERM`, `-ESRCH` on failure |
| 5 | `net_quota_install()` | limit and period | under a lock: if the state is NONE, store the limit, set state ACTIVE and start the timer for the full period | 0, or `-EEXIST` |
| 6 | Back in user space | the kernel's negative number | libc turns `-N` into return value -1 and `errno = N` | the wrapper returns it |
| 7 | `nq_strerror()` | `errno`, pid, bytes | picks the matching `NQ_MSG_*` text | a readable message on stderr |

The call changes nothing except the state of the target. The timer starts
here, in the same moment the limit is set: the window does not wait for the
target's first network use, and it ends `seconds` later whether or not the
target has used the network by then.

## 3. Flow two: a transfer under a quota

This runs inside the kernel every time the restricted process sends or
receives, whatever call it used (`send`, `sendto`, `sendmsg`, `write`,
`recv`, `read`, `sendfile`, `splice`, io_uring).

```mermaid
flowchart TD
  A["Program calls send, recv, read, write or sendfile"] --> B["sock_sendmsg_nosec or sock_recvmsg_nosec"]
  B --> C{"Process has a quota?"}
  C -- no --> Z["Run the transfer as usual"]
  C -- yes --> D{"Counted socket?<br/>IPv4 or IPv6 and peer not 127.x"}
  D -- no --> Z
  D -- yes --> F{"Bytes left?"}
  F -- none --> X["Return -EDQUOT"]
  F -- some --> G{"Request larger than what is left?"}
  G -- no --> H["Reserve the bytes"]
  G -- yes --> I{"Stream send or any receive?"}
  I -- yes --> J["Shorten the request to what is left<br/>then reserve it"]
  I -- no --> X
  H --> K["Protocol does the transfer<br/>tcp_sendmsg, udp_recvmsg and so on"]
  J --> K
  K --> L["Give back reserved bytes that were not moved"]
  L --> M["Return the byte count"]
```

The same flow in words, with the data that moves:

1. **Input:** the program's buffer and length (`len`) from user space.
2. **Quick exit:** a process without a quota costs one load and one branch
   (`net_quota_active()`); the rest is skipped.
3. **Which traffic counts:** only IPv4/IPv6 sockets whose peer is not a
   loopback address. Unix sockets and `127.x` traffic are ignored.
4. **Reserve before moving:** the allowed number of bytes is added to `used`
   up front, so two threads cannot both slip past the limit.
5. **Partial transfer:** if `len` is more than what is left, a TCP send or any
   receive is shortened (`iov_iter_truncate`). A UDP send is refused whole,
   because cutting a datagram short would deliver a corrupt message.
6. **After the transfer:** bytes that were reserved but not moved are given
   back. A receive from a loopback sender on an unconnected socket is not
   charged.
7. **Output:** the number of bytes moved, or `-1` with `errno = EDQUOT` when
   nothing is left.

Receive-side `splice` calls the protocol directly, so it has its own hook in
`sock_splice_read()`.

## 4. Flow three: the life of a quota

```mermaid
stateDiagram-v2
  [*] --> NONE
  NONE --> ACTIVE: net_quota succeeds, timer starts
  NONE --> ACTIVE: fork of a restricted process, child inherits, its timer starts
  ACTIVE --> NONE: timer fires after the period, revoked for good
  ACTIVE --> [*]: process exits, timer stopped
  note right of ACTIVE
    When used reaches limit, every
    send and receive returns -EDQUOT
    until the timer fires.
  end note
```

| Event | Where in the kernel | What happens |
|---|---|---|
| `net_quota()` succeeds | `net_quota_install()` | NONE becomes ACTIVE; limit and period stored; `mod_timer()` starts the period |
| Period ends | `nq_timer_fn()` in softirq | quota cleared back to NONE; nothing is renewed |
| `fork()` of a restricted process | `copy_signal()` in `kernel/fork.c` | child gets the same limit and period and a fresh counter, and its own timer starts at the fork for the full period |
| Last thread exits | `do_exit()` in `kernel/exit.c` | timer stopped, quota cleared; a reused PID starts clean |
| Second `net_quota()` call | `net_quota_install()` | refused with `-EEXIST`; nothing is changed |

Threads and `fork()` differ on purpose. Threads of one process share the
quota. A new child process gets its own copy, so a restricted process that
forks can move more than the limit in total (listed in the known limitations).

## 5. Where the quota lives

```mermaid
flowchart LR
  T1["thread 1<br/>task_struct"] --> S
  T2["thread 2<br/>task_struct"] --> S
  T3["thread 3<br/>task_struct"] --> S
  S["signal_struct<br/>one per process"] --> Q["net_quota_info<br/>state, limit, used, period,<br/>gen, lock, timer"]
```

| Field | Meaning |
|---|---|
| `state` | NONE or ACTIVE; read without the lock on the fast path |
| `dead` | set when the process exits, so a late `net_quota()` call cannot start a timer that nobody would stop |
| `limit` | payload bytes allowed, sent plus received |
| `used` | bytes charged so far, including transfers still in flight |
| `period_sec` | length of the window in seconds |
| `gen` | bumped on every install and revoke, so a transfer that started under an old quota never touches a newer one |
| `lock` | spinlock taken with `spin_lock_bh()` because the timer runs in softirq |
| `timer` | revokes the quota when the window ends |

## 6. From a kernel error to a message

The kernel never builds text. It returns a number, and user space turns it
into a message.

```mermaid
flowchart LR
  K["Kernel returns<br/>-EEXIST"] --> L["libc syscall()<br/>returns -1, errno = EEXIST"]
  L --> M["nq_strerror(errno, pid, bytes)"]
  M --> N["NQ_MSG_ALREADY_SET<br/>printed to stderr"]
```

| Kernel returns | `errno` | Constant | Message (start) |
|---|---|---|---|
| 0 | 0 | `NQ_OK` | "network data limit of N bytes set" |
| `-EINVAL` | EINVAL | `NQ_E_BAD_ARGS` | "Invalid arguments for process" |
| `-EPERM` | EPERM | `NQ_E_SELF_TARGET` | "a process cannot restrict itself" |
| `-ESRCH` | ESRCH | `NQ_E_NOT_DESCENDANT` | "is not a descendant of the calling process" |
| `-EEXIST` | EEXIST | `NQ_E_ALREADY_SET` | "a network data limit is already set" |
| `-EDQUOT` from send or recv | EDQUOT | `NQ_E_QUOTA_EXCEEDED` | "exceeded the maximum network data quota of N bytes" |
| `-ENOSYS` (kernel lacks the call) | ENOSYS | none | "The net_quota system call is not available" |

The codes are `#define`d once, in `kernel-code/new-files/include/uapi/linux/net_quota.h`,
and the message texts once, in `user-code/include/net_quota_msgs.h`.

A restricted program that knows nothing about quotas only sees the standard
`EDQUOT` text, "Disk quota exceeded". The longer message comes from programs
that call `nq_perror()`, such as the tests.

## 7. Worked example: the counter step by step

`test_basic` with a limit of 10000 bytes and a period of 4 seconds:

| Step | Call | Asked for | Allowed | Returned | `used` after |
|---|---|---|---|---|---|
| 1 | send | 3000 | 3000 | 3000 | 3000 |
| 2 | recv (the echo) | 3000 | 3000 | 3000 | 6000 |
| 3 | send | 8000 | 4000 (shortened) | 4000 | 10000 |
| 4 | send | 100 | 0 | -1, EDQUOT | 10000 |
| 5 | recv | 100 | 0 | -1, EDQUOT | 10000 |
| 6 | wait 6 s | | | | timer fired, quota gone |
| 7 | send 1000, recv 5000 | | no limit | 1000, 5000 | not counted |

Sent and received bytes share the counter (steps 1 and 2), the last send is
cut to fit (step 3), both directions stop at the limit (steps 4 and 5), and
the timer lifts the limit by itself (steps 6 and 7).

## 8. Worked example: a real program

```bash
./build/nq_run 20000 20 bash -c \
  'exec 3<>/dev/tcp/<server-ip>/5001; dd if=/dev/zero bs=1000 count=60 >&3'
```

| Where | What happens |
|---|---|
| `nq_run` (user space) | forks a child that waits, calls `nq_set_quota(child, 20000, 20)`, prints "limit of 20000 bytes set", then lets the child `exec` bash |
| Kernel | the quota is stored with state ACTIVE and the 20-second timer starts; it survives `exec` because it belongs to the process |
| `dd` (user space) | writes 1000-byte blocks to the socket |
| Kernel, first write | 1000 bytes reserved and sent (the 20 seconds have been running since the limit was set) |
| Kernel, writes 2 to 20 | each is charged; `used` reaches 20000 |
| Kernel, write 21 | nothing left: returns `-EDQUOT` |
| `dd` (user space) | prints "Disk quota exceeded" and "20000 bytes copied", then exits with an error |

## 9. Which file does what

| Role | File |
|---|---|
| Return codes shared by kernel and user space | `kernel-code/new-files/include/uapi/linux/net_quota.h` |
| The system call, descendant check, timer, socket hook logic | `kernel-code/new-files/net_quota/net_quota.c` |
| Quota struct and lifecycle functions | `kernel-code/new-files/include/linux/net_quota.h` |
| Inline fast path for send, receive, splice | `kernel-code/new-files/include/net/net_quota.h` |
| Quota field in the process | `kernel-code/patches/0001-*` (`include/linux/sched/signal.h`) |
| Inherit at fork | `kernel-code/patches/0002-*` (`kernel/fork.c`) |
| Clear at exit | `kernel-code/patches/0003-*` (`kernel/exit.c`) |
| Hooks on send and receive | `kernel-code/patches/0004-*` (`net/socket.c`) |
| Build and register as syscall 472 | `kernel-code/patches/0005-*` (`Kbuild`, `syscall_64.tbl`, `syscalls.h`) |
| Stop the timer if `fork()` fails | `kernel-code/patches/0006-*` (`kernel/fork.c`) |
| Wrapper and messages | `user-code/src/net_quota.c`, `user-code/include/net_quota_msgs.h` |
| Command-line tools | `user-code/src/nq_cli.c`, `nq_run.c`, `nq_demo.c` |
| Tests | `user-code/tests/` |

## An auto-generated diagram for comparison

[GitDiagram](https://github.com/ahmedkhaleel2004/gitdiagram) draws an
architecture diagram of any public repository: replace `hub` with `diagram`
in the repository URL (`gitdiagram.com/Dhruv0306/net-quota-syscall`). It works
from the file layout, so it shows which parts exist and how they connect. The
diagrams above show what it cannot: the path a byte takes inside the kernel,
the timer, and the quota's life cycle.
