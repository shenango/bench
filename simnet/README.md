# Simnet

Simnet simulates packet traces against a system provisioned with configurable
number of CPU cores assigned to it. By sweeping over different configurations it
is able to experimentally find the optimal provisioning to meet an SLA given a
known request load and service time distribution.


### Usage

With Rust installed, run:

    $ cargo run --release

This will write CSV formatted output to stdout which can be copied to a
spreadsheet or piped to a file.

To see a full list of configuration options:

    $ cargo run --release -- --help