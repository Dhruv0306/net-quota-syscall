/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * net_quota system call: user-visible constants.
 *
 * This header is the single source of truth for the return codes of
 * sys_net_quota() and for the error that send/recv report when a quota is
 * used up. The kernel returns the negated value (return -NQ_E_ALREADY_SET);
 * user space sees -1 from syscall() and finds the positive value in errno.
 */
#ifndef _UAPI_LINUX_NET_QUOTA_H
#define _UAPI_LINUX_NET_QUOTA_H

#include <linux/errno.h>

/* sys_net_quota() success */
#define NQ_OK			0

/* sys_net_quota() errors (positive errno values) */
#define NQ_E_BAD_ARGS		EINVAL	/* pid/size/period invalid, or pid is a thread id */
#define NQ_E_SELF_TARGET	EPERM	/* the caller tried to restrict itself */
#define NQ_E_NOT_DESCENDANT	ESRCH	/* pid is not in the caller's process subtree */
#define NQ_E_ALREADY_SET	EEXIST	/* a data limit is already set on that process */

/* send/recv family: the process has used up its quota */
#define NQ_E_QUOTA_EXCEEDED	EDQUOT

/* Longest allowed quota period, in seconds (7 days). */
#define NQ_MAX_PERIOD_SEC	(7 * 24 * 3600)

#endif /* _UAPI_LINUX_NET_QUOTA_H */
