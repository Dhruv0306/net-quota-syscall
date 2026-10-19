# user-code

User-space side of `net_quota`.

```
user-code/
├── include/
│   ├── net_quota_user.h    wrapper API: nq_set_quota(), nq_strerror(), nq_perror()
│   └── net_quota_msgs.h    every message text as an NQ_MSG_* #define
├── src/
│   ├── net_quota.c         the wrapper (raw syscall + message formatting)
│   └── nq_cli.c            command line: nq_cli <pid> <bytes> <seconds>
├── tests/
│   ├── test_errors.c       every error path and message, no network needed
│   ├── test_basic.c        parent, child, grandchild: limit, partial, EDQUOT, expiry
│   ├── test_inherit.c      a forked child inherits its own quota
│   ├── test_common.[ch]    shared helpers
│   ├── net_server.c        echo server for the tests
│   ├── setup_netns.sh      veth + network namespace, so traffic is not loopback
│   └── run_tests.sh        sets everything up, runs the tests, cleans up
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

Why a separate network namespace: traffic to loopback addresses is not
counted, so a test that connects to `127.0.0.1` can never reach its limit.
