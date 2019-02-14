# Threading Benchmarks

First build Arachne, then build the benchmarks in this directory with
`make clean && make`. Run the benchmarks as described below,
restricting each to run on a single core.

## pthreads
```
taskset --cpu-list 2 ./tbench_linux
```

## Go
```
export GOMAXPROCS=1
cd go
go test -bench .
```

## Arachne
In arachne-all directory:
```
sudo ./CoreArbiter/bin/coreArbiterServer
```

In this directory:
```
sudo ./tbench_arachne
```
