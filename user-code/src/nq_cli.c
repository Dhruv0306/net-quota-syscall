// SPDX-License-Identifier: GPL-2.0
/*
 * nq_cli: command-line front end of the net_quota system call.
 *
 *     nq_cli <pid> <bytes> <seconds>
 *
 * Only descendants of this process can be restricted, so the usual use is
 * to start the program to limit from a shell script that runs nq_cli, or
 * to call nq_set_quota() from your own parent program.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "net_quota_user.h"

/* Parse a plain decimal number; reject empty input, signs, junk, overflow. */
static int parse_u64(const char *s, unsigned long long max,
		     unsigned long long *out)
{
	char *end;

	if (!*s || *s == '-' || *s == '+')
		return -1;
	errno = 0;
	*out = strtoull(s, &end, 10);
	if (errno || *end || *out > max)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned long long pid, bytes, secs;
	char msg[256];
	int err;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <pid> <bytes> <seconds>\n", argv[0]);
		return 2;
	}
	if (parse_u64(argv[1], INT_MAX, &pid) ||
	    parse_u64(argv[2], ULLONG_MAX, &bytes) ||
	    parse_u64(argv[3], UINT_MAX, &secs)) {
		fprintf(stderr, "%s: pid, bytes and seconds must be plain positive numbers\n",
			argv[0]);
		return 2;
	}

	if (nq_set_quota((pid_t)pid, bytes, (unsigned int)secs) == 0) {
		printf("%s\n", nq_strerror(0, (pid_t)pid, bytes, msg, sizeof(msg)));
		return 0;
	}
	err = errno;
	nq_perror(err, (pid_t)pid, bytes);
	return 1;
}
