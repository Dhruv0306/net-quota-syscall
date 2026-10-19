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
│   ├── nq_cli.c            command line: nq_cli <pid> <bytes> <seconds>
│   └── nq_demo.c           direct syscall() and wrapper side by side (course add2Num style)
├── tests/
│   ├── test_errors.c       every error path and message, no network needed
│   ├── test_basic.c        parent, child, grandchild: limit, partial, EDQUOT, expiry
│   ├── test_inherit.c      a forked child inherits its own quota
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
