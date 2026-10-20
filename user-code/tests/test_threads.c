// SPDX-License-Identifier: GPL-2.0
/*
 * test_threads: all threads of a process share one quota.
 *
 * Four threads, each with its own TCP connection, send 700-byte blocks as
 * fast as they can. Together they must move exactly LIMIT bytes: not less
 * (the counter must not lose bytes) and not more (threads must not slip past
 * the limit together). 700 does not divide 20000, so the last transfer is a
 * partial one.
 *
 * Also: a thread id is not a process id, so the quota cannot be put on one.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT    20000ULL
#define PERIOD   30U
#define NTHREADS 4
#define CHUNK    700

static struct server_args g_srv;
static pthread_barrier_t g_bar;
static uint64_t g_sent[NTHREADS];
static int g_err[NTHREADS], g_fd[NTHREADS];
static int g_tid_pipe;

static void *worker(void *arg)
{
	long i = (long)arg;

	if (i == 0) {			/* report the id of a non-leader thread */
		pid_t tid = (pid_t)syscall(SYS_gettid);

		if (write(g_tid_pipe, &tid, sizeof(tid)) != sizeof(tid))
			perror("tid");
	}
	g_fd[i] = tc_connect(&g_srv);
	pthread_barrier_wait(&g_bar);	/* everybody sends at the same moment */
	if (g_fd[i] >= 0)
		g_sent[i] = tc_send_until_error(g_fd[i], CHUNK, LIMIT * 5, &g_err[i]);
	return NULL;
}

static int run_child(int go_fd, int tid_fd)
{
	pthread_t th[NTHREADS];
	uint64_t sum = 0;
	int i, all_edquot = 1;

	g_tid_pipe = tid_fd;
	pthread_barrier_init(&g_bar, NULL, NTHREADS + 1);
	for (i = 0; i < NTHREADS; i++)
		pthread_create(&th[i], NULL, worker, (void *)(long)i);

	tc_pipe_wait(go_fd);		/* the parent has set the quota */
	pthread_barrier_wait(&g_bar);
	for (i = 0; i < NTHREADS; i++)
		pthread_join(th[i], NULL);

	for (i = 0; i < NTHREADS; i++) {
		printf("  ..   [%d] thread %d sent %llu bytes, then errno %d\n",
		       (int)getpid(), i, (unsigned long long)g_sent[i], g_err[i]);
		sum += g_sent[i];
		if (g_err[i] != NQ_E_QUOTA_EXCEEDED)
			all_edquot = 0;
	}
	CHECK(sum == LIMIT, "%d threads together sent %llu bytes: exactly the limit of %llu",
	      NTHREADS, (unsigned long long)sum, LIMIT);
	CHECK(all_edquot, "every thread ended with EDQUOT");
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	int go[2], tidp[2], rc, e;
	pid_t c, tid = 0;

	if (tc_parse_server_args(argc, argv, &g_srv))
		return 2;
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go) || pipe(tidp)) {
		perror("pipe");
		return 2;
	}

	printf("test_threads: limit %llu bytes shared by %d threads\n", LIMIT, NTHREADS);

	c = fork();
	if (c == 0)
		_exit(run_child(go[0], tidp[1]));

	if (read(tidp[0], &tid, sizeof(tid)) != sizeof(tid)) {
		perror("read tid");
		return 2;
	}
	rc = nq_set_quota(tid, LIMIT, PERIOD);
	e = errno;
	CHECK(rc == -1 && e == NQ_E_BAD_ARGS,
	      "thread id %d is rejected: the quota belongs to the process", (int)tid);
	rc = nq_set_quota(c, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on the process %d", (int)c);

	tc_pipe_signal(go[1]);
	rc = tc_wait_exit(c);
	CHECK(rc == 0, "child finished with all checks passed");

	printf("test_threads: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
