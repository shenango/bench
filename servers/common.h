#pragma once

#include <math.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#if defined (__cplusplus)
extern "C" {
#endif

void init_ix(int udp);
void init_linux(int n_cpu, int port);
void init_arachne(int *argc, const char** argv);
void init_thread(void);
void process_request(void);
void start_ix_server(int udp);
void start_linux_server(void);
void start_arachne_server(int udp, int port);
void do_work(int iterations);

#if defined (__cplusplus)
}
#endif

extern __thread int thread_no;
extern int nr_cpu;

static inline long mytime(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}
