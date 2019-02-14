#include <stdio.h>
#include <string.h>

#include "fake_worker.h"
#include "common.h"

FakeWorker *worker;

void do_work(int iterations)
{
	worker->Work(iterations);
}

static void help(const char *prgname)
{
	printf("Usage: %s [--udp] service-time-distribution worker port arachne_args\n"
	       "\n", prgname);
}

int main(int argc, char *argv[])
{
	int udp, port, next_arg;

	init_arachne(&argc, (const char **)argv);

        if (argc < 3) {
                help(argv[0]);
                return -1;
        }

        udp = !strcmp(argv[1], "--udp");

        if (udp)
		next_arg = 2;
	else
		next_arg = 1;

        worker = FakeWorkerFactory(argv[next_arg++]);
        port = atoi(argv[next_arg++]);
        start_arachne_server(udp, port);

        return 0;
}
