// SPDX-License-Identifier: GPL-2.0
/*
 * net_quota: per-process network data quota.
 *
 * sys_net_quota(pid, data_size, period_sec) lets a process limit how many
 * payload bytes (sent + received, one combined counter) one of its
 * descendants may move over the network. Rules:
 *
 *  - only a descendant (child, grandchild, ...) of the caller can be
 *    targeted, never the caller itself, and only a process, not one thread;
 *  - a quota that is already set is never overwritten (-NQ_E_ALREADY_SET);
 *  - the window of period_sec seconds starts at the process's first use of
 *    the network, and a kernel timer revokes the quota when it ends; after
 *    that the process is unrestricted for good;
 *  - when the limit is reached, send/recv fail with -NQ_E_QUOTA_EXCEEDED
 *    until the window ends; a transfer that only partly fits is shortened
 *    (partial transfer) where that is safe, see __net_quota_begin();
 *  - only AF_INET/AF_INET6 sockets whose peer is not a loopback address
 *    are counted;
 *  - a child created by fork() of a restricted process inherits its own
 *    copy of the quota (same limit and period, fresh counter);
 *  - the quota disappears when the process exits.
 *
 * The state lives in struct signal_struct (shared by all threads of the
 * process) as a struct net_quota_info.
 */

#include <linux/errno.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/jiffies.h>
#include <linux/limits.h>
#include <linux/net.h>
#include <linux/net_quota.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <net/ipv6.h>
#include <net/net_quota.h>
#include <net/sock.h>

#include "net_quota_syscall.h"

/* ------------------------------------------------------------------ */
/* Quota state                                                         */
/* ------------------------------------------------------------------ */

/* Caller holds nq->lock. Back to "no quota"; invalidates in-flight tickets. */
static void nq_clear_locked(struct net_quota_info *nq)
{
	WRITE_ONCE(nq->state, NQ_STATE_NONE);
	nq->limit = 0;
	nq->used = 0;
	nq->period_sec = 0;
	nq->gen++;
}

/* Runs in softirq context when the window ends: revoke the quota. */
static void nq_timer_fn(struct timer_list *t)
{
	struct net_quota_info *nq = timer_container_of(nq, t, timer);

	spin_lock(&nq->lock);
	nq_clear_locked(nq);
	spin_unlock(&nq->lock);
}

/* Called from copy_signal() for every new process. */
void net_quota_init(struct net_quota_info *nq)
{
	spin_lock_init(&nq->lock);
	nq->state = NQ_STATE_NONE;
	nq->gen = 0;
	nq->period_sec = 0;
	nq->limit = 0;
	nq->used = 0;
	timer_setup(&nq->timer, nq_timer_fn, 0);
}

/*
 * Called from copy_signal() after net_quota_init(): a child of a restricted
 * process gets its own quota with the same limit and period, a fresh counter
 * and its own window, which starts at the child's first use of the network.
 * A parent whose quota was already revoked passes on nothing.
 */
void net_quota_inherit(struct net_quota_info *child,
		       struct net_quota_info *parent)
{
	spin_lock_bh(&parent->lock);
	if (parent->state != NQ_STATE_NONE) {
		/* The child is not visible to anyone yet: no lock needed. */
		child->limit = parent->limit;
		child->period_sec = parent->period_sec;
		child->used = 0;
		child->state = NQ_STATE_PENDING;
	}
	spin_unlock_bh(&parent->lock);
}

/* Called from do_exit() when the last thread of the process is exiting. */
void net_quota_exit(struct net_quota_info *nq)
{
	if (READ_ONCE(nq->state) == NQ_STATE_NONE)
		return;

	/* No thread is left to re-arm the timer; stop it, then clear. */
	timer_shutdown_sync(&nq->timer);
	spin_lock_bh(&nq->lock);
	nq_clear_locked(nq);
	spin_unlock_bh(&nq->lock);
}

int net_quota_install(struct net_quota_info *nq, u64 limit,
		      unsigned int period_sec)
{
	int ret = NQ_OK;

	/* secs_to_jiffies() multiplies in unsigned int: make sure it fits. */
	BUILD_BUG_ON((u64)NQ_MAX_PERIOD_SEC * HZ > UINT_MAX);

	spin_lock_bh(&nq->lock);
	if (nq->state != NQ_STATE_NONE) {
		ret = -NQ_E_ALREADY_SET;
	} else {
		nq->limit = limit;
		nq->period_sec = period_sec;
		nq->used = 0;
		nq->gen++;
		WRITE_ONCE(nq->state, NQ_STATE_PENDING);
	}
	spin_unlock_bh(&nq->lock);
	return ret;
}

