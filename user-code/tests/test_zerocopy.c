// SPDX-License-Identifier: GPL-2.0
/*
 * test_zerocopy: TCP_ZEROCOPY_RECEIVE is refused for a process under a quota.
 *
 * That receive path (getsockopt() on a TCP socket whose pages are mapped into
 * the process) takes data straight from the receive queue and does not go
 * through sock_recvmsg(), where net_quota counts. On the first hardware run
 * this test showed the data was not counted at all (gap G11). The kernel now
 * returns EOPNOTSUPP from that option for a process that has a quota (patch
 * 0006); a program falls back to recv(), which is counted.
 *
 * A restricted process sends 32768 bytes to the echo server, waits for the
 * echo, and then:
 *
 *   - asks for TCP_ZEROCOPY_RECEIVE: it must be refused with EOPNOTSUPP;
 *   - reads the same data with a normal recv(): it is still in the socket and
 *     is counted, which a probe of the quota left confirms.
 *
 * If the option is accepted instead, bytes were taken without being charged,
 * and the test fails. It prints SKIP if this system's headers or kernel do not
 * have the feature at all.
 *
 * Usage: test_zerocopy --server <ip> --port <port>
 */
#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <linux/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "net_quota_user.h"
#include "test_common.h"

#define SENT	32768
#define LIMIT	70000
#define MAPLEN	(SENT)

static struct server_args g_srv;

#ifdef TCP_ZEROCOPY_RECEIVE
static int zc_body(void *arg)
{
	static char copybuf[SENT];
	struct tcp_zerocopy_receive z;
	socklen_t zlen = sizeof(z);
	size_t sent = 0, moved;
	ssize_t got;
	enum tc_probe p;
	int avail = 0, err, fd = tc_connect(&g_srv);
	void *map;
	char *buf = malloc(4096);

	(void)arg;
	CHECK(fd >= 0 && buf, "connected to %s:%u", g_srv.ip, g_srv.port);
	if (fd < 0 || !buf)
		return 1;
	memset(buf, 'z', 4096);
	while (sent < SENT) {
		ssize_t n = send(fd, buf, 4096, 0);

		if (n <= 0)
			break;
		sent += (size_t)n;
	}
	CHECK(sent == SENT, "sent %d bytes (got %zu)", SENT, sent);

	for (int i = 0; i < 100 && avail < SENT; i++) {		/* wait for the echo */
		if (ioctl(fd, FIONREAD, &avail))
			break;
		if (avail < SENT)
			poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 50);
	}
	CHECK(avail >= SENT, "%d echoed bytes are queued in the socket", avail);

	map = mmap(NULL, MAPLEN, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		printf("  SKIP [%d] cannot map the socket (%s): TCP_ZEROCOPY_RECEIVE is not usable here\n",
		       (int)getpid(), strerror(errno));
		return 0;
	}
	memset(&z, 0, sizeof(z));
	z.address = (uint64_t)(uintptr_t)map;
	z.length = MAPLEN;
	z.copybuf_address = (uint64_t)(uintptr_t)copybuf;
	z.copybuf_len = sizeof(copybuf);
	if (getsockopt(fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &z, &zlen) == 0) {
		CHECK(0, "TCP_ZEROCOPY_RECEIVE was accepted for a process under a quota: %u bytes mapped and %d copied without being charged",
		      z.length, z.copybuf_len);
		return 1;
	}
	err = errno;
	if (err != EOPNOTSUPP) {
		printf("  SKIP [%d] getsockopt(TCP_ZEROCOPY_RECEIVE) failed with %s, not the expected refusal\n",
		       (int)getpid(), strerror(err));
		return 0;
	}
	CHECK(1, "TCP_ZEROCOPY_RECEIVE is refused with EOPNOTSUPP under a quota");

	got = tc_recv_exact(fd, SENT);
	CHECK(got == SENT, "the data is still in the socket: a normal recv() reads %d bytes (got %zd)", SENT, got);
	p = tc_probe_remaining(fd, LIMIT - sent - SENT, &moved);
	CHECK(p == TC_PROBE_COUNTED, "that recv() was counted: exactly %zu bytes were left", (size_t)(LIMIT - sent - SENT));
	return 0;
}
#endif

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	if (tc_parse_server_args(argc, argv, &g_srv))
		return 2;
#ifdef TCP_ZEROCOPY_RECEIVE
	{
		int rc = tc_run_restricted(LIMIT, 60, zc_body, NULL);

		CHECK(rc == 0, "set-up and probe in the restricted process went through");
	}
	printf("test_zerocopy: %s\n", g_failures ? "FAILED" : "PASSED");
#else
	printf("test_zerocopy: SKIP, this system's headers have no TCP_ZEROCOPY_RECEIVE\n");
#endif
	return g_failures ? 1 : 0;
}
