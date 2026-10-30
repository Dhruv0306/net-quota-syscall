/* SPDX-License-Identifier: GPL-2.0 */
/* Small helpers shared by the net_quota test programs. */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* Number of failed CHECKs in this process. */
extern int g_failures;

#define CHECK(cond, ...)						\
	do {								\
		if (cond) {						\
			printf("  ok   [%d] ", (int)getpid());		\
		} else {						\
			printf("  FAIL [%d] ", (int)getpid());		\
			g_failures++;					\
		}							\
		printf(__VA_ARGS__);					\
		printf("\n");						\
		fflush(stdout);						\
	} while (0)

struct server_args {
	const char *ip;
	unsigned short port;
};

/* Parses "--server <ip> --port <n>". Returns 0, or -1 after printing usage. */
int tc_parse_server_args(int argc, char **argv, struct server_args *out);

/* Blocking TCP connect to the test server. Returns an fd, or -1. */
int tc_connect(const struct server_args *srv);

/*
 * send() chunk-sized blocks until send fails or @cap bytes went out.
 * Returns the bytes sent; *err is the errno of the failing send, or 0 if
 * the cap was reached first.
 */
uint64_t tc_send_until_error(int fd, size_t chunk, uint64_t cap, int *err);

/* recv() exactly @n bytes. Returns @n, or -1 with errno set. */
ssize_t tc_recv_exact(int fd, size_t n);

/* One-byte signalling over a pipe. */
void tc_pipe_signal(int fd);
void tc_pipe_wait(int fd);

/* waitpid(); returns the exit code, or 255 if the child did not exit. */
int tc_wait_exit(pid_t pid);

/*
 * Waits until at least @want bytes are queued for reading on @fd (FIONREAD),
 * for at most @timeout_ms, sleeping between looks. Returns the bytes queued.
 * poll() is not used: it returns at once while any data is queued, so a loop
 * around it spins through its attempts when only part of an echo has arrived.
 */
int tc_wait_queued(int fd, int want, int timeout_ms);

/* Blocking TCP connect over IPv6 to [ip6]:port. Returns an fd, or -1. */
int tc_connect6(const char *ip6, unsigned short port);

/*
 * Runs body(arg) in a grandchild G of the calling process, after the caller
 * has restricted G to @limit bytes for @period seconds (G waits until the
 * limit is set). G exits when body() returns, and its quota goes with it.
 * Returns 0 if body() returned 0 and none of its CHECKs failed, 1 if not, and
 * -1 if the process or the limit could not be set up.
 */
int tc_run_restricted(uint64_t limit, unsigned int period,
		      int (*body)(void *), void *arg);

/* Result of tc_probe_remaining(). */
enum tc_probe {
	TC_PROBE_COUNTED,	/* exactly the expected bytes were left */
	TC_PROBE_NOT_COUNTED,	/* more were left: some traffic was not counted */
	TC_PROBE_OTHER,		/* anything else: less left than expected, or an error */
};

/*
 * Finds out whether the quota of the calling process has exactly @left bytes
 * left: a send of @left + 1 bytes must be refused with EDQUOT, and a send of
 * @left bytes must then go through whole. If the first send is accepted, the
 * kernel had more than @left bytes left, so some earlier traffic was not
 * counted. After TC_PROBE_COUNTED the quota is used up. *moved is set to the
 * bytes the probe itself sent (may be NULL).
 */
enum tc_probe tc_probe_remaining(int fd, size_t left, size_t *moved);

#endif /* TEST_COMMON_H */
