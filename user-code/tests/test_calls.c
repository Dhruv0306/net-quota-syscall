// SPDX-License-Identifier: GPL-2.0
/*
 * test_calls: every way of moving bytes through a socket is counted.
 *
 * test_basic only uses send() and recv(). This test runs the other call
 * paths of a restricted process, one per grandchild with its own quota, and
 * each time asks the kernel how much quota is really left (a probe: a send
 * of "left + 1" bytes must be refused, a send of "left" bytes must go
 * through). If a path were not counted, more than "left" bytes would still be
 * allowed and the probe says so.
 *
 *   sendmsg / recvmsg          two-buffer messages
 *   sendmmsg / recvmmsg        three messages in one call
 *   sendfile                   file to socket; and one larger than the quota
 *   splice                     socket to pipe, and pipe to socket
 *   MSG_PEEK                   not counted, and not refused at the limit
 *   non-blocking recv          EAGAIN on an empty socket costs nothing
 *   IPv6                       only with --server6 <ip6>, TCP echo server
 *
 * Usage: test_calls --server <ip> --port <port> [--server6 <ip6>]
 * io_uring has its own program, test_uring.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "net_quota_user.h"
#include "test_common.h"

static struct server_args g_srv;
static const char *g_srv6;

struct tcase {
	const char *name;
	uint64_t limit;
	int (*fn)(void);
};

static int run_case(void *arg)
{
	const struct tcase *t = arg;

	return t->fn();
}

static int open_srv(void)
{
	int fd = tc_connect(&g_srv);

	CHECK(fd >= 0, "connected to %s:%u", g_srv.ip, g_srv.port);
	return fd;
}

static void expect_left(int fd, size_t left, const char *what)
{
	size_t moved;
	enum tc_probe p = tc_probe_remaining(fd, left, &moved);

	CHECK(p == TC_PROBE_COUNTED, "%s: exactly %zu bytes were left", what, left);
	if (p == TC_PROBE_NOT_COUNTED)
		printf("       %s moved bytes the quota did not see (%zu more were allowed)\n",
		       what, moved);
}

static void fill(char *b, size_t n, char c)
{
	memset(b, c, n);
}

static int t_msg(void)
{
	char a[1000], b[2000], rb[3000];
	struct iovec iov[2] = { { a, sizeof(a) }, { b, sizeof(b) } };
	struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2 };
	size_t got = 0;
	ssize_t n;
	int fd = open_srv();

	if (fd < 0)
		return 1;
	fill(a, sizeof(a), 'a');
	fill(b, sizeof(b), 'b');
	n = sendmsg(fd, &m, 0);
	CHECK(n == 3000, "sendmsg of two buffers sent 3000 bytes (got %zd)", n);

	while (got < 3000) {
		size_t rem = 3000 - got, first = rem < 1000 ? rem : 1000;
		struct iovec ri[2] = { { rb, first }, { rb + first, rem - first } };
		struct msghdr rm = { .msg_iov = ri, .msg_iovlen = rem > first ? 2 : 1 };

		n = recvmsg(fd, &rm, 0);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	CHECK(got == 3000, "recvmsg into two buffers got the 3000 echoed bytes (got %zu)", got);
	expect_left(fd, 1000, "sendmsg + recvmsg");
	close(fd);
	return 0;
}

static int t_mmsg(void)
{
	char data[3000], rbuf[3][1000];
	struct mmsghdr mm[3];
	struct iovec iv[3];
	size_t got = 0;
	int fd = open_srv(), n, ok = 1;

	if (fd < 0)
		return 1;
	fill(data, sizeof(data), 'm');
	memset(mm, 0, sizeof(mm));
	for (int i = 0; i < 3; i++) {
		iv[i].iov_base = data + i * 1000;
		iv[i].iov_len = 1000;
		mm[i].msg_hdr.msg_iov = &iv[i];
		mm[i].msg_hdr.msg_iovlen = 1;
	}
	n = sendmmsg(fd, mm, 3, 0);
	for (int i = 0; i < 3 && n == 3; i++)
		ok &= mm[i].msg_len == 1000;
	CHECK(n == 3 && ok, "sendmmsg sent three messages of 1000 bytes (got %d)", n);

	while (got < 3000) {
		struct mmsghdr rm[3];
		struct iovec ri[3];

		memset(rm, 0, sizeof(rm));
		for (int i = 0; i < 3; i++) {
			ri[i].iov_base = rbuf[i];
			ri[i].iov_len = sizeof(rbuf[i]);
			rm[i].msg_hdr.msg_iov = &ri[i];
			rm[i].msg_hdr.msg_iovlen = 1;
		}
		n = recvmmsg(fd, rm, 3, MSG_WAITFORONE, NULL);
		if (n <= 0)
			break;
		for (int i = 0; i < n; i++)
			got += rm[i].msg_len;
	}
	CHECK(got == 3000, "recvmmsg got the 3000 echoed bytes (got %zu)", got);
	expect_left(fd, 1000, "sendmmsg + recvmmsg");
	close(fd);
	return 0;
}

static int make_file(size_t n)
{
	char path[] = "/tmp/nq_test_XXXXXX";
	char *buf = malloc(n);
	int fd = mkstemp(path);

	if (fd < 0 || !buf)
		return -1;
	unlink(path);
	fill(buf, n, 'f');
	if (write(fd, buf, n) != (ssize_t)n) {
		close(fd);
		fd = -1;
	}
	free(buf);
	return fd;
}

static int t_sendfile(void)
{
	size_t sent = 0;
	off_t off = 0;
	int fd = open_srv(), ffd = make_file(3000);

	if (fd < 0 || ffd < 0)
		return 1;
	while (sent < 3000) {
		ssize_t n = sendfile(fd, ffd, &off, 3000 - sent);

		if (n <= 0)
			break;
		sent += (size_t)n;
	}
	CHECK(sent == 3000, "sendfile moved the 3000-byte file (got %zu)", sent);
	expect_left(fd, 1000, "sendfile");
	close(fd);
	return 0;
}

/* A file larger than the quota: whatever happens, never more than the limit. */
static int t_sendfile_big(void)
{
	off_t off = 0;
	ssize_t n;
	size_t moved;
	int e, fd = open_srv(), ffd = make_file(5000);

	if (fd < 0 || ffd < 0)
		return 1;
	n = sendfile(fd, ffd, &off, 5000);
	e = errno;
	CHECK(n == -1 ? e == NQ_E_QUOTA_EXCEEDED : (n >= 0 && n <= 3000),
	      "sendfile of 5000 bytes with 3000 allowed: refused or at most 3000 (got %zd)", n);
	moved = n > 0 ? (size_t)n : 0;
	if (moved <= 3000)
		expect_left(fd, 3000 - moved, "sendfile larger than the quota");
	close(fd);
	return 0;
}

