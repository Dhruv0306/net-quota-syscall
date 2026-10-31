# How net_quota works

This page explains the whole system with diagrams, from the system call that
sets a limit to the moment a send or receive is refused and the moment the
limit ends. It is written from the code of the current version (patches 0001
to 0006 and the files in `kernel-code/new-files/`, for Linux 7.2.12).

How to read the diagrams:

- A **diamond** is a decision. The text on each arrow leaving it is the
  condition for that branch.
- A **green box** is a success return, a **red box** is an error return, a grey
  box is something that happens on the way.
- Return values are written as the kernel gives them, for example
  `-EINVAL`. User space sees `-1` and finds `EINVAL` in `errno`. The exact
  texts that the tools print are in the tables under the diagrams.
- The pages that go with this one are listed in section 10. The system call
  alone, as a reference, is in [syscall-reference.md](syscall-reference.md).

## 1. The pieces

```mermaid
flowchart LR
  subgraph US["User space"]
    APP["Any program<br/>send, recv, read, write, sendfile, splice, io_uring"]
    RUN["nq_run<br/>starts a command under a limit"]
    CLI["nq_cli<br/>sets a limit on a descendant"]
    LIB["libnq wrapper<br/>nq_set_quota, nq_strerror, nq_perror"]
  end
  subgraph KS["Kernel"]
    SC["sys_net_quota<br/>syscall 472"]
    NQ["struct net_quota_info<br/>in signal_struct<br/>one per process"]
    HK["Socket hooks in net/socket.c<br/>sock_sendmsg_nosec<br/>sock_recvmsg_nosec<br/>sock_splice_read"]
    ZC["TCP hook in net/ipv4/tcp.c<br/>TCP_ZEROCOPY_RECEIVE"]
    TM["Timer<br/>ends the window"]
    FE["fork and exit hooks<br/>copy_signal, do_exit"]
  end
  RUN --> LIB
  CLI --> LIB
  LIB -->|"syscall 472"| SC
  SC -->|"set limit, arm timer"| NQ
  APP -->|"every transfer"| HK
  APP -->|"getsockopt"| ZC
  HK -->|"check, reserve, settle"| NQ
  ZC -->|"asks: has a quota?"| NQ
  TM -->|"clears the quota"| NQ
  FE -->|"init at fork, clear at exit"| NQ
```

| Part | Where it is |
|---|---|
| The system call, the counting rules, the timer | `kernel-code/new-files/net_quota/net_quota.c` |
| The record (`net_quota_info`) and the state names | `kernel-code/new-files/include/linux/net_quota.h` |
| The inline fast path and the ticket | `kernel-code/new-files/include/net/net_quota.h` |
| The constants and error codes shared with user space | `kernel-code/new-files/include/uapi/linux/net_quota.h` |
| The record inside `signal_struct` | patch 0001 |
| Initialise at fork | patch 0002 |
| Clear at exit | patch 0003 |
| The socket hooks | patch 0004 |
| Build, register as syscall 472 | patch 0005 |
| Refuse `TCP_ZEROCOPY_RECEIVE` under a quota | patch 0006 |
| The wrapper and the tools | `user-code/src/`, `user-code/include/` |

## 2. The quota record and its states

The record lives in `signal_struct`, which all threads of a process share, so a
limit belongs to the **process**, never to one thread.

| Field | Meaning |
|---|---|
| `state` | `NQ_STATE_NONE` (no quota) or `NQ_STATE_ACTIVE` (limit set, timer running, bytes are charged) |
| `limit` | bytes allowed in the window, sent and received together |
| `used` | bytes charged so far, including bytes reserved by transfers in flight |
| `period_sec` | length of the window |
| `gen` | generation counter, increased every time a quota is set or cleared |
| `dead` | set when the process starts exiting; nothing can be set on it any more |
| `lock`, `timer` | a spinlock for the record, and the kernel timer that ends the window |

