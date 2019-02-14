#include <errno.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <ixev.h>
#include <mempool.h>

#include "common.h"
#include "memcached.h"

#define ROUND_UP(num, multiple) ((((num) + (multiple) - 1) / (multiple)) * (multiple))

struct payload {
	uint64_t work_iterations;
	uint64_t index;
};

enum spin_conn_state {
	STATE_RECEIVE = 1,
	STATE_SPIN,
	STATE_SEND,
};

struct conn {
	struct ixev_ctx ctx;
	enum spin_conn_state state;
	int partial;
	binary_header_t header;
	struct payload payload;
};

static struct mempool_datastore conn_datastore;
static __thread struct mempool conn_pool;

struct udp_response {
	struct ip_tuple dest;
	struct udp_response_header header;
};

static struct mempool_datastore udp_response_datastore;
static __thread struct mempool udp_response_pool;
__thread int thread_no;
int nr_cpu;

static void handler(struct ixev_ctx *ctx, unsigned int reason);

static bool recv_exactly(struct ixev_ctx *ctx, void *buf, int size)
{
	ssize_t ret;
	char *cbuf = (char *) buf;
	struct conn *conn = container_of(ctx, struct conn, ctx);

	assert(size >= 0);
	while (conn->partial < size) {
		ret = ixev_recv(ctx, &cbuf[conn->partial], size - conn->partial);
		if (ret == -EAGAIN)
			return false;
		if (ret == -EIO) {
			ixev_close(ctx);
			return false;
		}
		assert(ret > 0);
		conn->partial += ret;
	}
	assert(conn->partial == size);
	conn->partial = 0;
	return true;
}

static bool send_exactly(struct ixev_ctx *ctx, void *buf, int size)
{
	ssize_t ret;
	char *cbuf = (char *) buf;
	struct conn *conn = container_of(ctx, struct conn, ctx);

	assert(size >= 0);
	while (conn->partial < size) {
		ret = ixev_send(ctx, &cbuf[conn->partial], size - conn->partial);
		if (ret == -EAGAIN)
			return false;
		assert(ret > 0);
		conn->partial += ret;
	}
	assert(conn->partial == size);
	conn->partial = 0;
	return true;
}

/*static bool drain_exactly(struct ixev_ctx *ctx, int size)
{
	ssize_t ret;
	char buf[4096];
	struct conn *conn = container_of(ctx, struct conn, ctx);

	assert(size >= 0);
	while (conn->partial < size) {
		ret = ixev_recv(ctx, buf, min(sizeof(buf), size - conn->partial));
		if (ret == -EAGAIN)
			return false;
		assert(ret > 0);
		conn->partial += ret;
	}
	assert(conn->partial == size);
	conn->partial = 0;
	return true;
	}*/

uint64_t ntohll(uint64_t value)
{
	/* really lazy, assumes a specific endianness */

	const uint32_t high_part = ntohl((uint32_t) (value >> 32));
	const uint32_t low_part = ntohl((uint32_t) (value & 0xFFFFFFFFLL));

	return (((uint64_t) low_part) << 32) | high_part;
}

static bool drive_machine(struct ixev_ctx *ctx, unsigned int reason)
{
	bool ok;
	struct conn *conn = container_of(ctx, struct conn, ctx);

	switch (conn->state) {
	case STATE_RECEIVE:
		ok = recv_exactly(ctx, &conn->payload, sizeof(conn->payload));
		if (!ok)
			return true;
		conn->state = STATE_SPIN;
		/* fallthrough */
	case STATE_SPIN:
		do_work(ntohll(conn->payload.work_iterations));
		conn->state = STATE_SEND;
		/* fallthrough */
	case STATE_SEND:
		ok = send_exactly(ctx, &conn->payload, sizeof(conn->payload));
		if (!ok)
			return true;
		conn->state = STATE_RECEIVE;
		break;
	default:
		assert(0);
	}

	return false;
}

static void handler(struct ixev_ctx *ctx, unsigned int reason)
{
	bool stop;
	struct conn *conn = container_of(ctx, struct conn, ctx);

	while (1) {
		stop = drive_machine(ctx, reason);
		if (stop)
			break;
	}

	switch (conn->state) {
	case STATE_RECEIVE:
		ixev_set_handler(&conn->ctx, IXEVIN, &handler);
		break;
	case STATE_SPIN:
		assert(0);
	case STATE_SEND:
		ixev_set_handler(&conn->ctx, IXEVOUT, &handler);
		break;
	default:
		assert(0);
	}
}

static struct ixev_ctx *tcp_accept(struct ip_tuple *id)
{
	struct conn *conn = mempool_alloc(&conn_pool);
	assert(conn);
	conn->state = STATE_RECEIVE;
	conn->partial = 0;
	ixev_ctx_init(&conn->ctx);
	ixev_set_handler(&conn->ctx, IXEVIN, &handler);

	return &conn->ctx;
}

static void release(struct ixev_ctx *ctx)
{
	struct conn *conn = container_of(ctx, struct conn, ctx);

	memset(conn, 0xcc, sizeof(*conn));

	mempool_free(&conn_pool, conn);
}

