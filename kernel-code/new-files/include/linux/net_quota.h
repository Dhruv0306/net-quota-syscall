/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-process network data quota (see net_quota/net_quota.c).
 *
 * One struct net_quota_info lives inside each struct signal_struct, so all
 * threads of a process share a single quota and a single byte counter.
 *
 * This header is included by <linux/sched/signal.h>, so it must stay small
 * and must not include anything that includes <linux/sched/signal.h>.
 */
#ifndef _LINUX_NET_QUOTA_H
#define _LINUX_NET_QUOTA_H

#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <uapi/linux/net_quota.h>

enum net_quota_state {
	NQ_STATE_NONE = 0,	/* no quota; also the zeroed default */
	NQ_STATE_ACTIVE,	/* limit set, timer running, bytes are being charged */
};

/**
 * struct net_quota_info - network quota of one process
 * @lock:	protects every field below; taken with spin_lock_bh() because
 *		the expiry timer runs in softirq context
 * @state:	see enum net_quota_state; read locklessly (READ_ONCE) on the
 *		send/recv fast path, written under @lock
 * @dead:	the process is exiting: no quota may be installed any more, so
 *		no timer can be armed after net_quota_exit() has stopped it
 * @gen:	bumped on every install and every revoke, so a transfer that
 *		started under an old quota never touches a newer one
 * @period_sec:	length of the quota window, in seconds
 * @limit:	payload bytes allowed (sent + received, one combined counter)
 * @used:	payload bytes charged so far, including bytes reserved by
 *		transfers still in flight
 * @timer:	revokes the quota when the window ends; armed in the same
 *		moment the limit is set
 *
 * A quota that is "exhausted" is simply state ACTIVE with used >= limit.
 * When the timer fires the quota is cleared back to NQ_STATE_NONE for good.
 */
struct net_quota_info {
	spinlock_t		lock;
	enum net_quota_state	state;
	bool			dead;
	u32			gen;
	unsigned int		period_sec;
	u64			limit;
	u64			used;
	struct timer_list	timer;
};

/* Lifecycle, called from kernel/fork.c and kernel/exit.c */
void net_quota_init(struct net_quota_info *nq);
void net_quota_inherit(struct net_quota_info *child,
		       struct net_quota_info *parent);
void net_quota_exit(struct net_quota_info *nq);

/* Called by sys_net_quota(); returns 0 or a negative NQ_E_* error. */
int net_quota_install(struct net_quota_info *nq, u64 limit,
		      unsigned int period_sec);

#endif /* _LINUX_NET_QUOTA_H */