```mermaid
stateDiagram-v2
  [*] --> NONE: process created, net_quota_init
  NONE --> ACTIVE: net_quota syscall succeeds, limit set, used 0, timer armed
  ACTIVE --> ACTIVE: transfer charged, used grows up to limit
  ACTIVE --> NONE: timer fires, window over
  ACTIVE --> NONE: last thread exits
  NONE --> NONE: last thread exits, dead is set
  NONE --> [*]: process gone
```

"Exhausted" is not a state: it is `state` ACTIVE with `used` equal to
`limit`. In that situation every counted send and receive fails until the
window ends, and a second `net_quota()` call on that process still returns
`-EEXIST`.

## 3. Setting a quota: the system call

`net_quota(pid, data_size, period_sec)` checks, in this order, and stops at the
first check that fails.

```mermaid
flowchart TD
  A(["net_quota(pid, data_size, period_sec)"]) --> B{"pid below 1, or data_size 0,<br/>or period_sec 0,<br/>or period_sec above 604800?"}
  B -->|"yes"| E1["return -EINVAL<br/>bad arguments"]
  B -->|"no"| C["find_task_by_vpid(pid)<br/>in the caller's pid namespace"]
  C --> D{"process found?"}
  D -->|"no"| E2["return -ESRCH<br/>not a descendant"]
  D -->|"yes"| F{"is pid a process id,<br/>the thread group leader?"}
  F -->|"no, it is a thread id"| E1b["return -EINVAL<br/>bad arguments"]
  F -->|"yes"| G{"same process as the caller?"}
  G -->|"yes"| E3["return -EPERM<br/>cannot restrict itself"]
  G -->|"no"| H{"is the caller an ancestor?<br/>walk up through real_parent"}
  H -->|"no"| E2b["return -ESRCH<br/>not a descendant"]
  H -->|"yes"| I["hold the target,<br/>call net_quota_install"]
  I --> J{"target already exiting?<br/>dead is set"}
  J -->|"yes"| E2c["return -ESRCH<br/>not a descendant"]
  J -->|"no"| K{"a quota is already active?<br/>state is not NONE"}
  K -->|"yes"| E4["return -EEXIST<br/>already set, nothing overwritten"]
  K -->|"no"| L["limit = data_size, used = 0,<br/>period = period_sec, gen + 1,<br/>state = ACTIVE,<br/>timer armed for period_sec seconds"]
  L --> OK["return 0<br/>limit set"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  classDef step fill:#e9ecef,stroke:#495057,color:#212529
  class OK good
  class E1,E1b,E2,E2b,E2c,E3,E4 bad
  class C,I,L step
```

The window starts at step "timer armed", that is **when the limit is set**,
whether or not the process uses the network. When it ends the quota is revoked
(section 7).

How the caller walks up the process tree (`nq_is_descendant`). It costs
O(depth), needs no allocation, and works when the caller has several threads,
because a child's `real_parent` is the thread that forked it.

```mermaid
flowchart TD
  S(["start: p is the target"]) --> P["parent = real_parent of p"]
  P --> Q{"parent is p?<br/>reached init"}
  Q -->|"yes"| N["return false<br/>not a descendant"]
  Q -->|"no"| R{"parent is in the same<br/>process as the caller?"}
  R -->|"yes"| Y["return true<br/>a descendant"]
  R -->|"no"| U["p = parent"]
  U --> P

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  class Y good
  class N bad
```

The descendant check is made once, when the limit is set. A process whose
parent died before that was handed to init (or a subreaper) and is no longer a
descendant; a quota that is already set is not affected.

Return values of the system call:

