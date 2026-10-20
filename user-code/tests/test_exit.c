// SPDX-License-Identifier: GPL-2.0
/*
 * test_exit: a quota must disappear with its process, in every state.
 *
 * Four restricted processes die in four different ways:
 *   P1  quota set, never used the network (window not started), killed
 *   C1  window running (timer armed), killed
 *   X1  limit reached (EDQUOT), killed
 *   C2  window running, exits normally by itself
 *
 * If the kernel kept a timer pointing at a freed process, it would fire
 * during the wait below and the kernel log would show a warning or an oops.
 * run_tests.sh compares the kernel log before and after the whole run. This
 * program checks what user space can see: the dead processes are gone, and a
 * new process starts with no quota at all.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  5000ULL
#define PERIOD 2U
#define FRESH  20000ULL

static void gone(pid_t pid, const char *who)
{
	int rc = nq_set_quota(pid, LIMIT, PERIOD);
	int e = errno;

	CHECK(rc == -1 && e == NQ_E_NOT_DESCENDANT, "%s (%d) is gone, no quota can be set on it", who, (int)pid);
}

int main(int argc, char **argv)
{
	struct server_args srv;
	int go[2], ready[2], rc, err, i;
	pid_t p1, c1, x1, c2, f;
	uint64_t sent;

	if (tc_parse_server_args(argc, argv, &srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go) || pipe(ready)) {
		perror("pipe");
		return 2;
	}

	printf("test_exit: limit %llu bytes, period %u s\n", LIMIT, PERIOD);

	p1 = fork();
	if (p1 == 0) {
		for (;;)
			pause();
	}
	c1 = fork();
	if (c1 == 0) {
		int fd;

		tc_pipe_wait(go[0]);
		fd = tc_connect(&srv);
		if (fd < 0 || tc_send_until_error(fd, 100, 100, &err) != 100)
			_exit(1);
		tc_pipe_signal(ready[1]);
		for (;;)
			pause();
	}
	x1 = fork();
	if (x1 == 0) {
		int fd;

		tc_pipe_wait(go[0]);
		fd = tc_connect(&srv);
		if (fd < 0)
			_exit(1);
		sent = tc_send_until_error(fd, 1000, FRESH, &err);
		if (sent != LIMIT || err != NQ_E_QUOTA_EXCEEDED)
			_exit(1);
		tc_pipe_signal(ready[1]);
		for (;;)
			pause();
	}
	c2 = fork();
	if (c2 == 0) {
		int fd;

		tc_pipe_wait(go[0]);
		fd = tc_connect(&srv);
		if (fd < 0 || tc_send_until_error(fd, 100, 100, &err) != 100)
			_exit(1);
		tc_pipe_signal(ready[1]);
		sleep(1);
		_exit(0);		/* normal exit with the timer armed */
	}

	rc = nq_set_quota(p1, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on P1 (%d), which never uses the network", (int)p1);
	rc = nq_set_quota(c1, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on C1 (%d)", (int)c1);
	rc = nq_set_quota(x1, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on X1 (%d)", (int)x1);
	rc = nq_set_quota(c2, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on C2 (%d)", (int)c2);

	for (i = 0; i < 3; i++)
		tc_pipe_signal(go[1]);
	for (i = 0; i < 3; i++)
		tc_pipe_wait(ready[0]);	/* C1, X1, C2 all reached their state */

	kill(p1, SIGKILL);
	kill(c1, SIGKILL);
	kill(x1, SIGKILL);
	waitpid(p1, NULL, 0);
	waitpid(c1, NULL, 0);
	waitpid(x1, NULL, 0);
	rc = tc_wait_exit(c2);
	CHECK(rc == 0, "C2 exited normally");

	gone(p1, "P1 (pending, killed)");
	gone(c1, "C1 (counting, killed)");
	gone(x1, "X1 (exhausted, killed)");
	gone(c2, "C2 (counting, exited)");

	printf("  ..   waiting %u s: a leaked timer would fire now\n", PERIOD + 2);
	fflush(stdout);
	sleep(PERIOD + 2);

	f = fork();
	if (f == 0) {
		int fd = tc_connect(&srv);

		if (fd < 0)
			_exit(1);
		sent = tc_send_until_error(fd, 1000, FRESH, &err);
		_exit(sent == FRESH && err == 0 ? 0 : 1);
	}
	rc = tc_wait_exit(f);
	CHECK(rc == 0, "a new process starts unrestricted: %llu bytes sent, no error", FRESH);

	printf("test_exit: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
