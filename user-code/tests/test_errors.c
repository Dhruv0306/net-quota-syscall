// SPDX-License-Identifier: GPL-2.0
/*
 * test_errors: every error path of the syscall, plus the message text.
 * Needs no network.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

static pid_t spawn_sleeper(void)
{
	pid_t p = fork();

	if (p == 0) {
		for (;;)
			pause();
	}
	return p;
}

static void check_fail(int rc, int want, const char *what)
{
	CHECK(rc == -1 && errno == want, "%s -> %s", what, strerror(want));
}

int main(void)
{
	pid_t a, b, dead, helper;
	char msg[256];
	int rc, code;

	a = spawn_sleeper();		/* descendant, will be restricted */
	b = spawn_sleeper();		/* sibling of the helper below */

	printf("test_errors\n");

	/* Bad arguments */
	check_fail(nq_set_quota(0, 1000, 5), NQ_E_BAD_ARGS, "pid 0");
	check_fail(nq_set_quota(-5, 1000, 5), NQ_E_BAD_ARGS, "negative pid");
	check_fail(nq_set_quota(a, 0, 5), NQ_E_BAD_ARGS, "size 0");
	check_fail(nq_set_quota(a, 1000, 0), NQ_E_BAD_ARGS, "period 0");
	check_fail(nq_set_quota(a, 1000, NQ_MAX_PERIOD_SEC + 1), NQ_E_BAD_ARGS,
		   "period above the maximum");

	/* Self, unrelated and missing processes */
	check_fail(nq_set_quota(getpid(), 1000, 5), NQ_E_SELF_TARGET, "calling process itself");
	check_fail(nq_set_quota(1, 1000, 5), NQ_E_NOT_DESCENDANT, "pid 1 (an ancestor)");

	dead = fork();
	if (dead == 0)
		_exit(0);
	waitpid(dead, NULL, 0);
	check_fail(nq_set_quota(dead, 1000, 5), NQ_E_NOT_DESCENDANT, "exited process");

	/* A process cannot restrict its sibling */
	helper = fork();
	if (helper == 0)
		_exit(nq_set_quota(b, 1000, 5) == 0 ? 0 : errno);
	code = tc_wait_exit(helper);
	CHECK(code == NQ_E_NOT_DESCENDANT, "a process cannot restrict its sibling (errno %d)", code);

	/* Success, then "already set" */
	rc = nq_set_quota(a, 1000, 60);
	CHECK(rc == 0, "child restricted");
	rc = nq_set_quota(a, 5000, 60);
	CHECK(rc == -1 && errno == NQ_E_ALREADY_SET, "second call: already set, nothing overwritten");

	/* Message text */
	nq_strerror(NQ_E_QUOTA_EXCEEDED, 42, 1000, msg, sizeof(msg));
	CHECK(strstr(msg, "exceeded") && strstr(msg, "42") && strstr(msg, "1000"),
	      "EDQUOT message: \"%s\"", msg);
	nq_strerror(NQ_E_ALREADY_SET, 42, 1000, msg, sizeof(msg));
	CHECK(strstr(msg, "already set"), "already-set message: \"%s\"", msg);
	nq_strerror(ENOSYS, 1, 1, msg, sizeof(msg));
	CHECK(strstr(msg, "not available"), "ENOSYS message: \"%s\"", msg);

	kill(a, SIGKILL);
	kill(b, SIGKILL);
	waitpid(a, NULL, 0);
	waitpid(b, NULL, 0);

	printf("test_errors: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