| Kernel returns | User space sees | Condition | Text printed by `nq_perror()` |
|---|---|---|---|
| `0` | `0` | limit set | `Process <pid>: network data limit of <bytes> bytes set` |
| `-EINVAL` | `-1`, `errno` = `EINVAL` | bad pid, size or period, or `pid` is a thread id | `Invalid arguments for process <pid> (limit <bytes> bytes): the pid must be a process id, and size and period must be positive and within the allowed range` |
| `-EPERM` | `-1`, `errno` = `EPERM` | the caller tried to restrict itself | `Process <pid>: a process cannot restrict itself (requested limit: <bytes> bytes)` |
| `-ESRCH` | `-1`, `errno` = `ESRCH` | no such process, not a descendant, or the target is exiting | `Process <pid> is not a descendant of the calling process (requested limit: <bytes> bytes)` |
| `-EEXIST` | `-1`, `errno` = `EEXIST` | a quota is already active on that process | `Process <pid>: a network data limit is already set, the requested <bytes> bytes were not applied` |
| `-ENOSYS` | `-1`, `errno` = `ENOSYS` | the running kernel has no syscall 472 | `The net_quota system call is not available: boot the net-quota kernel` |

## 4. User space: the wrapper and the tools

The kernel returns numbers. The texts are built in user space, from one table
of `#define`d messages (`net_quota_msgs.h`), so the codes and the texts are
written down once.

```mermaid
flowchart TD
  A(["nq_set_quota(pid, bytes, secs)"]) --> B["syscall 472 with the three values"]
  B --> C{"kernel returned 0?"}
  C -->|"yes"| R0["return 0"]
  C -->|"no"| R1["return -1<br/>errno holds the code"]
  R1 --> D(["caller: nq_strerror(errno, pid, bytes) or nq_perror"])
  D --> E{"errno is"}
  E -->|"0"| M0["Process N: network data limit of B bytes set"]
  E -->|"EEXIST"| M1["a network data limit is already set,<br/>the requested B bytes were not applied"]
  E -->|"ESRCH"| M2["Process N is not a descendant<br/>of the calling process"]
  E -->|"EPERM"| M3["a process cannot restrict itself"]
  E -->|"EINVAL"| M4["Invalid arguments for process N"]
  E -->|"EDQUOT"| M5["Process N exceeded the maximum<br/>network data quota of B bytes"]
  E -->|"ENOSYS"| M6["The net_quota system call is not available:<br/>boot the net-quota kernel"]
  E -->|"anything else"| M7["net_quota failed for process N<br/>with error E and the strerror text"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  class R0,M0 good
  class R1,M1,M2,M3,M4,M5,M6,M7 bad
```

`EDQUOT` is not returned by `net_quota()`. It is the error that `send` and
`recv` report when a quota is used up; `nq_strerror` has a text for it so a
program can print it the same way.

**nq_cli** sets a limit on a descendant of itself, so by itself it is only
useful to check that the call exists. It only checks that each argument is a
plain number; a zero that passes this check is refused by the kernel with
`-EINVAL`, so it ends with exit 1 and the `EINVAL` text. Exit codes:

```mermaid
flowchart TD
  A(["nq_cli pid bytes seconds"]) --> B{"exactly 3 arguments?"}
  B -->|"no"| U["print usage<br/>exit 2"]
  B -->|"yes"| C{"all three are plain decimal numbers<br/>without a sign, and fit?<br/>pid up to INT_MAX, seconds up to UINT_MAX"}
  C -->|"no"| V["print: pid, bytes and seconds must be plain positive numbers<br/>exit 2"]
  C -->|"yes"| D{"nq_set_quota returned 0?"}
  D -->|"yes"| OK["print the OK text<br/>exit 0"]
  D -->|"no"| ER["print the error text for errno<br/>exit 1"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  class OK good
  class U,V,ER bad
```

**nq_run** is the tool that restricts a real program. It starts the command as
its child, sets the limit on that child before the command starts, then lets it
go. The quota survives `exec`.

```mermaid
sequenceDiagram
  participant R as nq_run
  participant C as child process
  participant K as kernel
  R->>R: parse bytes and seconds, create a pipe
  R->>C: fork
  Note over C: waits to read one byte from the pipe
  R->>K: net_quota(child, bytes, seconds)
  alt the call returns 0
    K-->>R: 0
    R->>R: print the OK text on stderr
    R->>C: write one byte
    C->>C: execvp(command)
    alt exec fails
      C-->>R: exit 127
    else exec works
      Note over C: runs under the limit
      C-->>R: exits with a status, or is killed by a signal
    end
    R-->>R: exit with the command's status, or 128 plus the signal
  else the call fails
    K-->>R: -1 and errno
    R->>R: print the error text
    R->>C: SIGKILL
    R-->>R: exit 1
  end
```

