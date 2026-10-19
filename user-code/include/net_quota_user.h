/* SPDX-License-Identifier: GPL-2.0 */
/*
 * User-space wrapper for the net_quota system call.
 *
 * The NQ_* codes come from the kernel's own UAPI header, so the kernel and
 * user space can never disagree about them. Build with
 * -I../kernel-code/new-files/include/uapi (the Makefile does this), or use
 * the copy installed by "make headers_install".
 */
#ifndef NET_QUOTA_USER_H
#define NET_QUOTA_USER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <linux/net_quota.h>
#include "net_quota_msgs.h"

/* Syscall number on x86_64 in the net-quota kernel (see patch 0005). */
#ifndef __NR_net_quota
#define __NR_net_quota 472
#endif

/*
 * Limit the network data of descendant process @pid to @data_size payload
 * bytes (sent + received) for @period_sec seconds.
 * Returns 0 on success, or -1 with errno set to one of the NQ_E_* values
 * (ENOSYS if the running kernel has no net_quota).
 */
int nq_set_quota(pid_t pid, uint64_t data_size, unsigned int period_sec);

/*
 * Format the message for @err (an errno value; 0 means success) into @buf.
 * @pid and @bytes are the arguments of the call that failed, or, for
 * NQ_E_QUOTA_EXCEEDED, the process that hit the limit and its limit.
 * Returns @buf.
 */
const char *nq_strerror(int err, pid_t pid, uint64_t bytes,
			char *buf, size_t buflen);

/* nq_strerror() printed to stderr, followed by a newline. */
void nq_perror(int err, pid_t pid, uint64_t bytes);

#endif /* NET_QUOTA_USER_H */
