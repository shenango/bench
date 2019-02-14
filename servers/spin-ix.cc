#include <stdio.h>
#include <string.h>
#include <iostream>

#include "fake_worker.h"
#include "common.h"

FakeWorker *worker;

void process_request(void)
{
}

void do_work(int iterations)
{
	worker->Work(iterations);
}

void init_thread(void)
{
}

static void help(const char *prgname)
{
  printf("Usage: %s worker\n", prgname);
}

int main(int argc, char *argv[])
{
	int udp = 0;

	if (argc < 2) {
		help(argv[0]);
		return -1;
	}

	worker = FakeWorkerFactory(argv[1]);
	if (!worker) {
		std::cerr << "Invalid worker argument." << std::endl;
		return 1;
	}
	init_ix(udp);
	start_ix_server(udp);

	return 0;
}
