// SPDX-License-Identifier: GPL-2.0
/*
 * test_uring: traffic moved through io_uring is counted like any other.
 *
 * Uses the raw io_uring system calls, so no liburing is needed. Each case runs
 * in its own restricted grandchild, and ends with a probe that asks the kernel
 * how much quota is really left (see tc_probe_remaining()).
 *
 *   IORING_OP_SEND + IORING_OP_RECV   3000 bytes each way
 *   IORING_OP_SEND_ZC                 zero-copy send, 3000 bytes
 *   IORING_OP_SEND, oversized         refused whole with EDQUOT, nothing moved
 *
 * Not covered, because they need set-up this test does not do: multishot
 * receive with provided buffers, and io_uring zero-copy receive (it needs a
 * network card with header/data split and a registered queue; it cannot run on
 * a veth pair). See user-code/docs/design.md, limitation 5.
 *
 * If the kernel has io_uring switched off, or an opcode is missing, the case
 * prints SKIP and does not fail.
 *
 * Usage: test_uring --server <ip> --port <port>
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/io_uring.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include "net_quota_user.h"
#include "test_common.h"

static struct server_args g_srv;

struct ring {
	int fd;
	unsigned *sq_tail, *sq_mask, *sq_array, *cq_head, *cq_tail, *cq_mask;
	struct io_uring_sqe *sqes;
	struct io_uring_cqe *cqes;
};

static int ring_init(struct ring *r)
{
	struct io_uring_params p;
	size_t sq_sz, cq_sz;
	char *sq, *cq;

	memset(&p, 0, sizeof(p));
	r->fd = (int)syscall(__NR_io_uring_setup, 8, &p);
	if (r->fd < 0)
		return -1;
	sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
	cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		if (cq_sz > sq_sz)
			sq_sz = cq_sz;
		cq_sz = sq_sz;
	}
	sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		  r->fd, IORING_OFF_SQ_RING);
	cq = (p.features & IORING_FEAT_SINGLE_MMAP) ? sq :
	     mmap(NULL, cq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		  r->fd, IORING_OFF_CQ_RING);
	r->sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe),
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		       r->fd, IORING_OFF_SQES);
	if (sq == MAP_FAILED || cq == MAP_FAILED || r->sqes == MAP_FAILED)
		return -1;
	r->sq_tail = (unsigned *)(sq + p.sq_off.tail);
	r->sq_mask = (unsigned *)(sq + p.sq_off.ring_mask);
	r->sq_array = (unsigned *)(sq + p.sq_off.array);
	r->cq_head = (unsigned *)(cq + p.cq_off.head);
	r->cq_tail = (unsigned *)(cq + p.cq_off.tail);
	r->cq_mask = (unsigned *)(cq + p.cq_off.ring_mask);
	r->cqes = (struct io_uring_cqe *)(cq + p.cq_off.cqes);
	return 0;
}

/*
 * Runs one send or receive operation to completion. Returns the result of
 * the operation (bytes, or -errno), after also consuming the extra
 * notification entry that a zero-copy send posts.
 */
static int ring_op(struct ring *r, int opcode, int fd, void *buf, unsigned len)
{
	unsigned tail = *r->sq_tail, idx = tail & *r->sq_mask;
	struct io_uring_sqe *sqe = &r->sqes[idx];
	int res = -EIO, got_result = 0, more = 0;

	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = (unsigned char)opcode;
	sqe->fd = fd;
	sqe->addr = (unsigned long)buf;
	sqe->len = len;
	sqe->user_data = 1;
	r->sq_array[idx] = idx;
	__atomic_store_n(r->sq_tail, tail + 1, __ATOMIC_RELEASE);

	if (syscall(__NR_io_uring_enter, r->fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0) < 0)
		return -errno;
	for (;;) {
		unsigned head = *r->cq_head;

		if (head == __atomic_load_n(r->cq_tail, __ATOMIC_ACQUIRE)) {
			if (got_result && !more)
				break;
			if (syscall(__NR_io_uring_enter, r->fd, 0, 1,
				    IORING_ENTER_GETEVENTS, NULL, 0) < 0)
				return -errno;
			continue;
		}
		{
			struct io_uring_cqe *cqe = &r->cqes[head & *r->cq_mask];

			if (!(cqe->flags & IORING_CQE_F_NOTIF)) {
				res = cqe->res;
				got_result = 1;
				more = !!(cqe->flags & IORING_CQE_F_MORE);
			} else {
				more = 0;
			}
		}
		__atomic_store_n(r->cq_head, head + 1, __ATOMIC_RELEASE);
		if (got_result && !more)
			break;
	}
	return res;
}