/* ------------------------------------------------------------------ */
/* Which transfers are counted                                         */
/* ------------------------------------------------------------------ */

static bool nq_sockaddr_is_loopback(const struct sockaddr *sa, int len)
{
	const struct in6_addr *a6;

	if (!sa)
		return false;

	switch (sa->sa_family) {
	case AF_INET:
		if (len < (int)sizeof(struct sockaddr_in))
			return false;
		return ipv4_is_loopback(((const struct sockaddr_in *)sa)->sin_addr.s_addr);
	case AF_INET6:
		if (len < (int)offsetofend(struct sockaddr_in6, sin6_addr))
			return false;
		a6 = &((const struct sockaddr_in6 *)sa)->sin6_addr;
		return ipv6_addr_loopback(a6) || ipv6_addr_v4mapped_loopback(a6);
	default:
		return false;
	}
}

/*
 * Is this transfer subject to the quota? Only IPv4/IPv6 sockets whose peer
 * is not loopback. @peer is the explicit destination of a send, if any.
 * Sets *peer_unknown for a receive on an unconnected socket: the sender is
 * only known once the data has arrived.
 */
static bool nq_socket_counts(struct socket *sock, const struct sockaddr *peer,
			     int peerlen, enum net_quota_dir dir,
			     bool *peer_unknown)
{
	struct sock *sk = sock->sk;
	bool connected;

	if (!sk)
		return false;

	switch (READ_ONCE(sk->sk_family)) {
	case AF_INET:
		if (peer)
			return !nq_sockaddr_is_loopback(peer, peerlen);
		connected = READ_ONCE(sk->sk_daddr) != 0;
		if (connected)
			return !ipv4_is_loopback(READ_ONCE(sk->sk_daddr));
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		if (peer)
			return !nq_sockaddr_is_loopback(peer, peerlen);
		connected = !ipv6_addr_any(&sk->sk_v6_daddr);
		if (connected)
			return !ipv6_addr_loopback(&sk->sk_v6_daddr) &&
			       !ipv6_addr_v4mapped_loopback(&sk->sk_v6_daddr);
		break;
#endif
	default:
		return false;	/* AF_UNIX, netlink, packet, ... */
	}

	*peer_unknown = (dir == NQ_DIR_RECV);
	return true;
}

/* ------------------------------------------------------------------ */
/* Socket hooks                                                        */
/* ------------------------------------------------------------------ */

/**
 * __net_quota_begin - check and reserve quota before one transfer
 * @sock:	socket used
 * @peer:	explicit destination of a send, or NULL
 * @peerlen:	length of @peer
 * @len:	in: bytes the caller wants to move; out: bytes it may move
 * @dir:	send or receive
 * @flags:	recv flags (only MSG_PEEK matters: peeking is not counted)
 * @t:		ticket for net_quota_end()
 *
 * Returns 0 to go ahead (t->active tells whether the transfer is charged),
 * or -NQ_E_QUOTA_EXCEEDED if no quota is left. The window starts here, at
 * the first counted transfer.
 *
 * A request larger than the remaining quota is shortened to it (partial
 * transfer) for receives and for sends on stream sockets. A send on a
 * message-oriented socket (UDP, ...) is refused instead, because cutting a
 * datagram short would silently deliver a corrupt message.
 */
int __net_quota_begin(struct socket *sock, const struct sockaddr *peer,
		      int peerlen, size_t *len, enum net_quota_dir dir,
		      int flags, struct net_quota_ticket *t)
{
	struct net_quota_info *nq = &current->signal->net_quota;
	bool peer_unknown = false;
	u64 remaining;

	if (flags & MSG_PEEK)
		return 0;
	if (!nq_socket_counts(sock, peer, peerlen, dir, &peer_unknown))
		return 0;

	spin_lock_bh(&nq->lock);
	if (nq->state == NQ_STATE_NONE) {	/* revoked in the meantime */
		spin_unlock_bh(&nq->lock);
		return 0;
	}
	if (nq->state == NQ_STATE_PENDING) {	/* first use: start the window */
		WRITE_ONCE(nq->state, NQ_STATE_COUNTING);
		mod_timer(&nq->timer, jiffies + secs_to_jiffies(nq->period_sec));
	}

	remaining = nq->limit - min(nq->limit, nq->used);
	if (!remaining) {
		spin_unlock_bh(&nq->lock);
		return -NQ_E_QUOTA_EXCEEDED;
	}
	if (*len > remaining) {
		if (dir == NQ_DIR_SEND && sock->type != SOCK_STREAM) {
			spin_unlock_bh(&nq->lock);
			return -NQ_E_QUOTA_EXCEEDED;
		}
		*len = remaining;
	}

	/* Reserve now so that concurrent threads cannot overshoot the limit. */
	nq->used += *len;
	t->active = true;
	t->dir = dir;
	t->peer_unknown = peer_unknown;
	t->gen = nq->gen;
	t->reserved = *len;
	spin_unlock_bh(&nq->lock);
	return 0;
}

