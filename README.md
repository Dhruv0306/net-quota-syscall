# net-quota-syscall
A Linux kernel module/patch implementing a custom system call that enforces combined send/receive network byte limits on specified descendant processes for a given time window. Includes process tree validation, dynamic socket hooking (`AF_INET`/`AF_INET6`), kernel timer lifecycle management, and custom user-space error reporting (`EDQUOT`).
