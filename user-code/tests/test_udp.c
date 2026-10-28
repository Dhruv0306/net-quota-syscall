// SPDX-License-Identifier: GPL-2.0
/*
 * test_udp: datagram sockets. A datagram cannot be cut short without
 * delivering a corrupt message, so nothing is ever done partly: a send that
 * does not fit is refused whole, and so is a receive when the datagram that is
 * waiting is larger than what is left. The refused datagram stays queued, and
 * a refusal costs nothing. The socket is unconnected (sendto / recvfrom), so
 * the kernel also has to look up the sender before refusing.
 *
 * Limit 2500 bytes, against the UDP echo server (net_server ... udp):
 *   send 1000        ok                          used 1000
 *   recv 1000        ok, the echo counts too     used 2000
 *   send 1000        refused with EDQUOT: only 500 left, nothing is cut off
 *   send 400         ok, the refusal cost nothing  used 2400
 *   send 400         refused: only 100 left
 *   recv (400-byte echo waiting)   refused: 400 is more than the 100 left,
 *                                  for any buffer size; it stays queued
 *   send 100         ok: the refused receives cost nothing   used 2500
 *   recv             EDQUOT, the quota is used up
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "net_quota_user.h"
#include "test_common.h"

#define LIMIT  2500ULL
#define PERIOD 30U

static int run_child(const struct sockaddr_in *srv, int go_fd)
{
	struct timeval tv = { .tv_sec = 2 };
	char buf[1000];
	ssize_t n;
	int fd, e;

	memset(buf, 'd', sizeof(buf));
	tc_pipe_wait(go_fd);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return 1;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#define SEND(len) sendto(fd, buf, (len), 0, (const struct sockaddr *)srv, sizeof(*srv))

	n = SEND(1000);
	CHECK(n == 1000, "send 1000 (got %zd)", n);

	n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
	CHECK(n == 1000, "recv the 1000-byte echo, counted as well (got %zd)", n);

	n = SEND(1000);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED, "send 1000 with 500 left is refused whole, not cut to 500");

	n = SEND(400);
	CHECK(n == 400, "send 400 works: the refused send used up nothing (got %zd)", n);

	n = SEND(400);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED, "send 400 with 100 left is refused");
	if (n == -1)
		nq_perror(e, getpid(), LIMIT);

	/* Wait until the 400-byte echo is queued, so its size is known. */
	poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 2000);

	n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED,
	      "recv with a 400-byte datagram waiting and 100 left is refused whole, not cut (got %zd)", n);

	n = recvfrom(fd, buf, 50, 0, NULL, NULL);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED,
	      "a smaller buffer does not help: the datagram, not the buffer, is judged (got %zd)", n);

	n = recvfrom(fd, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT, NULL, NULL);
	CHECK(n == 400, "the refused datagram is still queued: a peek sees its 400 bytes (got %zd)", n);

	n = SEND(100);
	CHECK(n == 100, "send 100 works: the refused receives used up nothing (got %zd)", n);

	n = recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, NULL, NULL);
	e = errno;
	CHECK(n == -1 && e == NQ_E_QUOTA_EXCEEDED, "recv with the quota used up fails with EDQUOT");

	close(fd);
	return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct server_args args;
	struct sockaddr_in srv = { .sin_family = AF_INET };
	int go[2], rc;
	pid_t c;

	if (tc_parse_server_args(argc, argv, &args))
		return 2;
	srv.sin_port = htons(args.port);
	if (inet_pton(AF_INET, args.ip, &srv.sin_addr) != 1) {
		fprintf(stderr, "bad address %s\n", args.ip);
		return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	if (pipe(go)) {
		perror("pipe");
		return 2;
	}

	printf("test_udp: limit %llu bytes, period %u s (UDP echo server on port %u)\n",
	       LIMIT, PERIOD, args.port);

	c = fork();
	if (c == 0)
		_exit(run_child(&srv, go[0]));

	rc = nq_set_quota(c, LIMIT, PERIOD);
	CHECK(rc == 0, "limit set on child %d", (int)c);
	tc_pipe_signal(go[1]);
	rc = tc_wait_exit(c);
	CHECK(rc == 0, "child finished with all checks passed");

	printf("test_udp: %s\n", g_failures || rc ? "FAILED" : "PASSED");
	return g_failures || rc ? 1 : 0;
}
