// SPDX-License-Identifier: GPL-2.0
/*
 * net_server: tiny TCP echo server for the quota tests.
 *
 *     net_server <bind-ip> <port> [udp]
 *
 * <bind-ip> may be an IPv6 address (it contains a colon); that works for TCP
 * only.
 *
 * Every connection is served by its own child process, which echoes what it
 * receives until the peer closes. Run it in the other network namespace
 * (see setup_netns.sh) so that the traffic is not loopback.
 *
 * With the extra argument "udp" it is a UDP echo server instead: every
 * datagram is sent straight back to its sender.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void echo_loop(int fd)
{
	char buf[4096];
	ssize_t n;

	while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
		ssize_t off = 0;

		while (off < n) {
			ssize_t w = send(fd, buf + off, (size_t)(n - off), 0);

			if (w < 0)
				return;
			off += w;
		}
	}
}

static int run_udp(const struct sockaddr_in *addr)
{
	char buf[2048];
	struct sockaddr_in peer;
	socklen_t plen;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0 || bind(fd, (const struct sockaddr *)addr, sizeof(*addr))) {
		perror("net_server udp");
		return 1;
	}
	for (;;) {
		ssize_t n;

		plen = sizeof(peer);
		n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &plen);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recvfrom");
			return 1;
		}
		sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, plen);
	}
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr = { .sin_family = AF_INET };
	struct sockaddr_in6 addr6 = { .sin6_family = AF_INET6 };
	struct sockaddr *sa = (struct sockaddr *)&addr;
	socklen_t salen = sizeof(addr);
	int one = 1, lfd, v6;

	if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "udp"))) {
		fprintf(stderr, "usage: %s <bind-ip> <port> [udp]\n", argv[0]);
		return 2;
	}
	v6 = strchr(argv[1], ':') != NULL;
	if (v6 && argc == 4) {
		fprintf(stderr, "udp over IPv6 is not supported\n");
		return 2;
	}
	if (v6) {
		if (inet_pton(AF_INET6, argv[1], &addr6.sin6_addr) != 1) {
			fprintf(stderr, "bad address: %s\n", argv[1]);
			return 2;
		}
		addr6.sin6_port = htons((unsigned short)atoi(argv[2]));
		sa = (struct sockaddr *)&addr6;
		salen = sizeof(addr6);
	} else {
		if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
			fprintf(stderr, "bad address: %s\n", argv[1]);
			return 2;
		}
		addr.sin_port = htons((unsigned short)atoi(argv[2]));
	}
	if (argc == 4)
		return run_udp(&addr);

	signal(SIGCHLD, SIG_IGN);	/* no zombies */
	signal(SIGPIPE, SIG_IGN);

	lfd = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
	if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
	    bind(lfd, sa, salen) || listen(lfd, 16)) {
		perror("net_server");
		return 1;
	}

	for (;;) {
		int fd = accept(lfd, NULL, NULL);

		if (fd < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			return 1;
		}
		if (fork() == 0) {
			close(lfd);
			echo_loop(fd);
			_exit(0);
		}
		close(fd);
	}
}
