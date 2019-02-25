#![allow(deprecated)]
#![allow(unused)]

#[macro_use]
extern crate clap;
extern crate rand;

use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::io::stdout;
use std::io::Write;

use clap::{App, Arg};
use rand::distributions::{Exp, IndependentSample};
use rand::Rng;

#[derive(Copy, Clone, Debug)]
enum Distribution {
    Constant(f64),
    Exponential(f64),
    Bimodal1(f64),
    Bimodal2(f64),
}
impl Distribution {
    fn name(&self) -> &'static str {
        match *self {
            Distribution::Constant(_) => "constant",
            Distribution::Exponential(_) => "exponential",
            Distribution::Bimodal1(_) => "bimodal1",
            Distribution::Bimodal2(_) => "bimodal2",
        }
    }
    fn mean(&self) -> u64 {
        match *self {
            Distribution::Constant(m)
            | Distribution::Exponential(m)
            | Distribution::Bimodal1(m)
            | Distribution::Bimodal2(m) => m as u64,
        }
    }
    fn sample<R: Rng>(&self, rng: &mut R) -> u64 {
        match *self {
            Distribution::Constant(m) => m as u64,
            Distribution::Exponential(m) => Exp::new(1.0 / m).ind_sample(rng) as u64,
            Distribution::Bimodal1(m) => {
                if rng.gen_weighted_bool(10) {
                    (m * 5.5) as u64
                } else {
                    (m * 0.5) as u64
                }
            }
            Distribution::Bimodal2(m) => {
                if rng.gen_weighted_bool(1000) {
                    (m * 500.5) as u64
                } else {
                    (m * 0.5) as u64
                }
            }
        }
    }
}

struct Packet {
    target_start: u64,
    service_time: u64,
}

fn make_schedule(
    distribution: Distribution,
    packets_per_second: u64,
    interval: u64,
) -> Vec<Packet> {
    let ns_per_packet = 1_000_000_000 / packets_per_second;
    let num_packets = interval / ns_per_packet + 1;

    let exp = Exp::new(1.0 / ns_per_packet as f64);
    let mut rng = rand::thread_rng();

    let mut last = 0;
    let mut packets = Vec::with_capacity(num_packets as usize);
    for _ in 0..num_packets {
        last += exp.ind_sample(&mut rng) as u64;
        last = last.min(interval);

        packets.push(Packet {
            target_start: last,
            service_time: distribution.sample(&mut rng),
        });
    }

    packets
}

/// Simulate the given packet schedule and return the latencies of each packet in the schedule.
fn simulate(
    schedule: &[Packet],
    interval: u64,
    num_cpus: u64,
    peak_cpus: u64,
) -> Vec<u64> {
    let mut t = 0;
    let mut cpus: BinaryHeap<_> = (0..num_cpus).map(|_| Reverse(0)).collect();
    let mut latencies = Vec::with_capacity(schedule.len());

    for _ in num_cpus..peak_cpus {
        cpus.push(Reverse(interval));
    }

    assert!(!schedule.is_empty());
    for p in schedule {
        t = t.max(p.target_start);
        t = t.max(cpus.pop().unwrap().0);

        latencies.push(t - p.target_start + p.service_time);
        cpus.push(Reverse(p.service_time + t));
    }

    latencies
}

/// Simulate `iterations` number of packet schedules for the given system configuration, and return
/// the resulting 99.9 percentile latency.
fn bulk_simulate(
    distribution: Distribution,
    packets_per_second: u64,
    interval: u64,
    num_cpus: u64,
    peak_cpus: u64,
    iterations: u64
) -> f32 {
    let mut latencies: Vec<_> = (0..iterations)
        .flat_map(|_| {
            simulate(
                &make_schedule(distribution, packets_per_second, interval),
                interval,
                num_cpus,
                peak_cpus,
            ).into_iter()
        }).collect();

    latencies.sort();
    latencies[(latencies.len() as f32 * 99.9 / 100.0) as usize] as f32 / 1000.0
}

fn main() {
    let matches = App::new("Network load simulator")
        .version("0.1")
        .arg(
            Arg::with_name("peak_cpus")
                .long("peak_cpus")
                .default_value("16")
                .help("Number of cores to burst to after interval ends")
        )
        .arg(
            Arg::with_name("cpus")
                .long("cpus")
                .default_value("10")
                .help("Maximum number of CPUs to simulate during interval"),
        )
        .arg(
            Arg::with_name("load")
                .long("load")
                .default_value("8.0")
                .help("Maximum number of CPUs worth of load to subject the system to"),
        )
        .arg(
            Arg::with_name("service_time")
                .long("service_time")
                .default_value("10.0")
                .help("Mean service time of requests in microseconds"),
        )
        .arg(
            Arg::with_name("sla")
                .long("sla")
                .default_value("100.0")
                .help("Maximum 99.9% latency before system is considered overloaded in microseconds"),
        )
        .arg(
            Arg::with_name("interval")
                .long("interval")
                .default_value("1000")
                .help("Length of simulated interval in microseconds"),
        )
        .arg(
            Arg::with_name("datapoints")
                .long("datapoints")
                .default_value("100")
                .help("Number of different request loads to simulate"),
        )
        .arg(
            Arg::with_name("iterations")
                .long("iterations")
                .default_value("50000")
                .help("Number of packet traces to simulate per request load"),
        )
        .arg(
            Arg::with_name("distribution")
                .long("distribution")
                .takes_value(true)
                .possible_values(&["constant", "exponential", "bimodal1", "bimodal2"])
                .default_value("exponential")
                .help("Distribution of service times to use"),
        )
        .get_matches();

    let peak_cpus = value_t_or_exit!(matches, "peak_cpus", u64);
    let max_cpus = value_t_or_exit!(matches, "cpus", u64);
    let max_load = value_t_or_exit!(matches, "load", f64);
    let mean_service_time = value_t_or_exit!(matches, "service_time", f64);
    let target_sla = value_t_or_exit!(matches, "sla", f64);
    let interval_length = value_t_or_exit!(matches, "interval", u64);
    let datapoints = value_t_or_exit!(matches, "datapoints", u64);
    let datapoint_iterations = value_t_or_exit!(matches, "iterations", u64);
    let service_time_distribution = match matches.value_of("distribution").unwrap() {
        "constant" => Distribution::Constant,
        "exponential" => Distribution::Exponential,
        "bimodal1" => Distribution::Bimodal1,
        "bimodal2" => Distribution::Bimodal2,
        _ => unreachable!(),
    };

    println!("Load, Requests/s, Efficiency, Cores");
    for load in (1..=datapoints).map(|i| i as f64 / datapoints as f64 * max_load) {
        let rps = (load * 1000000.0 / mean_service_time) as u64;

        let mut latencies = Vec::new();
        let mut optimal_cpus = 0.0;
        let mut efficiency = 0.0;
        for cpus in 1..=max_cpus {
                let latency = bulk_simulate(
                    service_time_distribution(1000.0 * mean_service_time),
                    rps,
                    interval_length * 1000,
                    cpus,
                    peak_cpus,
                    datapoint_iterations,
                );

                latencies.push(latency);
                if (latency as f64) < target_sla {
                    optimal_cpus = cpus as f64;
                    efficiency = load / optimal_cpus;
                    break;
            }
        }
        println!("{:.4}, {}, {:.4}, {}", load, rps, efficiency, optimal_cpus);
    }
}