`nq_run` returns 2 for a bad command line, 1 if it cannot set up the pipe, fork
or limit, and 126 is the child's exit status if the pipe is closed before the
go byte. Children that the command forks are **not** restricted: a shell that
runs the real program as a child leaves it free; make the shell `exec` it.

## 5. Charging a transfer

### 5.1 Where the hooks sit

```mermaid
flowchart TD
  A(["send, write, sendmsg, sendfile, io_uring send"]) --> S["sock_sendmsg_nosec"]
  B(["recv, read, recvmsg, io_uring recv"]) --> RV["sock_recvmsg_nosec"]
  C(["splice from a socket"]) --> SP["sock_splice_read"]
  S --> H1["net_quota_begin"]
  RV --> H1
  SP --> H1
  H1 --> Q{"process has a quota?<br/>in task context and state is not NONE"}
  Q -->|"no"| P["call the protocol as usual<br/>cost: one load and one branch"]
  Q -->|"yes"| BG["__net_quota_begin<br/>section 5.3"]
  BG --> BR{"result"}
  BR -->|"refused: -EDQUOT"| X["return -EDQUOT to the caller<br/>the protocol is not called"]
  BR -->|"0, charged or not"| P2["call the protocol<br/>possibly with a shorter buffer"]
  P2 --> EN["net_quota_end<br/>section 5.5"]
  EN --> RT["return what the protocol returned"]
  P --> RT

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  class RT good
  class X bad
```

### 5.2 Which transfers are counted

Only IPv4 and IPv6 sockets whose peer is not a loopback address are counted.
`127.0.0.0/8`, `::1` and the IPv4-mapped loopback are loopback; the machine's
own non-loopback address is not.

```mermaid
flowchart TD
  A(["is this transfer counted?"]) --> B{"socket has an underlying sock?"}
  B -->|"no"| N0["not counted"]
  B -->|"yes"| C{"address family"}
  C -->|"AF_INET or AF_INET6"| D{"explicit destination given?<br/>sendto, sendmsg with a name"}
  C -->|"AF_UNIX, netlink, packet, others"| N1["not counted"]
  D -->|"yes"| E{"destination is loopback?"}
  E -->|"yes"| N2["not counted"]
  E -->|"no"| Y1["counted"]
  D -->|"no"| F{"socket is connected?"}
  F -->|"yes"| G{"connected peer is loopback?"}
  G -->|"yes"| N3["not counted"]
  G -->|"no"| Y2["counted"]
  F -->|"no"| H{"direction"}
  H -->|"send"| Y3["counted"]
  H -->|"receive"| Y4["counted, sender unknown yet<br/>checked after the data arrives"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef neutral fill:#e9ecef,stroke:#495057,color:#212529
  class Y1,Y2,Y3,Y4 good
  class N0,N1,N2,N3 neutral
```

### 5.3 Check and reserve: `__net_quota_begin`

This is the heart of the system. It decides whether one send or receive may go
ahead, and how many bytes it may move. The input is the number of bytes the
caller asks for (`len`), the direction and the flags.

