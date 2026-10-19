// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "test_common.h"

int g_failures;

int tc_parse_server_args(int argc, char **argv, struct server_args *out)
{
	out->ip = NULL;
	out->port = 0;

	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--server"))
			out->ip = argv[i + 1];
		else if (!strcmp(argv[i], "--port"))
			out->port = (unsigned short)atoi(argv[i + 1]);
	}
	if (!out->ip || !out->port) {
		fprintf(stderr, "usage: %s --server <ip> --port <port>\n"
			"(use a non-loopback address: loopback traffic is not counted)\n",
			argv[0]);
		return -1;
	}
	return 0;
}

int tc_connect(const struct server_args *srv)
{
	struct sockaddr_in addr = { .sin_family = AF_INET,
				    .sin_port = htons(srv->port) };
	int fd;

	if (inet_pton(AF_INET, srv->ip, &addr.sin_addr) != 1)
		return -1;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

uint64_t tc_send_until_error(int fd, size_t chunk, uint64_t cap, int *err)
{
	char *buf = malloc(chunk);
	uint64_t total = 0;

	*err = 0;
	if (!buf) {
		*err = ENOMEM;
		return 0;
	}
	memset(buf, 'q', chunk);
	while (total < cap) {
		ssize_t n = send(fd, buf, chunk, 0);

		if (n < 0) {
			*err = errno;
			break;
		}
		total += (uint64_t)n;
	}
	free(buf);
	return total;
}

ssize_t tc_recv_exact(int fd, size_t n)
{
	char buf[4096];
	size_t got = 0;

	while (got < n) {
		size_t want = n - got < sizeof(buf) ? n - got : sizeof(buf);
		ssize_t r = recv(fd, buf, want, 0);

		if (r < 0)
			return -1;
		if (r == 0) {
			errno = ECONNRESET;
			return -1;
		}
		got += (size_t)r;
	}
	return (ssize_t)got;
}

void tc_pipe_signal(int fd)
{
	char c = 'g';

	if (write(fd, &c, 1) != 1)
		perror("pipe write");
}

void tc_pipe_wait(int fd)
{
	char c;

	if (read(fd, &c, 1) != 1)
		perror("pipe read");
}

int tc_wait_exit(pid_t pid)
{
	int st;

	if (waitpid(pid, &st, 0) < 0)
		return 255;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 255;
}
