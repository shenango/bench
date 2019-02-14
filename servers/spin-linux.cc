#include <stdio.h>
#include <string.h>
#include <iostream>

#include "fake_worker.h"
#include "common.h"

FakeWorker *worker;

void do_work(int iterations)
{
	worker->Work(iterations);
}

void init_thread(void)
{
}

static void help(const char *prgname)
{
	printf("Usage: %s worker n_cpu port\n", prgname);
}

int main(int argc, char *argv[])
{
	int n_cpu, port;
	if (argc < 3) {
		help(argv[0]);
		return -1;
	}

	worker = FakeWorkerFactory(argv[1]);
	if (!worker) {
		std::cerr << "Invalid worker argument." << std::endl;
		return 1;
	}
	n_cpu = atoi(argv[2]);
	port = atoi(argv[3]);
	init_linux(n_cpu, port);
	start_linux_server();

	return 0;
}