static int t_splice(void)
{
	char buf[3000];
	size_t got = 0;
	int p[2], fd = open_srv();
	ssize_t n;

	if (fd < 0 || pipe(p))
		return 1;
	fill(buf, sizeof(buf), 's');
	n = send(fd, buf, 3000, 0);
	CHECK(n == 3000, "send of 3000 bytes (got %zd)", n);

	while (got < 3000) {			/* socket -> pipe: counted as receive */
		n = splice(fd, NULL, p[1], NULL, 3000 - got, 0);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	CHECK(got == 3000, "splice from the socket into a pipe got 3000 bytes (got %zu)", got);
	for (size_t r = 0; r < got;) {
		n = read(p[0], buf, got - r);
		if (n <= 0)
			break;
		r += (size_t)n;
	}

	fill(buf, 1000, 't');
	CHECK(write(p[1], buf, 1000) == 1000, "1000 bytes written into the pipe");
	n = splice(p[0], NULL, fd, NULL, 1000, 0);	/* pipe -> socket: counted as send */
	CHECK(n == 1000, "splice from the pipe into the socket sent 1000 bytes (got %zd)", n);
	expect_left(fd, 1000, "splice both ways");
	close(fd);
	return 0;
}

static int t_peek(void)
{
	char buf[1000], one[16];
	ssize_t n = 0;
	int e, fd = open_srv();

	if (fd < 0)
		return 1;
	fill(buf, sizeof(buf), 'k');
	CHECK(send(fd, buf, 1000, 0) == 1000, "send of 1000 bytes");
	for (int i = 0; i < 100; i++) {			/* wait for the echo, up to 5 s */
		n = recv(fd, buf, 1000, MSG_PEEK | MSG_DONTWAIT);
		if (n == 1000)
			break;
		poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 50);
	}
	CHECK(n == 1000, "MSG_PEEK sees the 1000 echoed bytes (got %zd)", n);
	n = recv(fd, buf, 1000, MSG_PEEK);
	CHECK(n == 1000, "a second MSG_PEEK sees them again (got %zd)", n);
	n = tc_recv_exact(fd, 1000);
	CHECK(n == 1000, "the real recv takes the 1000 bytes (got %zd)", n);

	/* 6000 limit - 1000 sent - 1000 received: the peeks cost nothing. */
	expect_left(fd, 4000, "two MSG_PEEK calls and one real recv");

	n = recv(fd, one, sizeof(one), MSG_PEEK | MSG_DONTWAIT);
	e = errno;
	CHECK(!(n == -1 && e == NQ_E_QUOTA_EXCEEDED),
	      "MSG_PEEK at the limit is not refused with EDQUOT (got %zd)", n);
	close(fd);
	return 0;
}

