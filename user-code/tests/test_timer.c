// SPDX-License-Identifier: GPL-2.0
/*
 * test_timer: the expiry timer on its own.
 *
 * test_basic shows that traffic works again after the period, but a lazy
 * check on the next send would look the same. Here nobody touches the
 * network after the window ends; the parent only asks again with
 * net_quota(): if the quota is really gone, the call succeeds instead of
 * saying "already set".
 *
 *   A: starts using the network (the window starts), then goes idle
 *   P: has a quota but never uses the network (the window never starts)
 *
 * Expected after PERIOD seconds: A can be restricted again (timer fired),
 * P still has its quota (nothing ever started its timer).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  5000ULL
#define PERIOD 3U

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
		fd = tc_connect(&srv);
		if (fd < 0 || tc_send_until_error(fd, 100, 100, &err) != 100)
			_exit(1);
		tc_pipe_signal(ready[1]);	/* window is running now */
		for (;;)
			pause();		/* idle: no more network use */
	}
	p = spawn_pauser();

	rc = nq_set_quota(a, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on A (%d)", (int)a);
	rc = nq_set_quota(p, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on P (%d), which will never use the network", (int)p);

	tc_pipe_signal(go[1]);
	tc_pipe_wait(ready[0]);

	rc = again(a, &e);
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET, "while the window runs: A is still limited");

	printf("  ..   waiting %u s with no network activity\n", PERIOD + 2);
	fflush(stdout);
	sleep(PERIOD + 2);

	rc = again(a, &e);
	CHECK(rc == 0, "after the period: the timer has revoked A's quota (set again, rc %d)", rc);
	rc = again(p, &e);
	CHECK(rc == -1 && e == NQ_E_ALREADY_SET,
	      "P's window never started, so its quota is still there");

	kill(a, SIGKILL);
	kill(p, SIGKILL);
	waitpid(a, NULL, 0);
	waitpid(p, NULL, 0);

	printf("test_timer: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
