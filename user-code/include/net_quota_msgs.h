/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Human-readable messages for the net_quota return codes.
 *
 * Every NQ_MSG_* below takes the same two printf arguments, in this order:
 *     (int pid, unsigned long long bytes)
 * except NQ_MSG_NOSYS (none) and NQ_MSG_UNKNOWN (int pid, int errnum,
 * const char *errtext).
 */
#ifndef NET_QUOTA_MSGS_H
#define NET_QUOTA_MSGS_H

#define NQ_MSG_OK \
	"Process %d: network data limit of %llu bytes set"
#define NQ_MSG_ALREADY_SET \
	"Process %d: a network data limit is already set, the requested %llu bytes were not applied"
#define NQ_MSG_NOT_DESCENDANT \
	"Process %d is not a descendant of the calling process (requested limit: %llu bytes)"
#define NQ_MSG_SELF_TARGET \
	"Process %d: a process cannot restrict itself (requested limit: %llu bytes)"
#define NQ_MSG_BAD_ARGS \
	"Invalid arguments for process %d (limit %llu bytes): the pid must be a process id, and size and period must be positive and within the allowed range"
#define NQ_MSG_EXCEEDED \
	"Process %d exceeded the maximum network data quota of %llu bytes"
#define NQ_MSG_NOSYS \
	"The net_quota system call is not available: boot the net-quota kernel"
#define NQ_MSG_UNKNOWN \
	"net_quota failed for process %d with error %d (%s)"

#endif /* NET_QUOTA_MSGS_H */
