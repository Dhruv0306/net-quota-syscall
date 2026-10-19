// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "net_quota_user.h"

int nq_set_quota(pid_t pid, uint64_t data_size, unsigned int period_sec)
{
	long ret = syscall(__NR_net_quota, (long)pid, (long)data_size,
			   (long)period_sec);

	return ret == 0 ? 0 : -1;
}

const char *nq_strerror(int err, pid_t pid, uint64_t bytes,
			char *buf, size_t buflen)
{
	unsigned long long b = (unsigned long long)bytes;

	switch (err) {
	case 0:
		snprintf(buf, buflen, NQ_MSG_OK, (int)pid, b);
		break;
	case NQ_E_ALREADY_SET:
		snprintf(buf, buflen, NQ_MSG_ALREADY_SET, (int)pid, b);
		break;
	case NQ_E_NOT_DESCENDANT:
		snprintf(buf, buflen, NQ_MSG_NOT_DESCENDANT, (int)pid, b);
		break;
	case NQ_E_SELF_TARGET:
		snprintf(buf, buflen, NQ_MSG_SELF_TARGET, (int)pid, b);
		break;
	case NQ_E_BAD_ARGS:
		snprintf(buf, buflen, NQ_MSG_BAD_ARGS, (int)pid, b);
		break;
	case NQ_E_QUOTA_EXCEEDED:
		snprintf(buf, buflen, NQ_MSG_EXCEEDED, (int)pid, b);
		break;
	case ENOSYS:
		snprintf(buf, buflen, "%s", NQ_MSG_NOSYS);
		break;
	default:
		snprintf(buf, buflen, NQ_MSG_UNKNOWN, (int)pid, err,
			 strerror(err));
		break;
	}
	return buf;
}

void nq_perror(int err, pid_t pid, uint64_t bytes)
{
	char buf[256];

	fprintf(stderr, "%s\n", nq_strerror(err, pid, bytes, buf, sizeof(buf)));
}