```mermaid
flowchart TD
  A(["begin: len, direction, flags"]) --> B{"MSG_PEEK set?"}
  B -->|"yes"| Z1["return 0, not charged<br/>peeking does not consume data"]
  B -->|"no"| C{"transfer counted?<br/>section 5.2"}
  C -->|"no"| Z2["return 0, not charged"]
  C -->|"yes"| D{"a receive without<br/>MSG_ERRQUEUE or MSG_OOB?"}
  D -->|"yes"| W["waiting = data already queued<br/>TCP: bytes queued, UDP: next datagram,<br/>other protocols: 0, nothing queued: 0"]
  D -->|"no"| W0["waiting = 0"]
  W --> E{"waiting above 0, unconnected UDP,<br/>and waiting above what is left?"}
  W0 --> L
  E -->|"yes"| SL["look up the sender of the next datagram<br/>zero-length MSG_PEEK, nothing is dequeued"]
  E -->|"no"| L
  SL --> L["take the quota lock"]
  L --> F{"state is NONE?<br/>revoked meanwhile"}
  F -->|"yes"| Z3["return 0, not charged"]
  F -->|"no"| G{"what is left is 0?"}
  G -->|"yes"| X1["return -EDQUOT<br/>nothing left"]
  G -->|"no"| H{"waiting above 0?"}
  H -->|"yes"| J{"judged size above what is left?<br/>UDP: the datagram size<br/>TCP: smaller of buffer and queued"}
  J -->|"yes"| K{"sender is loopback?"}
  K -->|"yes"| Z4["return 0, not charged<br/>a loopback datagram is not counted"]
  K -->|"no"| X2["return -EDQUOT<br/>refused whole, data stays queued,<br/>nothing charged"]
  J -->|"no"| M["len = smaller of len and waiting"]
  H -->|"no"| N{"len above what is left?"}
  N -->|"no"| RS
  N -->|"yes"| O{"direction"}
  O -->|"send"| X3["return -EDQUOT<br/>send refused whole, nothing charged"]
  O -->|"receive"| P["len = what is left<br/>the receive is cut"]
  M --> RS
  P --> RS["reserve: used grows by len,<br/>write the ticket: gen, reserved, direction"]
  RS --> OK["return 0, charged<br/>the call may move up to len bytes"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  classDef neutral fill:#e9ecef,stroke:#495057,color:#212529
  class OK good
  class X1,X2,X3 bad
  class Z1,Z2,Z3,Z4 neutral
```

What `__net_quota_begin` returns, and what the hook does with it:

| Result | Ticket | Meaning | What happens next |
|---|---|---|---|
| `0` | not active | the transfer is not charged: `MSG_PEEK`, a socket that is not counted, a quota revoked in the meantime, or a loopback datagram | the protocol is called, `net_quota_end` does nothing |
| `0` | active, `reserved` = n | n bytes are reserved against the quota; n may be smaller than asked | the protocol is called with a buffer cut to n if needed; `net_quota_end` settles |
| `-EDQUOT` | none | nothing left, or the request does not fit | the hook returns `-EDQUOT` to the caller; the protocol is not called; nothing is charged |

The rule behind the two refusals that are not "nothing left":

- A **send** that does not fit is refused whole, on every kind of socket. A
  smaller send still works.
- A **receive** is judged by the data already waiting in the socket, not by the
  size of the buffer. See the next section.

### 5.4 The receive rule in detail

```mermaid
flowchart TD
  A(["a counted receive, data may be waiting"]) --> B{"protocol"}
  B -->|"TCP"| C{"bytes queued in the socket"}
  B -->|"UDP or UDP-Lite"| D{"datagrams queued?"}
  B -->|"other: SCTP, MPTCP, raw"| F["size not known in advance<br/>the receive is cut to what is left"]
  C -->|"none, the call will block"| F
  C -->|"some"| G["judged = smaller of buffer and queued bytes"]
  D -->|"none, the call will block"| F
  D -->|"some"| H["judged = size of the next datagram<br/>whatever the buffer size"]
  G --> I{"judged above what is left?"}
  H --> I
  I -->|"yes"| R["refused whole with -EDQUOT<br/>the data stays queued, nothing charged"]
  I -->|"no"| O["go ahead, limited to the data that was waiting<br/>charged with the bytes really moved"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  classDef neutral fill:#e9ecef,stroke:#495057,color:#212529
  class O good
  class R bad
  class F neutral
```

Examples (the cases that the tests check):

