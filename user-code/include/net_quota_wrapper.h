/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macro wrapper in the style of the course's addWrapper.h: the caller never
 * has to remember the system call number.
 *
 *     net_quota(pid, bytes, seconds)    ->  0, or -1 with errno set
 *
 * The number is the one used by the net-quota kernel (syscall_64.tbl).
 * It needs <unistd.h> for syscall(); the function wrapper nq_set_quota()
 * in net_quota_user.h does the same job with proper types.
 */
#ifndef NET_QUOTA_WRAPPER_H
#define NET_QUOTA_WRAPPER_H

#define NQ_SYSCALL_NR 472

#define net_quota(pid, bytes, secs) \
	(syscall(NQ_SYSCALL_NR, (long)(pid), (long)(bytes), (long)(secs)))

#endif /* NET_QUOTA_WRAPPER_H */
