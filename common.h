#pragma once

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>

static void sigchild(int sig) {
	int sts;
	wait(&sts);
}

static void die(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	exit(256);
}

static void warning(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "warning: ");
	vfprintf(stderr, fmt, ap);
}

struct buf {
	char v[64*1024];
	int b, e;
};

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) < (b) ? (b) : (a))
#endif

static int xrecv(int fd, char* p, size_t sz) {
	int n;
	do {
		n = recv(fd, p, sz, 0);
	} while (n == 0 && errno == EINTR);

	if (n < 0 && errno == EWOULDBLOCK) {
		return 0;
	} else if (n <= 0) {
		return -1;
	} else {
		return n;
	}
}

static int xsend(int fd, const char* p, size_t sz) {
	int n;
	do {
		n = send(fd, p, sz, 0);
	} while (n == 0 && errno == EINTR);

	if (n < 0 && errno == EWOULDBLOCK) {
		return 0;
	} else if (n <= 0) {
		return -1;
	} else {
		return n;
	}
}

static int do_recv(int fd, struct buf* b) {
	int n = xrecv(fd, b->v + b->e, ((b->b <= b->e) ? min(b->b + sizeof(b->v) - 1, sizeof(b->v)) : b->b-1) - b->e);
	if (n < 0) return -1;
	b->e += n;

	if (b->e < sizeof(b->v)) {
		return 0;
	}

	n = xrecv(fd, b->v, b->b - 1);
	if (n < 0) return -1;
	b->e = n;

	return 0;
}

static int do_send(int fd, struct buf* b) {
	int n = xsend(fd, b->v + b->b, ((b->e >= b->b) ? b->e : sizeof(b->v)) - b->b);
	if (n < 0) return -1;
	b->b += n;

	if (b->b < sizeof(b->v)) {
		return 0;
	}

	b->b = 0;
	if (!b->e) {
		return 0;
	}

	n = xsend(fd, b->v, b->e);
	if (n < 0) return -1;
	b->b += n;

	return 0;
}

static void join(int fd1, int fd2) {
	struct pollfd fds[2];
	struct buf b1, b2;

	b1.b = b1.e = 0;
	b2.b = b2.e = 0;

	fcntl(fd1, F_SETFL, fcntl(fd1, F_GETFL) | O_NONBLOCK);
	fcntl(fd2, F_SETFL, fcntl(fd2, F_GETFL) | O_NONBLOCK);

	fds[0].fd = fd1;
	fds[1].fd = fd2;

	for (;;) {
		fds[0].events = fds[1].events = 0;

		if (b1.b != b1.e) {
			fds[1].events |= POLLOUT;
		}
		if (b2.b != b2.e) {
			fds[0].events |= POLLOUT;
		}
		if (((b1.e + 1) % sizeof(b1.v)) != b1.b) {
			fds[0].events |= POLLIN;
		}
		if (((b2.e + 1) % sizeof(b2.v)) != b2.b) {
			fds[1].events |= POLLIN;
		}

		poll(fds, 2, -1);

		if ((fds[0].revents & POLLIN) && do_recv(fd1, &b1)) {
			break;
		}

		if ((fds[1].revents & POLLOUT) && do_send(fd2, &b1)) {
			break;
		}

		if ((fds[1].revents & POLLIN) && do_recv(fd2, &b2)) {
			break;
		}

		if ((fds[0].revents & POLLOUT) && do_send(fd1, &b2)) {
			break;
		}

		if ((fds[0].revents & POLLERR) || (fds[1].revents & POLLERR)) {
			break;
		}
	}
}
