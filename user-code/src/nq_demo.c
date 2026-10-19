// SPDX-License-Identifier: GPL-2.0
/*
 * nq_demo: the two ways to call the system call, as in the course's
 * add2Num.c:
 *   (A) directly with syscall() and the system call number, and
 *   (B) through the wrapper, so the number never appears in the program.
 *
 * It forks a child (only a descendant can be restricted), restricts it
 * directly, then tries again through the wrapper, which must report that a
 * limit is already set.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "net_quota_user.h"
#include "net_quota_wrapper.h"

#define MY_SYSCALL 472
#define LIMIT_BYTES 5000
#define PERIOD_SECS 10

int main(void)
{
	char msg[256];
	long res_direct, res_wrapper;
	int err_direct, err_wrapper;
	pid_t child = fork();

	if (child == 0) {
		pause();		/* stand-in for the process to restrict */
		_exit(0);
	}

	res_direct = syscall(MY_SYSCALL, (long)child, (long)LIMIT_BYTES,
			     (long)PERIOD_SECS);
	err_direct = errno;
	res_wrapper = net_quota(child, LIMIT_BYTES, PERIOD_SECS);
	err_wrapper = errno;

	printf("Restricting child %d to %d bytes for %d seconds\n",
	       (int)child, LIMIT_BYTES, PERIOD_SECS);
	printf("Result of direct call:  %ld  %s\n", res_direct,
	       nq_strerror(res_direct ? err_direct : 0, child, LIMIT_BYTES,
			   msg, sizeof(msg)));
	printf("Result of wrapper call: %ld  %s\n", res_wrapper,
	       nq_strerror(res_wrapper ? err_wrapper : 0, child, LIMIT_BYTES,
			   msg, sizeof(msg)));

	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	return 0;
}
