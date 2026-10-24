// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

int g_failures;

int tc_parse_server_args(int argc, char **argv, struct server_args *out)
{
	out->ip = NULL;
	out->port = 0;

	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--server"))
			out->ip = argv[i + 1];
		else if (!strcmp(argv[i], "--port"))
			out->port = (unsigned short)atoi(argv[i + 1]);
	}
	if (!out->ip || !out->port) {
		fprintf(stderr, "usage: %s --server <ip> --port <port>\n"
			"(use a non-loopback address: loopback traffic is not counted)\n",
			argv[0]);
		return -1;
	}
	return 0;
}

int tc_connect(const struct server_args *srv)
{
	struct sockaddr_in addr = { .sin_family = AF_INET,
				    .sin_port = htons(srv->port) };
	int fd;

	if (inet_pton(AF_INET, srv->ip, &addr.sin_addr) != 1)
		return -1;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

uint64_t tc_send_until_error(int fd, size_t chunk, uint64_t cap, int *err)
{
	char *buf = malloc(chunk);
	uint64_t total = 0;

	*err = 0;
	if (!buf) {
		*err = ENOMEM;
		return 0;
	}
	memset(buf, 'q', chunk);
	while (total < cap) {
		ssize_t n = send(fd, buf, chunk, 0);

		if (n < 0) {
			*err = errno;
			break;
		}
		total += (uint64_t)n;
	}
	free(buf);
	return total;
}

ssize_t tc_recv_exact(int fd, size_t n)
{
	char buf[4096];
	size_t got = 0;

	while (got < n) {
		size_t want = n - got < sizeof(buf) ? n - got : sizeof(buf);
		ssize_t r = recv(fd, buf, want, 0);

		if (r < 0)
			return -1;
		if (r == 0) {
			errno = ECONNRESET;
			return -1;
		}
		got += (size_t)r;
	}
	return (ssize_t)got;
}

void tc_pipe_signal(int fd)
{
	char c = 'g';

	if (write(fd, &c, 1) != 1)
		perror("pipe write");
}

void tc_pipe_wait(int fd)
{
	char c;

	if (read(fd, &c, 1) != 1)
		perror("pipe read");
}

int tc_wait_exit(pid_t pid)
{
	int st;

	if (waitpid(pid, &st, 0) < 0)
		return 255;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 255;
}

int tc_connect6(const char *ip6, unsigned short port)
{
	struct sockaddr_in6 addr = { .sin6_family = AF_INET6,
				     .sin6_port = htons(port) };
	int fd;

	if (inet_pton(AF_INET6, ip6, &addr.sin6_addr) != 1)
		return -1;
	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int tc_run_restricted(uint64_t limit, unsigned int period,
		      int (*body)(void *), void *arg)
{
	int go[2], pidp[2], rc;
	pid_t a, g = 0;

	if (pipe(go) || pipe(pidp)) {
		perror("pipe");
		return -1;
	}
	fflush(stdout);
	a = fork();
	if (a < 0) {
		perror("fork");
		return -1;
	}
	if (a == 0) {			/* A: parent of G, not restricted */
		pid_t gc = fork();

		if (gc == 0) {		/* G: gets the quota */
			int r;

			tc_pipe_wait(go[0]);
			g_failures = 0;
			r = body(arg);
			fflush(stdout);	/* _exit() does not flush */
			_exit(r || g_failures ? 1 : 0);
		}
		if (write(pidp[1], &gc, sizeof(gc)) != sizeof(gc))
			_exit(2);
		_exit(tc_wait_exit(gc));
	}

	if (read(pidp[0], &g, sizeof(g)) != sizeof(g)) {
		perror("read pid");
		return -1;
	}
	rc = nq_set_quota(g, limit, period);
	if (rc) {
		int e = errno;

		nq_perror(e, g, limit);
		kill(g, SIGKILL);
		tc_wait_exit(a);
		return -1;
	}
	tc_pipe_signal(go[1]);
	rc = tc_wait_exit(a);
	return rc == 0 ? 0 : 1;
}

enum tc_probe tc_probe_remaining(int fd, size_t left, size_t *moved)
{
	char *buf = malloc(left + 1);
	enum tc_probe res = TC_PROBE_COUNTED;
	ssize_t n;
	int e;

	if (moved)
		*moved = 0;
	if (!buf)
		return TC_PROBE_OTHER;
	memset(buf, 'p', left + 1);

	n = send(fd, buf, left + 1, 0);
	e = errno;
	if (n >= 0) {
		if (moved)
			*moved = (size_t)n;
		res = TC_PROBE_NOT_COUNTED;
	} else if (e != NQ_E_QUOTA_EXCEEDED) {
		res = TC_PROBE_OTHER;
	} else if (left > 0) {
		n = send(fd, buf, left, 0);
		if (n == (ssize_t)left && moved)
			*moved = left;
		if (n != (ssize_t)left)
			res = TC_PROBE_OTHER;
	}
	free(buf);
	return res;
}
