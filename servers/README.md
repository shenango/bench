## Overview

This repository contains a collection of TCP and UDP RPC servers for
use with Linux, [IX](https://github.com/ix-project/ix),
[Zygos](https://github.com/ix-project/zygos), and
[Arachne](https://github.com/PlatformLab/Arachne). These servers are
based on the [servers used by
Zygos](https://github.com/ix-project/servers), but instead of
implementing the binary memcached protocol, they implement the
protocol used by Shenango's synthetic application.

To build these servers, first build the dependencies (Shenango,
Arachne, and ZygOS), then run `make clean && make` in this
directory. The servers can be run as described below.

To start a Shenango client, see the [Shenango
repo](https://github.com/shenango/shenango).

### Linux
```
./spin-linux <synthetic_work> <cores> <port>
```
For example:
```
./spin-linux stridedmem:1024:7 16 5000
```

### ZygOS
```
$IX_DIR/dp/ix -c <ix_conf_file> -- ./spin-ix <synthetic_work>
```

### Arachne
Start Arachne's core arbiter in the arachne-all directory:
```
sudo ./CoreArbiter/bin/coreArbiterServer
```
Then start the Arachne server in this directory:
```
./spin-arachne <arachne_args> <synthetic_work> <port>
```
For example:
```
./spin-arachne --minNumCores 2 --maxNumCores 16 stridedmem:1024:7 5000
```
