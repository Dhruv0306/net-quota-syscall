# user-code

User-space side of `net_quota`.

```
user-code/
├── include/
│   ├── net_quota_user.h    wrapper API: nq_set_quota(), nq_strerror(), nq_perror()
│   ├── net_quota_wrapper.h macro wrapper like the course's addWrapper.h
│   └── net_quota_msgs.h    every message text as an NQ_MSG_* #define
├── src/
│   ├── net_quota.c         the wrapper (raw syscall + message formatting)
│   ├── nq_cli.c            nq_cli <pid> <bytes> <seconds> (only for your own descendants)
│   ├── nq_run.c            nq_run <bytes> <seconds> <command...>: start any program with a quota
│   └── nq_demo.c           direct syscall() and wrapper side by side (course add2Num style)
├── tests/
│   ├── test_errors.c       every error path and message, no network needed
│   ├── test_basic.c        parent, child, grandchild: limit, partial, EDQUOT, expiry
│   ├── test_inherit.c      a forked child inherits its own quota
│   ├── test_timer.c        the expiry timer fires with no network activity
│   ├── test_loopback.c     loopback and AF_UNIX are not counted
│   ├── test_common.[ch]    shared helpers
│   ├── net_server.c        echo server for the tests
│   ├── setup_netns.sh      optional veth + namespace (needs CONFIG_VETH)
│   └── run_tests.sh        starts the server, runs the tests, cleans up
├── docs/design.md
└── Makefile
```

## Build and run

```bash
make                         # binaries in ./build
sudo ./tests/run_tests.sh    # needs root and the net-quota kernel
./build/nq_cli <pid> <bytes> <seconds>
```

The return codes (`NQ_E_*`) come from the kernel's own UAPI header at
`../kernel-code/new-files/include/uapi/linux/net_quota.h`; override the
location with `make KERNEL_UAPI=/path/to/uapi`.

The kernel returns negative `NQ_E_*` values; `syscall()` turns them into `-1`
and sets `errno`, so user code compares `errno == NQ_E_ALREADY_SET`.

The tests connect to this machine's own non-loopback address (found
automatically, or set `SERVER_IP=...`). Traffic to `127.x` is not counted, so
a test using `127.0.0.1` could never reach its limit. The quota looks at the
destination address, not the interface, so the machine's own address works and
no extra network setup is needed. `setup_netns.sh` is a fallback and needs
`CONFIG_VETH`, which `make defconfig` does not enable.

## Try it on real programs

`nq_cli` can only restrict descendants of itself, so it cannot restrict a
program started from another shell. `nq_run` starts the program for you,
restricts it, then lets it run. Use the address of an echo server
(`./build/net_server <ip> 5001`):

```bash
# 50 000 bytes allowed for 20 s; head tries to push 200 000
./build/nq_run 50000 20 bash -c \
  'exec 3<>/dev/tcp/<ip>/5001; head -c 200000 /dev/zero >&3; echo "head exit: $?"'
# head: error writing 'standard output': Disk quota exceeded
```

An ordinary program knows nothing about quotas, so it just reports the stock
EDQUOT text, "Disk quota exceeded".
