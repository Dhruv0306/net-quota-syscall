// SPDX-License-Identifier: GPL-2.0
/*
 * test_loopback: what must NOT be counted.
 *
 * A restricted child moves far more than its limit over loopback and over a
 * Unix socket pair without a single error, and afterwards still has its
 * whole quota: the next transfer to a non-loopback address is cut off at
 * exactly the limit.
 *
 * Needs a second echo server on 127.0.0.1 (run_tests.sh starts it).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  2000ULL
#define PERIOD 30U
#define BIG    10000ULL

static int run_child(const struct server_args *srv,
		     const struct server_args *loop, int go_fd)
{
	char buf[1000];
	uint64_t sent;
	int sv[2], fd, err, i;

	memset(buf, 'u', sizeof(buf));
	tc_pipe_wait(go_fd);

	fd = tc_connect(loop);
	CHECK(fd >= 0, "connected to loopback %s:%u", loop->ip, loop->port);
	if (fd < 0)
		return 1;
	sent = tc_send_until_error(fd, 1000, BIG, &err);
	CHECK(sent == BIG && err == 0,
	      "%llu bytes over loopback, limit %llu: sent %llu, no error (loopback is not counted)",
	      BIG, LIMIT, (unsigned long long)sent);
	close(fd);

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "unix socket pair created");
	for (i = 0, err = 0; i < (int)(BIG / sizeof(buf)); i++) {
		if (write(sv[0], buf, sizeof(buf)) != (ssize_t)sizeof(buf) ||
		    read(sv[1], buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
			err = errno ? errno : 1;
			break;
		}
	}
	CHECK(err == 0, "%llu bytes over a unix socket and back, no error (AF_UNIX is not counted)", BIG);
	close(sv[0]);
	close(sv[1]);

	fd = tc_connect(srv);
	CHECK(fd >= 0, "connected to %s:%u", srv->ip, srv->port);
	if (fd < 0)
		return 1;
	sent = tc_send_until_error(fd, 1000, BIG, &err);
	CHECK(sent == LIMIT && err == NQ_E_QUOTA_EXCEEDED,
	      "non-loopback: stopped at exactly %llu bytes with EDQUOT (sent %llu); the quota was untouched before",
	      LIMIT, (unsigned long long)sent);
	if (err)
		nq_perror(err, getpid(), LIMIT);
	close(fd);
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct server_args srv, loop;
	int go[2], rc, i;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	loop.ip = "127.0.0.1";
	loop.port = 0;
	for (i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--loop-port"))
			loop.port = (unsigned short)atoi(argv[i + 1]);
	if (!loop.port) {
		fprintf(stderr, "usage: %s --server <ip> --port <p> --loop-port <p>\n", argv[0]);
		return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go)) {
		perror("pipe");
		return 2;
	}

	printf("test_loopback: limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	pid_t c = fork();

	if (c == 0)
		_exit(run_child(&srv, &loop, go[0]));

	rc = nq_set_quota(c, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on child %d", (int)c);
	tc_pipe_signal(go[1]);
	rc = tc_wait_exit(c);
	CHECK(rc == 0, "child finished with all checks passed");

	printf("test_loopback: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