| Situation | What is left | Result |
|---|---|---|
| TCP, 3000 bytes queued, buffer 4096 | 2000 | judged 3000 is more than 2000: `-EDQUOT`, the 3000 bytes stay queued |
| the same, then a receive with buffer 2000 | 2000 | judged 2000: goes ahead, returns 2000, 1000 bytes stay queued |
| TCP, nothing queued yet, buffer 4096 | 1500 | the size is not known: the buffer is cut to 1500 |
| UDP, a 400-byte datagram queued, buffer 1000 | 100 | `-EDQUOT`, the datagram stays queued |
| UDP, the same datagram, buffer 50 | 100 | `-EDQUOT` too: the datagram is judged, not the buffer |
| unconnected UDP, 400-byte datagram from a loopback sender | 100 | goes ahead, not charged: loopback is not counted |
| any counted receive | 0 | `-EDQUOT` |

Two consequences: a UDP datagram that is larger than what is left stays at the
head of the queue and every later receive is refused until the window ends; and
a program that reads a TCP stream with a large buffer gets `EDQUOT` as soon as
that buffer is larger than what is left, and works again with a smaller one.

### 5.5 Settle: `__net_quota_end`

After the protocol has moved some bytes, the ticket is settled: what was
reserved but not moved is given back.

```mermaid
flowchart TD
  A(["end: ticket, bytes moved or an error"]) --> B["moved = result if above 0, else 0"]
  B --> C{"moved above what was reserved?<br/>for example MSG_TRUNC"}
  C -->|"yes"| D["moved = reserved"]
  C -->|"no"| E
  D --> E{"moved above 0, sender was unknown,<br/>and the sender is loopback?"}
  E -->|"yes"| F["moved = 0, nothing is charged"]
  E -->|"no"| G
  F --> G["refund = reserved - moved"]
  G --> H{"refund is 0?"}
  H -->|"yes"| Z(["done"])
  H -->|"no"| I["take the quota lock"]
  I --> J{"same generation as at begin,<br/>and state not NONE?"}
  J -->|"yes"| K["used = used - refund"]
  J -->|"no"| L["do nothing: the quota was revoked<br/>or replaced in the meantime"]
  K --> Z
  L --> Z
```

### 5.6 What the program sees

| Situation | `send` / `write` | `recv` / `read` |
|---|---|---|
| process has no quota, or the transfer is not counted | whatever the protocol returns | whatever the protocol returns |
| goes ahead | bytes sent | bytes received |
| nothing left | `-1`, `errno` = `EDQUOT` ("Disk quota exceeded") | `-1`, `errno` = `EDQUOT` |
| does not fit | `-1`, `EDQUOT`, nothing sent | `-1`, `EDQUOT`, data stays queued (see 5.4) |
| the limit ends (timer) | works again, no limit | works again, no limit |

A program that knows nothing about quotas just sees an ordinary error. `dd`,
for example, stops with `dd: error writing 'standard output': Disk quota
exceeded`.

## 6. Which calls reach the hooks

```mermaid
flowchart LR
  subgraph SEND["Send side"]
    s1["send, sendto, sendmsg, sendmmsg, write"]
    s2["sendfile, splice into a socket"]
    s3["io_uring: send, send_zc"]
  end
  subgraph RECV["Receive side"]
    r1["recv, recvfrom, recvmsg, recvmmsg, read"]
    r2["splice out of a socket"]
    r3["io_uring: recv"]
  end
  s1 --> HS["sock_sendmsg_nosec"]
  s2 --> HS
  s3 --> HS
  r1 --> HR["sock_recvmsg_nosec"]
  r3 --> HR
  r2 --> HP["sock_splice_read"]
  HP -->|"protocol has no splice_read: copy_splice_read"| HR
  HS --> BEGIN["net_quota_begin"]
  HR --> BEGIN
  HP -->|"protocol has splice_read"| BEGIN
  G["getsockopt TCP_ZEROCOPY_RECEIVE"] --> ZG["tcp.c hook"]
  ZG --> ZQ{"process has a quota?"}
  ZQ -->|"yes"| ZE["return -EOPNOTSUPP<br/>data stays queued<br/>the program falls back to recv"]
  ZQ -->|"no"| ZN["normal zero-copy receive"]

  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  class ZE bad
  class ZN good
```

