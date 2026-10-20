// SPDX-License-Identifier: GPL-2.0
/*
 * nq_run: start any program with a network quota.
 *
 *     nq_run <bytes> <seconds> <command> [args...]
 *
 * Only descendants can be restricted, so nq_run starts the command itself:
 * it forks, restricts the child, and only then lets the child exec the
 * command. The quota survives exec, and children the command forks inherit
 * their own copy of it. The command does not need to know about quotas: it
 * just sees send/recv fail with "Disk quota exceeded" (EDQUOT) at the limit.
 *
 *     nq_run 50000 20 curl -o /dev/null http://example.org/big-file
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "net_quota_user.h"

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
	unsigned long long bytes, secs;
	char msg[256], go = 'g';
	int fd[2], status;
	pid_t child;

	if (argc < 4 || parse_u64(argv[1], ULLONG_MAX, &bytes) ||
	    parse_u64(argv[2], UINT_MAX, &secs)) {
		fprintf(stderr, "usage: %s <bytes> <seconds> <command> [args...]\n",
			argv[0]);
		return 2;
	}
	if (pipe(fd)) {
		perror("pipe");
		return 1;
	}

	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	if (child == 0) {
		close(fd[1]);
		if (read(fd[0], &go, 1) != 1)	/* wait until the quota is set */
			_exit(126);
		close(fd[0]);
		execvp(argv[3], &argv[3]);
		perror(argv[3]);
		_exit(127);
	}
	close(fd[0]);

	if (nq_set_quota(child, bytes, (unsigned int)secs) != 0) {
		nq_perror(errno, child, bytes);
		kill(child, SIGKILL);
		waitpid(child, &status, 0);
		return 1;
	}
	fprintf(stderr, "%s\n", nq_strerror(0, child, bytes, msg, sizeof(msg)));

	if (write(fd[1], &go, 1) != 1)
		perror("write");
	close(fd[1]);

	if (waitpid(child, &status, 0) < 0)
		return 1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 128 + WTERMSIG(status);
}