static int t_nonblock(void)
{
	char buf[2000];
	int bad = 0, fd = open_srv();
	ssize_t n;

	if (fd < 0)
		return 1;
	fill(buf, sizeof(buf), 'n');
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	for (int i = 0; i < 20; i++) {
		n = recv(fd, buf, 1500, 0);
		if (!(n == -1 && errno == EAGAIN))
			bad++;
	}
	CHECK(bad == 0, "20 non-blocking recv() of 1500 bytes on an empty socket give EAGAIN");
	n = send(fd, buf, 2000, 0);
	CHECK(n == 2000, "the EAGAINs cost nothing: all 2000 bytes can still be sent (got %zd)", n);
	expect_left(fd, 0, "non-blocking recv");
	n = send(fd, buf, 0, 0);
	CHECK(n == -1 && errno == NQ_E_QUOTA_EXCEEDED,
	      "a zero-length send with nothing left returns EDQUOT (intended, see design.md) (got %zd)", n);
	close(fd);
	return 0;
}

static int t_ipv6(void)
{
	char buf[3000];
	int fd = tc_connect6(g_srv6, g_srv.port);
	ssize_t n;

	CHECK(fd >= 0, "connected to [%s]:%u", g_srv6, g_srv.port);
	if (fd < 0)
		return 1;
	fill(buf, sizeof(buf), '6');
	n = send(fd, buf, 3000, 0);
	CHECK(n == 3000, "send of 3000 bytes over IPv6 (got %zd)", n);
	n = tc_recv_exact(fd, 3000);
	CHECK(n == 3000, "recv of the 3000 echoed bytes over IPv6 (got %zd)", n);
	expect_left(fd, 1000, "IPv6 traffic");
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	const struct tcase cases[] = {
		{ "sendmsg / recvmsg", 7000, t_msg },
		{ "sendmmsg / recvmmsg", 7000, t_mmsg },
		{ "sendfile", 4000, t_sendfile },
		{ "sendfile larger than the quota", 3000, t_sendfile_big },
		{ "splice, socket to pipe and pipe to socket", 8000, t_splice },
		{ "MSG_PEEK", 6000, t_peek },
		{ "non-blocking recv", 2000, t_nonblock },
		{ "IPv6", 7000, t_ipv6 },
	};

	signal(SIGPIPE, SIG_IGN);
	if (tc_parse_server_args(argc, argv, &g_srv))
		return 2;
	for (int i = 1; i + 1 < argc; i += 2)
		if (!strcmp(argv[i], "--server6"))
			g_srv6 = argv[i + 1];

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rc;

		if (cases[i].fn == t_ipv6 && !g_srv6) {
			printf("--- %s: skipped (no --server6 given)\n", cases[i].name);
			continue;
		}
		printf("--- %s (limit %llu bytes)\n", cases[i].name,
		       (unsigned long long)cases[i].limit);
		rc = tc_run_restricted(cases[i].limit, 60, run_case, (void *)&cases[i]);
		CHECK(rc == 0, "%s: all checks passed in the restricted process", cases[i].name);
	}

	printf("test_calls: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