static int open_ring(struct ring *r, int *fd)
{
	*fd = tc_connect(&g_srv);
	CHECK(*fd >= 0, "connected to %s:%u", g_srv.ip, g_srv.port);
	if (*fd < 0)
		return 1;
	if (ring_init(r)) {
		int e = errno;

		printf("  SKIP [%d] io_uring is not available here (%s)\n",
		       (int)getpid(), strerror(e));
		return -1;
	}
	return 0;
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

static int u_sendrecv(void *arg)
{
	char buf[3000];
	struct ring r;
	size_t got = 0;
	int fd, rc, res;

	(void)arg;
	rc = open_ring(&r, &fd);
	if (rc)
		return rc > 0;
	memset(buf, 'u', sizeof(buf));
	res = ring_op(&r, IORING_OP_SEND, fd, buf, 3000);
	CHECK(res == 3000, "IORING_OP_SEND sent 3000 bytes (got %d)", res);
	while (got < 3000) {
		res = ring_op(&r, IORING_OP_RECV, fd, buf, (unsigned)(3000 - got));
		if (res <= 0)
			break;
		got += (size_t)res;
	}
	CHECK(got == 3000, "IORING_OP_RECV got the 3000 echoed bytes (got %zu)", got);
	expect_left(fd, 1000, "io_uring send + recv");
	return 0;
}

static int u_sendzc(void *arg)
{
	char buf[3000];
	struct ring r;
	int fd, rc, res;

	(void)arg;
	rc = open_ring(&r, &fd);
	if (rc)
		return rc > 0;
	memset(buf, 'z', sizeof(buf));
	res = ring_op(&r, IORING_OP_SEND_ZC, fd, buf, 3000);
	if (res == -EINVAL || res == -EOPNOTSUPP) {
		printf("  SKIP [%d] IORING_OP_SEND_ZC is not supported here (%d)\n",
		       (int)getpid(), res);
		return 0;
	}
	CHECK(res == 3000, "IORING_OP_SEND_ZC sent 3000 bytes (got %d)", res);
	expect_left(fd, 1000, "io_uring zero-copy send");
	return 0;
}

static int u_oversized(void *arg)
{
	char buf[3500];
	struct ring r;
	int fd, rc, res;

	(void)arg;
	rc = open_ring(&r, &fd);
	if (rc)
		return rc > 0;
	memset(buf, 'o', sizeof(buf));
	res = ring_op(&r, IORING_OP_SEND, fd, buf, 3500);
	CHECK(res == -NQ_E_QUOTA_EXCEEDED,
	      "IORING_OP_SEND of 3500 bytes with 3000 allowed is refused whole (got %d)", res);
	expect_left(fd, 3000, "io_uring refused send");
	return 0;
}

int main(int argc, char **argv)
{
	const struct {
		const char *name;
		uint64_t limit;
		int (*fn)(void *);
	} cases[] = {
		{ "IORING_OP_SEND and IORING_OP_RECV", 7000, u_sendrecv },
		{ "IORING_OP_SEND_ZC", 4000, u_sendzc },
		{ "IORING_OP_SEND larger than the quota", 3000, u_oversized },
	};

	signal(SIGPIPE, SIG_IGN);
	if (tc_parse_server_args(argc, argv, &g_srv))
		return 2;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rc;

		printf("--- %s (limit %llu bytes)\n", cases[i].name,
		       (unsigned long long)cases[i].limit);
		rc = tc_run_restricted(cases[i].limit, 60, cases[i].fn, NULL);
		CHECK(rc == 0, "%s: all checks passed in the restricted process", cases[i].name);
	}
	printf("test_uring: %s\n", g_failures ? "FAILED" : "PASSED");
	return g_failures ? 1 : 0;
}
