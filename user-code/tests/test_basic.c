// SPDX-License-Identifier: GPL-2.0
/*
 * test_basic: the general case.
 *
 *   test (parent) --fork--> A --fork--> G (grandchild, gets the quota)
 *
 * The parent restricts its grandchild G to LIMIT bytes for PERIOD seconds,
 * then G talks to the echo server and the test checks, in order:
 *   - the limit is applied once, a second call says "already set";
 *   - sent and received bytes share one counter (3000 + 3000 + 4000);
 *   - a request larger than what is left is shortened (partial transfer);
 *   - send and recv both fail with EDQUOT once the limit is reached;
 *   - after the period the quota is gone and traffic flows again.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  10000ULL
#define PERIOD 4U

static int run_grandchild(const struct server_args *srv, int go_fd)
{
	char buf[8000];
	ssize_t n;
	int fd, e;

	memset(buf, 'x', sizeof(buf));
	tc_pipe_wait(go_fd);

	fd = tc_connect(srv);
	CHECK(fd >= 0, "connected to %s:%u", srv->ip, srv->port);
	if (fd < 0)
		return 1;

	n = send(fd, buf, 3000, 0);
	CHECK(n == 3000, "send 3000 bytes inside the quota (got %zd)", n);

	n = tc_recv_exact(fd, 3000);
	CHECK(n == 3000, "recv 3000 echoed bytes, counted on the same counter (got %zd)", n);

	n = send(fd, buf, 8000, 0);
	CHECK(n == 4000, "send 8000: shortened to the 4000 bytes left (got %zd)", n);

	n = send(fd, buf, 100, 0);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED, "send at the limit fails with EDQUOT");
	if (n == -1)
		nq_perror(e, getpid(), LIMIT);

	n = recv(fd, buf, 100, 0);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED, "recv at the limit fails with EDQUOT");

	printf("  ..   [%d] waiting %u s for the quota to expire\n",
	       (int)getpid(), PERIOD + 2);
	fflush(stdout);
	sleep(PERIOD + 2);

	n = send(fd, buf, 1000, 0);
	CHECK(n == 1000, "send works again after the period (got %zd)", n);

	n = tc_recv_exact(fd, 5000);	/* 4000 echoed earlier + 1000 now */
	CHECK(n == 5000, "recv works again after the period (got %zd)", n);

	close(fd);
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct server_args srv;
	int go[2], pidp[2];
	pid_t a, g = 0;
	int rc, e;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go) || pipe(pidp)) {
		perror("pipe");
		return 2;
	}

	printf("test_basic: limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	a = fork();
	if (a == 0) {			/* A */
		pid_t gc = fork();

		if (gc == 0)		/* G */
			_exit(run_grandchild(&srv, go[0]));
		if (write(pidp[1], &gc, sizeof(gc)) != sizeof(gc))
			_exit(2);
		_exit(tc_wait_exit(gc));
	}

	if (read(pidp[0], &g, sizeof(g)) != sizeof(g)) {
		perror("read pid");
		return 2;
	}

	rc = nq_set_quota(g, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on grandchild %d", (int)g);
	rc = nq_set_quota(g, LIMIT, PERIOD);
	e = errno;			/* CHECK() prints and may change errno */
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "second call says the limit is already set");
	if (rc == -1)
		nq_perror(e, g, LIMIT);

	tc_pipe_signal(go[1]);
	rc = tc_wait_exit(a);
	CHECK(rc == 0, "grandchild finished with all checks passed");

	printf("test_basic: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
