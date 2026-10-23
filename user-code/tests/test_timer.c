// SPDX-License-Identifier: GPL-2.0
/*
 * test_timer: when the window starts, and the timer that ends it.
 *
 * The window starts the moment the limit is set, whether or not the process
 * uses the network, and a kernel timer ends it by itself. Two processes:
 *
 *   A: first uses the network LATE seconds after its limit was set
 *   P: never uses the network at all
 *
 * Both limits must end PERIOD seconds after they were set. The check happens
 * after PERIOD + 1 seconds, nobody touching the network in between: if the
 * window had started at A's first use it would still run for LATE more
 * seconds, and a window that waits for a first use would never end for P.
 * Asking again with net_quota() tells: success means the quota is gone, "already
 * set" means it is still there.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  5000ULL
#define PERIOD 4U
#define LATE   2U

static pid_t spawn_pauser(void)
{
	pid_t p = fork();

	if (p == 0) {
		for (;;)
			pause();
	}
	return p;
}

static int again(pid_t pid, int *err)
{
	int rc = nq_set_quota(pid, LIMIT, PERIOD);

	*err = errno;
	return rc;
}

int main(int argc, char **argv)
{
	struct server_args srv;
	int go[2], ready[2], rc, e;
	pid_t a, p;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go) || pipe(ready)) {
		perror("pipe");
		return 2;
	}

	printf("test_timer: limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	a = fork();
	if (a == 0) {
		int fd, err;

		tc_pipe_wait(go[0]);
		sleep(LATE);			/* first network use, late */
		fd = tc_connect(&srv);
		if (fd < 0 || tc_send_until_error(fd, 100, 100, &err) != 100)
			_exit(1);
		tc_pipe_signal(ready[1]);
		for (;;)
			pause();		/* idle again: no more network use */
	}
	p = spawn_pauser();

	rc = nq_set_quota(a, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on A (%d), which will use the network after %u s", (int)a, LATE);
	rc = nq_set_quota(p, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on P (%d), which will never use the network", (int)p);

	tc_pipe_signal(go[1]);

	rc = again(a, &e);
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "right after setting: A is limited");
	rc = again(p, &e);
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "right after setting: P is limited, with no network use at all");

	tc_pipe_wait(ready[0]);		/* about LATE seconds in: A has just used the network */
	rc = again(a, &e);
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "%u s in, A has used the network and is still limited", LATE);

	printf("  ..   waiting %u s with no network activity\n", PERIOD - LATE + 1);
	fflush(stdout);
	sleep(PERIOD - LATE + 1);	/* now PERIOD + 1 s after the limits were set */

	rc = again(a, &e);
	CHECK(rc == 0, "after %u s: A's quota ended %u s after it was set, not after its first use (set again, rc %d)",
	      PERIOD + 1, PERIOD, rc);
	rc = again(p, &e);
	CHECK(rc == 0, "after %u s: P's quota ended too, although it never used the network (set again, rc %d)",
	      PERIOD + 1, rc);

	kill(a, SIGKILL);
	kill(p, SIGKILL);
	waitpid(a, NULL, 0);
	waitpid(p, NULL, 0);

	printf("test_timer: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