`TCP_ZEROCOPY_RECEIVE` reads data straight from the receive queue and never
passes through `sock_recvmsg()`, so on the first hardware run it was not
counted at all. Patch 0006 closes that by refusing the option for a process
that has a quota. Processes without a quota can still use it.

What each path was tested with (`user-code/tests`):

| Path | Test |
|---|---|
| `send`, `recv`, the limit, expiry | `test_basic` |
| `sendmsg`, `recvmsg`, `sendmmsg`, `recvmmsg`, `sendfile`, `splice` both ways, `MSG_PEEK`, non-blocking, zero-length send, TCP receive rule, IPv6 | `test_calls` |
| UDP, send and receive rule | `test_udp` |
| io_uring send, receive, zero-copy send, oversized send | `test_uring` |
| `TCP_ZEROCOPY_RECEIVE` | `test_zerocopy` |
| threads sharing one quota | `test_threads` |
| loopback and Unix sockets not counted | `test_loopback` |
| a real program stopped at the limit | `test_realprog.sh` |

Not tested: io_uring multishot receive with provided buffers, and io_uring
zero-copy receive.

## 7. When a quota ends

A quota ends in three ways: the timer fires, the process exits, and (not a way
to end it, but a way not to have it) a child is created.

### 7.1 The window ends

```mermaid
sequenceDiagram
  participant T as thread in recv or send
  participant Q as quota record
  participant M as timer
  T->>Q: begin: reserve 500 bytes, ticket carries gen 7
  Note over T: the protocol call is running
  M->>Q: timer fires after period_sec seconds
  Q->>Q: state NONE, limit 0, used 0, gen 8
  Note over T: the next transfer is not charged at all
  T->>Q: end: refund 200 bytes under gen 7
  Q->>Q: gen is now 8, so the refund is skipped
  Note over Q: a quota set later cannot be corrupted by an old ticket
```

After the timer the process is unrestricted until an ancestor sets a new quota,
which starts a new window with a counter from zero. Nothing is renewed
automatically.

### 7.2 The process exits

```mermaid
flowchart TD
  A(["do_exit"]) --> B{"last thread of the process?"}
  B -->|"no"| C["nothing: other threads keep the quota"]
  B -->|"yes"| D["net_quota_exit:<br/>dead = true<br/>clear the quota, gen + 1"]
  D --> E["timer_shutdown_sync<br/>waits for a running timer callback"]
  E --> F["the quota and its timer are gone"]
  F --> G{"a later net_quota call on this pid?"}
  G -->|"yes"| H["return -ESRCH<br/>the target is exiting"]
  G -->|"pid reused by a new process"| I["the new process starts with no quota"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef bad fill:#fde2e4,stroke:#9d0208,color:#6a040f
  class I good
  class H bad
```

`dead` is set even for a process that never had a quota. Otherwise a quota
installed during the exit would arm a timer that nobody stops before the
`signal_struct` is freed.

### 7.3 A child is created: nothing is inherited

```mermaid
flowchart TD
  A(["fork or clone of a restricted process"]) --> B{"CLONE_THREAD?"}
  B -->|"yes, a new thread"| C["shares the parent's signal_struct<br/>so it is under the same quota<br/>and the same counter"]
  B -->|"no, a new process"| D["copy_signal makes a new signal_struct<br/>net_quota_init: state NONE, no timer"]
  D --> E["the child is unrestricted"]
  E --> F{"an ancestor knows the child's pid?"}
  F -->|"yes"| G["it can set a limit on the child<br/>with its own window"]
  F -->|"no"| H["the child stays unrestricted"]

  classDef good fill:#d8f3dc,stroke:#2d6a4f,color:#1b4332
  classDef neutral fill:#e9ecef,stroke:#495057,color:#212529
  class C,G good
  class E,H neutral
```

This is the professor's rule (a child starts with no quota), and it is
intended: a restricted process can fork and do its network work in the child.
`exec` keeps the quota, because it keeps the `signal_struct`; that is why
`nq_run` can set the limit on the child before the command starts.

## 8. Concurrency

All threads of a process share one record, so the record is protected by a
spinlock. The rules:

