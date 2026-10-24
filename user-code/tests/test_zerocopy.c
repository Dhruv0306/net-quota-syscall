// SPDX-License-Identifier: GPL-2.0
/*
 * test_zerocopy: is data read with TCP_ZEROCOPY_RECEIVE counted?
 *
 * That receive path (getsockopt() on a TCP socket whose pages are mapped into
 * the process) takes data straight from the socket's receive queue and does
 * not go through sock_recvmsg(), where net_quota counts. This test finds out
 * what the kernel really does, instead of leaving it as an assumption.
 *
 * A restricted process sends 32768 bytes to the echo server, waits for the
 * echo, then reads it with TCP_ZEROCOPY_RECEIVE (with the copy buffer, so it
 * also works when the pages cannot be mapped), and then probes how much quota
 * is left.
 *
 * The result is a finding, not a pass or fail, and the program exits 0 either
 * way:
 *
 *   FINDING: counted         the bytes read this way were charged: no gap
 *   FINDING: NOT counted     they were not: a restricted process can receive
 *                            data without using up its quota (record it in
 *                            docs/gap/ and design.md limitation 8)
 *
 * It exits 1 only if the set-up itself went wrong, and prints SKIP if this
 * kernel or header does not have the feature.
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
	size_t sent = 0, consumed, left, moved;
	enum tc_probe p;
	int avail = 0, fd = tc_connect(&g_srv);
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
	if (getsockopt(fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &z, &zlen)) {
		printf("  SKIP [%d] getsockopt(TCP_ZEROCOPY_RECEIVE) failed (%s)\n",
		       (int)getpid(), strerror(errno));
		return 0;
	}
	consumed = z.length + (z.copybuf_len > 0 ? (size_t)z.copybuf_len : 0);
	printf("  info [%d] TCP_ZEROCOPY_RECEIVE took %zu bytes (%u mapped, %d copied)\n",
	       (int)getpid(), consumed, z.length, z.copybuf_len);
	if (!consumed) {
		printf("  SKIP [%d] nothing was taken by TCP_ZEROCOPY_RECEIVE\n", (int)getpid());
		return 0;
	}

	left = LIMIT - sent - consumed;		/* what is left if it was counted */
	p = tc_probe_remaining(fd, left, &moved);
	if (p == TC_PROBE_COUNTED) {
		printf("  FINDING [%d]: TCP_ZEROCOPY_RECEIVE is counted (%zu bytes charged)\n",
		       (int)getpid(), consumed);
	} else if (p == TC_PROBE_NOT_COUNTED) {
		printf("  FINDING [%d]: TCP_ZEROCOPY_RECEIVE is NOT counted: %zu bytes were received without being charged\n",
		       (int)getpid(), consumed);
	} else {
		CHECK(0, "the probe gave an unexpected result (neither counted nor clearly uncounted)");
		return 1;
	}
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
	printf("test_zerocopy: %s\n", g_failures ? "FAILED" : "DONE (see the FINDING line)");
#else
	printf("test_zerocopy: SKIP, this system's headers have no TCP_ZEROCOPY_RECEIVE\n");
#endif
	return g_failures ? 1 : 0;
}
