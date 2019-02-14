#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Arachne/Arachne.h"
#include "Arachne/DefaultCorePolicy.h"
#include "common.h"
#include "memcached.h"

#define BUFSIZE 2048
#define CONFIG_MAX_EVENTS 1
#define BACKLOG 8192

struct payload {
	uint64_t work_iterations;
	uint64_t index;
};

struct conn {
	int fd;
	int buf_head;
	int buf_tail;
	unsigned char buf[BUFSIZE];

	/* similar to Arachne memcache, this indicates if a connection is
	   already being handled by an existing thread, or if it is done. */
	bool finished;
};

static int epollfd;
struct sockaddr_in udp_sin;

/* return 1 if we should yield and try again later, 0 otherwise */
static int should_yield(ssize_t ret)
{
	if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 1;
	return 0;
}

static int avail_bytes(struct conn *conn)
{
	return conn->buf_tail - conn->buf_head;
}

static int recv_exactly(struct conn *conn, void *buf, size_t size)
{
	ssize_t ret;
	if (avail_bytes(conn) < (int) size) {
		if (conn->buf_head) {
			memmove(conn->buf, &conn->buf[conn->buf_head], avail_bytes(conn));
			conn->buf_tail -= conn->buf_head;
			conn->buf_head = 0;
		}
		while (conn->buf_tail < (int) size) {
			assert(BUFSIZE - conn->buf_tail > 0);
			ret = recv(conn->fd, &conn->buf[conn->buf_tail], BUFSIZE - conn->buf_tail, 0);
			if (should_yield(ret))
				return -1;
			else if (ret <= 0)
				return ret;
			conn->buf_tail += ret;
		}
	}
	memcpy(buf, &conn->buf[conn->buf_head], size);
	conn->buf_head += size;

	return 1;
}

static int send_exactly(struct conn *conn, void *buf, size_t size)
{
	ssize_t ret;
	char *cbuf = (char *) buf;
	size_t bytes_sent = 0;

	while (bytes_sent < size) {
		ret = send(conn->fd, &cbuf[bytes_sent], size - bytes_sent, MSG_NOSIGNAL);
		if (should_yield(ret))
			Arachne::yield();
		else if (ret <= 0)
			return ret;
		else
			bytes_sent += ret;
	}

	return 1;
}

static int handle_ret(struct conn *conn, ssize_t ret, int line)
{
	if (ret == 0) {
		close(conn->fd);
		/* TODO: should also free conn */
		return 1;
	} else if (ret == -1) {
		switch (errno) {
		case EBADF:
			return 1;
		case EPIPE:
		case ECONNRESET:
			close(conn->fd);
			/* TODO: should also free conn */
			return 1;
		default:
			fprintf(stderr, "Unexpected errno %d at line %d\n", errno, line);
		}
	}

	assert(ret == 1);

	return 0;
}

uint64_t ntohll(uint64_t value)
{
	/* really lazy, assumes a specific endianness */

	const uint32_t high_part = ntohl((uint32_t) (value >> 32));
	const uint32_t low_part = ntohl((uint32_t) (value & 0xFFFFFFFFLL));

	return (((uint64_t) low_part) << 32) | high_part;
}

static void tcp_worker(struct conn *conn)
{
	ssize_t ret = 0;
	uint64_t iterations;
	struct payload payload;

next_request:
	ret = recv_exactly(conn, &payload, sizeof(payload));
	if (should_yield(ret) || handle_ret(conn, ret, __LINE__)) {
		conn->finished = true;
		return;
	}

	iterations = ntohll(payload.work_iterations);
	do_work(iterations);

	ret = send_exactly(conn, &payload, sizeof(payload));
	if (handle_ret(conn, ret, __LINE__)) {
		conn->finished = true;
		return;
	}
	goto next_request;
}

static void epoll_ctl_add(int fd, void *arg)
{
	struct epoll_event ev;

	ev.events = EPOLLIN | EPOLLERR;
	ev.data.fd = fd;
	ev.data.ptr = arg;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl: EPOLL_CTL_ADD");
		exit(EXIT_FAILURE);
	}
}

static void setnonblocking(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	assert(flags >= 0);
	flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	assert(flags >= 0);
}

static void setreuse(int sock)
{
	int one;

	one = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *) &one, sizeof(one))) {
		perror("setsockopt(SO_REUSEPORT)");
		exit(1);
	}

	one = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof(one))) {
		perror("setsockopt(SO_REUSEADDR)");
		exit(1);
	}

}

static void dispatcher_tcp(int port)
{
	struct sockaddr_in sin;
	int sock, one;
	int ret, i, nfds, conn_sock;
	struct epoll_event ev, events[CONFIG_MAX_EVENTS];
	struct conn *conn;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (!sock) {
		perror("socket");
		exit(1);
	}
	printf("dispatcher_tcp\n");
	fflush(stdout);
	setnonblocking(sock);
	setreuse(sock);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);
	sin.sin_port = htons(port);

	if (bind(sock, (struct sockaddr*)&sin, sizeof(sin))) {
		perror("bind");
		exit(1);
	}

	if (listen(sock, BACKLOG)) {
		perror("listen");
		exit(1);
	}

	epollfd = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.u32 = 0;
	ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev);
	assert(!ret);

	while (1) {
		nfds = epoll_wait(epollfd, events, CONFIG_MAX_EVENTS, -1);
		assert(nfds > 0);
		for (i = 0; i < nfds; i++) {
			if (events[i].data.u32 == 0) {
				conn_sock = accept(sock, NULL, NULL);
				if (conn_sock == -1) {
					perror("accept");
					exit(EXIT_FAILURE);
				}
				setnonblocking(conn_sock);
				if (setsockopt(conn_sock, IPPROTO_TCP, TCP_NODELAY, (void *) &one, sizeof(one))) {
					perror("setsockopt(TCP_NODELAY)");
					exit(1);
				}
				conn = (struct conn*) malloc(sizeof *conn);

				conn->fd = conn_sock;
				conn->buf_head = 0;
				conn->buf_tail = 0;
				conn->finished = true;
				epoll_ctl_add(conn_sock, conn);
			} else {
				conn = (struct conn*) events[i].data.ptr;
				if (events[i].events & (EPOLLHUP | EPOLLERR)) {
					close(conn->fd);
					/* TODO: should also free conn */
				} else if (!conn->finished) {
					/* conn is already being handled by another Arachne thread */
					continue;
				} else {
					conn->finished = false;
					if (Arachne::createThread(tcp_worker, conn) ==
					       Arachne::NullThread) {
					  conn->finished = true; /* try again later */
					  //printf("Arachne createThread failed!\n");
					}
				}
			}
		}
	}
}

