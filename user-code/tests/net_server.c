// SPDX-License-Identifier: GPL-2.0
/*
 * net_server: tiny TCP echo server for the quota tests.
 *
 *     net_server <bind-ip> <port>
 *
 * Every connection is served by its own child process, which echoes what it
 * receives until the peer closes. Run it in the other network namespace
 * (see setup_netns.sh) so that the traffic is not loopback.
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

int main(int argc, char **argv)
{
	struct sockaddr_in addr = { .sin_family = AF_INET };
	int one = 1, lfd;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <bind-ip> <port>\n", argv[0]);
		return 2;
	}
	if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
		fprintf(stderr, "bad address: %s\n", argv[1]);
		return 2;
	}
	addr.sin_port = htons((unsigned short)atoi(argv[2]));

	signal(SIGCHLD, SIG_IGN);	/* no zombies */
	signal(SIGPIPE, SIG_IGN);

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
	    bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) || listen(lfd, 16)) {
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
