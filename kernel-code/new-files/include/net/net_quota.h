/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Socket-layer hooks of the network data quota (see net_quota/net_quota.c).
 *
 * Usage around any single transfer:
 *
 *	struct net_quota_ticket nqt;
 *	int err = net_quota_begin(sock, msg, NQ_DIR_SEND, 0, &nqt);
 *
 *	if (err)
 *		return err;		(quota used up: -NQ_E_QUOTA_EXCEEDED)
 *	ret = <do the transfer>;
 *	net_quota_end(&nqt, msg, ret);
 *
 * begin() reserves the bytes the transfer may move and, for a stream socket
 * or a receive, shortens the request to what is left of the quota (partial
 * transfer). end() gives back whatever was reserved but not moved. A
 * process without a quota only pays one inlined load and branch.
 */
#ifndef _NET_NET_QUOTA_H
#define _NET_NET_QUOTA_H

#include <linux/net_quota.h>
#include <linux/preempt.h>
#include <linux/sched/signal.h>
#include <linux/socket.h>
#include <linux/uio.h>

struct socket;
struct sockaddr;

enum net_quota_dir {
	NQ_DIR_SEND,
	NQ_DIR_RECV,
};

/**
 * struct net_quota_ticket - state carried from begin() to end()
 * @active:	this transfer is charged against a quota
 * @dir:	send or receive
 * @peer_unknown: receive on an unconnected socket; the source address is
 *		only known after the call, so loopback is checked in end()
 * @gen:	quota generation the reservation was made under
 * @reserved:	bytes reserved against the quota
 * @trimmed:	bytes cut off the iov_iter, added back by end()
 */
struct net_quota_ticket {
	bool			active;
	enum net_quota_dir	dir;
	bool			peer_unknown;
	u32			gen;
	u64			reserved;
	size_t			trimmed;
};

/* Out-of-line slow paths, in net_quota/net_quota.c */
int __net_quota_begin(struct socket *sock, const struct sockaddr *peer,
		      int peerlen, size_t *len, enum net_quota_dir dir,
		      int flags, struct net_quota_ticket *t);
void __net_quota_end(struct net_quota_ticket *t, const struct sockaddr *peer,
		     int peerlen, long ret);

/* True if the calling task belongs to a process that has a quota. */
static inline bool net_quota_active(void)
{
	return in_task() &&
	       READ_ONCE(current->signal->net_quota.state) != NQ_STATE_NONE;
}

/* Variant for callers that only have a length, such as splice_read. */
static inline int net_quota_begin_len(struct socket *sock, size_t *len,
				      enum net_quota_dir dir, int flags,
				      struct net_quota_ticket *t)
{
	t->active = false;
	t->trimmed = 0;
	if (likely(!net_quota_active()))
		return 0;
	return __net_quota_begin(sock, NULL, 0, len, dir, flags, t);
}

static inline void net_quota_end_len(struct net_quota_ticket *t, long ret)
{
	if (likely(!t->active))
		return;
	__net_quota_end(t, NULL, 0, ret);
}

/* Variant for sendmsg/recvmsg style transfers described by a msghdr. */
static inline int net_quota_begin(struct socket *sock, struct msghdr *msg,
				  enum net_quota_dir dir, int flags,
				  struct net_quota_ticket *t)
{
	size_t left, len;
	int ret;

	t->active = false;
	t->trimmed = 0;
	if (likely(!net_quota_active()))
		return 0;

	left = msg_data_left(msg);
	len = left;
	/* On receive, msg_name is an output buffer: do not read it yet. */
	ret = __net_quota_begin(sock,
				dir == NQ_DIR_SEND ? msg->msg_name : NULL,
				dir == NQ_DIR_SEND ? msg->msg_namelen : 0,
				&len, dir, flags, t);
	if (!ret && t->active && len < left) {
		t->trimmed = left - len;
		iov_iter_truncate(&msg->msg_iter, len);
	}
	return ret;
}

static inline void net_quota_end(struct net_quota_ticket *t,
				 struct msghdr *msg, long ret)
{
	if (likely(!t->active))
		return;
	if (t->trimmed)
		iov_iter_reexpand(&msg->msg_iter,
				  iov_iter_count(&msg->msg_iter) + t->trimmed);
	/* After a receive, msg_name holds the sender's address (or is NULL). */
	__net_quota_end(t,
			t->dir == NQ_DIR_RECV ? msg->msg_name : NULL,
			t->dir == NQ_DIR_RECV ? msg->msg_namelen : 0, ret);
}

#endif /* _NET_NET_QUOTA_H */