/**
 * __net_quota_end - settle a transfer started with __net_quota_begin()
 * @t:		ticket from begin
 * @peer:	sender address after a receive, or NULL
 * @peerlen:	length of @peer
 * @ret:	return value of the transfer: bytes moved, or an error
 *
 * Gives back the part of the reservation that was not moved. A receive on
 * an unconnected socket from a loopback sender is not charged at all.
 */
void __net_quota_end(struct net_quota_ticket *t, const struct sockaddr *peer,
		     int peerlen, long ret)
{
	struct net_quota_info *nq = &current->signal->net_quota;
	u64 moved = ret > 0 ? (u64)ret : 0;
	u64 refund;

	if (moved > t->reserved)	/* e.g. MSG_TRUNC reports the full size */
		moved = t->reserved;
	if (moved && t->peer_unknown && nq_sockaddr_is_loopback(peer, peerlen))
		moved = 0;

	refund = t->reserved - moved;
	if (!refund)
		return;

	spin_lock_bh(&nq->lock);
	/* Skip if the quota was revoked, or replaced, since begin(). */
	if (nq->gen == t->gen && nq->state != NQ_STATE_NONE)
		nq->used -= min(nq->used, refund);
	spin_unlock_bh(&nq->lock);
}

/* ------------------------------------------------------------------ */
/* The system call                                                     */
/* ------------------------------------------------------------------ */

/*
 * Is @ancestor an ancestor process of @target? This is the same question as
 * "is @target in the children subtree of @ancestor", but walking up through
 * real_parent costs O(depth), needs no allocation and no tasklist_lock, and
 * copes with the threads of @ancestor (a child's real_parent is the
 * thread that forked it). Caller holds rcu_read_lock().
 */
static bool nq_is_descendant(struct task_struct *target,
			     struct task_struct *ancestor)
{
	struct task_struct *p = target;

	for (;;) {
		struct task_struct *parent = rcu_dereference(p->real_parent);

		if (parent == p)	/* reached init_task */
			return false;
		if (same_thread_group(parent, ancestor))
			return true;
		p = parent;
	}
}

/**
 * sys_net_quota - limit the network data of a descendant process
 * @pid:	process to restrict (in the caller's pid namespace)
 * @data_size:	payload bytes allowed, sent + received combined
 * @period_sec:	length of the window in seconds, 1..NQ_MAX_PERIOD_SEC
 *
 * Returns NQ_OK, or a negative error:
 *   -NQ_E_BAD_ARGS		bad pid/size/period, or @pid is a thread id
 *   -NQ_E_SELF_TARGET		@pid is the calling process
 *   -NQ_E_NOT_DESCENDANT	@pid does not exist or is not a descendant
 *   -NQ_E_ALREADY_SET		the target already has a quota
 */
SYSCALL_DEFINE3(net_quota, pid_t, pid, u64, data_size, unsigned int, period_sec)
{
	struct task_struct *target;
	long ret;

	if (pid <= 0 || data_size == 0 || period_sec == 0 ||
	    period_sec > NQ_MAX_PERIOD_SEC)
		return -NQ_E_BAD_ARGS;

	rcu_read_lock();
	target = find_task_by_vpid(pid);
	if (!target) {
		ret = -NQ_E_NOT_DESCENDANT;
		goto out_unlock;
	}
	if (!thread_group_leader(target)) {
		ret = -NQ_E_BAD_ARGS;
		goto out_unlock;
	}
	if (same_thread_group(target, current)) {
		ret = -NQ_E_SELF_TARGET;
		goto out_unlock;
	}
	if (!nq_is_descendant(target, current)) {
		ret = -NQ_E_NOT_DESCENDANT;
		goto out_unlock;
	}
	/* task->signal stays valid for as long as we hold the task. */
	get_task_struct(target);
	rcu_read_unlock();

	ret = net_quota_install(&target->signal->net_quota, data_size,
				period_sec);
	put_task_struct(target);
	return ret;

out_unlock:
	rcu_read_unlock();
	return ret;
}