static void udp_recv(void *addr, size_t len, struct ip_tuple *id)
{
	binary_header_t *req_header;
	struct mc_header *mch;
	struct udp_response *response;

#define DEBUG 0
#if DEBUG
	unsigned char *data = addr;
	printf("udp_recv: addr %p len %ld from %x:%d\n", addr, len, id->src_ip, id->src_port);
	for (int i=0;i<len;i++) printf("%02x ", data[i]);
	for (int i=0,c;i<len;i++) {
		c = data[i];
		printf("%c", c >= 0x20 && c <= 0x7f ? c : '.');
	}
	puts("");
#endif

	mch = addr;
	req_header = (void *)mch + sizeof(struct mc_header);

	assert(req_header->magic == 0x80);
	assert(req_header->opcode == CMD_GET);
	assert(len == sizeof(*mch) + sizeof(*req_header) + req_header->extra_len + __builtin_bswap16(req_header->key_len));

	process_request();

	response = mempool_alloc(&udp_response_pool);
	if (!response)
		goto out;

	response->dest.src_ip = id->dst_ip;
	response->dest.dst_ip = id->src_ip;
	response->dest.src_port = id->dst_port;
	response->dest.dst_port = id->src_port;
	bzero(&response->header, sizeof(response->header));

	/*copy the request's memcached header*/
	response->header.mc_h = *mch;

	response->header.req_h.magic = (uint8_t)0x81;
	response->header.req_h.status = __builtin_bswap16(1); /* Key not found */
	response->header.req_h.body_len = 0;
	response->header.req_h.key_len = req_header->key_len;

#if DEBUG
	printf("udp_send: addr %p len %ld to %x:%d\n", &response->header, sizeof(response->header), id->src_ip, id->src_port);
#endif
	ix_udp_send(&response->header, sizeof(response->header), &response->dest, (unsigned long) response);

out:
	ix_udp_recv_done(addr);
}

static void udp_sent(unsigned long cookie)
{
#if DEBUG
	printf("udp_sent: cookie %lx\n", cookie);
#endif
	struct udp_response *r = (struct udp_response *) cookie;
	mempool_free(&udp_response_pool, r);
}

static void ignore()
{
}

static struct ixev_conn_ops tcp_ops = {
	.accept		= &tcp_accept,
	.release	= &release,
};

static struct ix_ops udp_ops = {
	.udp_recv	= &udp_recv,
	.udp_sent	= &udp_sent,
	.tcp_connected	= ignore,
	.tcp_knock	= ignore,
	.tcp_recv	= ignore,
	.tcp_sent	= ignore,
	.tcp_dead	= ignore,
};

static void *tcp_thread_main(void *arg)
{
	int ret;
	thread_no = (long) arg;

	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		return NULL;
	}

	ret = mempool_create(&conn_pool, &conn_datastore);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	init_thread();

	while (1)
		ixev_wait();

	return NULL;
}

static void *udp_thread_main(void *arg)
{
	int ret;

	ret = ix_init(&udp_ops, 4096);
	assert(!ret);

	ret = mempool_create(&udp_response_pool, &udp_response_datastore);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	while (1) {
		ix_poll();
		/* TODO: handle failed syscalls */
		karr->len = 0;
		ix_handle_events();
	}
}

void init_ix(int udp)
{
	int ret;
	unsigned int conn_pool_entries;

	srand48(mytime());

	conn_pool_entries = ROUND_UP(16384, MEMPOOL_DEFAULT_CHUNKSIZE);

	if (udp) {
		ret = mempool_create_datastore(&udp_response_datastore, 65536, sizeof(struct udp_response), 0, MEMPOOL_DEFAULT_CHUNKSIZE, "conn");
		if (ret) {
			fprintf(stderr, "unable to create mempool\n");
			exit(-1);
		}
	} else {
		ret = ixev_init(&tcp_ops);
		if (ret) {
			fprintf(stderr, "failed to initialize ixev\n");
			exit(-1);
		}

		ret = mempool_create_datastore(&conn_datastore, conn_pool_entries, sizeof(struct conn), 0, MEMPOOL_DEFAULT_CHUNKSIZE, "conn");
		if (ret) {
			fprintf(stderr, "unable to create mempool\n");
			exit(-1);
		}
	}

	nr_cpu = sys_nrcpus();
	if (nr_cpu < 1) {
		fprintf(stderr, "got invalid cpu count %d\n", nr_cpu);
		exit(-1);
	}
}

void start_ix_server(int udp)
{
	int i;
	pthread_t tid;

	sys_spawnmode(true);

	for (i = 1; i < nr_cpu; i++) {
		if (pthread_create(&tid, NULL, udp ? udp_thread_main : tcp_thread_main, (void *) (long) i)) {
			fprintf(stderr, "failed to spawn thread %d\n", i);
			exit(-1);
		}
	}

	udp ? udp_thread_main(0) : tcp_thread_main(0);
}