- The lookups that can sleep (`SIOCINQ`, which may take the socket lock, and the
  sender lookup) run **before** the quota lock is taken.
- The decision and the reservation are made under the lock, in one step, so
  two threads cannot both use the last bytes.
- The bytes are reserved before the transfer and given back after it, so a
  transfer that moves less than it reserved does not eat the quota.
- The timer callback and `net_quota_exit` take the same lock; the exit path
  waits for a running callback.

```mermaid
sequenceDiagram
  participant A as thread A
  participant B as thread B
  participant Q as quota record, limit 20000
  Note over Q: used is 19300, 700 left
  A->>Q: begin: send 700
  Q-->>A: 0, used becomes 20000, reserved 700
  B->>Q: begin: send 700
  Q-->>B: -EDQUOT, nothing left
  Note over A: the protocol call fails with -EAGAIN
  A->>Q: end: moved 0, refund 700
  Q->>Q: used becomes 19300
  B->>Q: begin: send 700 again
  Q-->>B: 0, used becomes 20000
```

`test_threads` shows the result: four threads sending blocks of 700 bytes
against a limit of 20000 together move 19600 bytes, 28 whole blocks, and every
thread ends with `EDQUOT`.

## 9. Worked examples

### 9.1 A real program under a limit

```mermaid
sequenceDiagram
  participant U as user
  participant R as nq_run
  participant D as dd, the command
  participant K as kernel
  participant N as network
  U->>R: nq_run 20000 20 sh -c "exec dd ..."
  R->>D: fork, the child waits
  R->>K: net_quota(child, 20000, 20)
  K-->>R: 0, timer armed for 20 seconds
  R->>D: go, exec replaces the shell with dd
  loop blocks of 1000 bytes
    D->>K: write 1000 bytes to the socket
    K->>K: begin: reserve, 20000 not reached
    K->>N: send
    K-->>D: 1000
  end
  D->>K: write 1000 bytes, the 21st
  K->>K: begin: nothing left
  K-->>D: -EDQUOT
  D->>D: prints "Disk quota exceeded", exits with status 1
  D-->>R: exit status 1
  R-->>U: exit 1, 20000 bytes were sent
```

### 9.2 One quota, step by step

Limit 10000 bytes, window 4 seconds, against an echo server. This is the
sequence of `test_basic`.

| Step | Call | Left before | Result | `used` after |
|---|---|---|---|---|
| 1 | send 3000 | 10000 | 3000 | 3000 |
| 2 | recv, 3000 bytes waiting | 7000 | 3000 | 6000 |
| 3 | send 8000 | 4000 | `-EDQUOT`, does not fit, nothing charged | 6000 |
| 4 | send 4000 | 4000 | 4000 | 10000 |
| 5 | send 100 | 0 | `-EDQUOT`, nothing left | 10000 |
| 6 | recv | 0 | `-EDQUOT`, nothing left | 10000 |
| 7 | wait for the timer | | quota revoked | state NONE |
| 8 | send 1000, recv 5000 | no limit | 1000, 5000 | not counted |

Steps 1 and 2 show that sent and received bytes share one counter. Step 3
shows that an oversized send is refused whole and costs nothing, so step 4 can
still use the exact remainder. Steps 5 and 6 show that both directions stop at
the limit. Steps 7 and 8 show that the timer lifts the limit by itself.

## 10. Related pages

| Page | What it covers |
|---|---|
| [syscall-reference.md](syscall-reference.md) | the system call as a reference: signature, arguments, return values, rules, examples |
| [handoff.md](handoff.md) | what exists, what is proven, what to do next |
| [setup-and-build.md](setup-and-build.md) | building and booting the kernel, and updating an existing build |
| [../user-code/docs/design.md](../user-code/docs/design.md) | behaviour rules, decision log, known limitations |
| [gap/README.md](gap/README.md) | open gaps and the record of the professor's third change |
| [removing-net-quota.md](removing-net-quota.md) | taking the system call out of the kernel source |
| [removing-custom-kernel.md](removing-custom-kernel.md) | removing the installed custom kernel from the machine |
