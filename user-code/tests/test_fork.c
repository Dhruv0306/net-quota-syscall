// SPDX-License-Identifier: GPL-2.0
/*
 * test_fork: a child of a restricted process does not inherit the quota.
 *
 *   test (parent P) --fork--> A (restricted) --fork--> B (the child)
 *
 * B is forked after A's limit was set. Expected:
 *   - A is stopped at its limit, with EDQUOT;
 *   - B moves far more than that limit without any error;
 *   - P, an ancestor of B, can then set a quota on B: it succeeds, which would
 *     say "already set" if B had inherited one;
 *   - A still has its quota.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  6000ULL
#define PERIOD 30U
#define BIG    20000ULL

static void report(int fd, int ok)
{
	char c = ok ? 'k' : 'x';

	if (write(fd, &c, 1) != 1)
		perror("report");
}

/* A, restricted: send until the quota stops us; exactly LIMIT bytes, then EDQUOT. */
static int run_restricted(const struct server_args *srv)
{
	uint64_t sent;
	int fd, err;

	fd = tc_connect(srv);
	CHECK(fd >= 0, "A connected");
	if (fd < 0)
		return 1;
	sent = tc_send_until_error(fd, 1500, LIMIT * 4, &err);
	CHECK(sent == LIMIT, "A sent %llu bytes before its limit of %llu",
	      (unsigned long long)sent, LIMIT);
	CHECK(err == NQ_E_QUOTA_EXCEEDED, "A then got EDQUOT");
	if (err)
		nq_perror(err, getpid(), LIMIT);
	close(fd);
	return g_failures ? 1 : 0;
}

/* B, the child: no limit, so BIG bytes (more than three times A's limit) go through. */
static int run_child(const struct server_args *srv)
{
	uint64_t sent;
	int fd, err;

	fd = tc_connect(srv);
	CHECK(fd >= 0, "B connected");
	if (fd < 0)
		return 1;
	sent = tc_send_until_error(fd, 1000, BIG, &err);
	CHECK(sent == BIG && err == 0,
	      "B sent %llu bytes with no error: it has no limit (A's limit is %llu)",
	      (unsigned long long)sent, LIMIT);
	close(fd);
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct server_args srv;
	int goa[2], pidp[2], gob[2], ready[2], fin[2];
	pid_t a, b = 0;
	int rc, e, i;
	char c;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(goa) || pipe(pidp) || pipe(gob) || pipe(ready) || pipe(fin)) {
		perror("pipe");
		return 2;
	}

	printf("test_fork: A's limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	a = fork();
	if (a == 0) {			/* A */
		pid_t bc;
		int ra;

		tc_pipe_wait(goa[0]);	/* A's quota is set before B exists */
		bc = fork();
		if (bc == 0) {		/* B */
			tc_pipe_wait(gob[0]);
			report(ready[1], run_child(&srv) == 0);
			for (;;)
				pause();
		}
		if (write(pidp[1], &bc, sizeof(bc)) != sizeof(bc))
			_exit(2);
		ra = run_restricted(&srv);
		report(ready[1], ra == 0);
		tc_pipe_wait(fin[0]);	/* P is done checking, B must still exist for that */
		kill(bc, SIGKILL);
		waitpid(bc, NULL, 0);
		_exit(ra);
	}

	rc = nq_set_quota(a, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on A (%d)", (int)a);
	tc_pipe_signal(goa[1]);

	if (read(pidp[0], &b, sizeof(b)) != sizeof(b)) {
		perror("read pid");
		return 2;
	}
	tc_pipe_signal(gob[1]);

	for (i = 0; i < 2; i++) {	/* A and B, in either order */
		if (read(ready[0], &c, 1) != 1)
			c = 'x';
		CHECK(c == 'k', "a process reported all its checks passed");
	}

	rc = nq_set_quota(b, LIMIT, PERIOD);
	e = errno;
	CHECK(rc == 0, "B (%d) had no quota: a limit could be set on it now (rc %d, errno %d)", (int)b, rc, e);
	rc = nq_set_quota(a, LIMIT, PERIOD);
	e = errno;
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "A still has its quota");

	tc_pipe_signal(fin[1]);
	rc = tc_wait_exit(a);
	CHECK(rc == 0, "A finished");

	printf("test_fork: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