static void udp_worker(struct conn *conn, int sock)
{
	struct payload p;
	struct sockaddr_in caddr;
	socklen_t caddr_len = sizeof(caddr);
	int conn_sock;

	if (conn)
		sock = conn->fd;

	ssize_t ret = recvfrom(sock, &p, sizeof(p), 0, (struct sockaddr *)&caddr, &caddr_len);
	if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		if (conn)
	  		conn->finished = true;
		return; /* nothing to read */
	} else if (ret != sizeof(p)) {
		printf("udp_worker: error, received wrong size payload %ld, expected %lu for sock %d\n", ret, sizeof(p), sock);
		if (conn)
			conn->finished = true;
		return;
	}

	/* perform fake work */
	uint64_t iterations = ntohll(p.work_iterations);
	do_work(iterations);

	/* send a response */
	ssize_t len = sizeof(p);
	ret = sendto(sock, &p, len, 0, (struct sockaddr *)&caddr, sizeof(caddr));
	if (ret != len)
		printf("udp_worker: udp write failed, ret = %ld\n", ret);
	
	if (!conn) {
		/* setup a new socket for this client addr/port */
		if ((conn_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			printf("socket() failed %d\n", -errno);
			exit(1);
		}
		setnonblocking(conn_sock);
		setreuse(conn_sock);

		if (bind(conn_sock, (struct sockaddr *)&udp_sin, sizeof(udp_sin)) < 0) {
			printf("bind() failed %d\n", -errno);
			exit(1);
		}

		if (connect(conn_sock, (struct sockaddr *)&caddr, sizeof(caddr)) < 0) {
			 printf("connect() failed %d\n", -errno);
			 exit(1);
		}

		/* add this socket to epoll */
		conn = (struct conn *)  malloc(sizeof(struct conn));
		conn->fd = conn_sock;
		epoll_ctl_add(conn_sock, (void *) conn);
	}

	conn->finished = true;
}

static void dispatcher_udp(int port)
{
	int sock, i, ret;
	int nfds;
	struct epoll_event ev, events[CONFIG_MAX_EVENTS];
	struct conn *conn;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (!sock) {
		perror("socket");
		exit(1);
	}

	setnonblocking(sock);
	setreuse(sock);
	
	memset(&udp_sin, 0, sizeof(udp_sin));
	udp_sin.sin_family = AF_INET;
	udp_sin.sin_addr.s_addr = htonl(0);
	udp_sin.sin_port = htons(port);

	if (bind(sock, (struct sockaddr*)&udp_sin, sizeof(udp_sin))) {
		perror("bind");
		exit(1);
	}

	epollfd = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.u32 = 0;
	ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev);
	assert(!ret);
	printf("about to start epoll loop\n");
	fflush(stdout);
	while (1) {
		nfds = epoll_wait(epollfd, events, CONFIG_MAX_EVENTS, -1);
		assert(nfds > 0);
		for (i = 0; i < nfds; i++) {
			if (events[i].data.u32 == 0) {
				/* spawn an Arachne thread to handle the work and setup a new connection */
			  while (Arachne::createThread(udp_worker, (struct conn *) NULL, sock) == Arachne::NullThread) { }
			} else {
				conn = (struct conn *) events[i].data.ptr;

				if (events[i].events & (EPOLLHUP | EPOLLERR)) {
					printf("error!\n");
					close(conn->fd);
					/* TODO: should also free conn */
				} else if (!conn->finished) {
					/* conn is already being handled by another Arachne thread */
					continue;
				} else {
					conn->finished = false;
					/* spawn an Arachne thread to receive, do the work, and send a response */
					while (Arachne::createThread(udp_worker, conn, 0) == Arachne::NullThread) { }
				}
			}
		}
	}
}

void init_arachne(int *argc, const char** argv)
{
	srand48(mytime());

	Arachne::Logger::setLogLevel(Arachne::WARNING);
	Arachne::setErrorStream(stderr);
	Arachne::init(argc, argv);
	/*	reinterpret_cast<Arachne::DefaultCorePolicy*>(Arachne::getCorePolicy())
            ->getEstimator()
            ->setLoadFactorThreshold(0.1);*/
}

void start_arachne_server(int udp, int port)
{
  printf("start_arachne_server\n");
  fflush(stdout);
	/* create arachne dispatch thread */
	if (udp)
		Arachne::createThreadWithClass(Arachne::DefaultCorePolicy::EXCLUSIVE,
					       dispatcher_udp, port);
	else
		Arachne::createThreadWithClass(Arachne::DefaultCorePolicy::EXCLUSIVE,
					       dispatcher_tcp, port);

	Arachne::waitForTermination();
}
