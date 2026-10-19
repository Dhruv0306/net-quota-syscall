// SPDX-License-Identifier: GPL-2.0
/*
 * test_inherit: a child forked by a restricted process inherits its own
 * quota (same limit, fresh counter).
 *
 *   test (parent) --fork--> A (restricted) --fork--> B (inherits)
 *
 * Checks: the parent sees "already set" on B without ever restricting it, and
 * A and B can each move LIMIT bytes, because they have separate counters.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  6000ULL
#define PERIOD 30U

/* Send until the quota stops us; expect exactly LIMIT bytes, then EDQUOT. */
static int exhaust(const struct server_args *srv, const char *who)
{
	uint64_t sent;
	int fd, err;

	fd = tc_connect(srv);
	CHECK(fd >= 0, "%s connected", who);
	if (fd < 0)
		return 1;
	sent = tc_send_until_error(fd, 1500, LIMIT * 4, &err);
	CHECK(sent == LIMIT, "%s sent %llu of its own %llu bytes before the limit",
	      who, (unsigned long long)sent, LIMIT);
	CHECK(err == NQ_E_QUOTA_EXCEEDED, "%s then got EDQUOT", who);
	if (err)
		nq_perror(err, getpid(), LIMIT);
	close(fd);
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct server_args srv;
	int goa[2], pidp[2], gob[2];
	pid_t a, b = 0;
	int rc, e;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(goa) || pipe(pidp) || pipe(gob)) {
		perror("pipe");
		return 2;
	}

	printf("test_inherit: limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	a = fork();
	if (a == 0) {			/* A */
		pid_t bc;
		int rb;

		tc_pipe_wait(goa[0]);	/* quota is set before B exists */
		bc = fork();
		if (bc == 0) {		/* B */
			tc_pipe_wait(gob[0]);
			_exit(exhaust(&srv, "B"));
		}
		if (write(pidp[1], &bc, sizeof(bc)) != sizeof(bc))
			_exit(2);
		rb = exhaust(&srv, "A");
		_exit(rb | tc_wait_exit(bc));
	}

	rc = nq_set_quota(a, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on child A (%d)", (int)a);
	tc_pipe_signal(goa[1]);

	if (read(pidp[0], &b, sizeof(b)) != sizeof(b)) {
		perror("read pid");
		return 2;
	}
	rc = nq_set_quota(b, 999, 5);
	e = errno;
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET,
	      "grandchild B (%d) already has an inherited quota", (int)b);

	tc_pipe_signal(gob[1]);
	rc = tc_wait_exit(a);
	CHECK(rc == 0, "A and B each used their own full limit");

	printf("test_inherit: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
