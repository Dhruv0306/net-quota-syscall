/* SPDX-License-Identifier: GPL-2.0 */
/* Entry point of the net_quota system call (see net_quota.c). */
#ifndef _NET_QUOTA_SYSCALL_H
#define _NET_QUOTA_SYSCALL_H

#include <linux/linkage.h>
#include <linux/types.h>

asmlinkage long sys_net_quota(pid_t pid, u64 data_size,
			      unsigned int period_sec);

#endif /* _NET_QUOTA_SYSCALL_H */
