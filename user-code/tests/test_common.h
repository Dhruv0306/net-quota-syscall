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

#endif /* TEST_COMMON_H */
