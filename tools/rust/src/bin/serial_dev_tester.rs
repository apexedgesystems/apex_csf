//! serial_dev_tester — Serial Port Loopback / Interconnect Verifier
//!
//! Usage:
//!   cargo run --bin serial_dev_tester -- --config pairs.toml [--report results.txt] -v

use std::fs;
use std::path::PathBuf;
use std::time::Duration;

use clap::Parser;
use rand::distr::Alphanumeric;
use rand::Rng;
use serde::Deserialize;

use apex_rust_tools::serial::{
    BaudRate, NumStopBits, Parity, ProbeOptions, ProbeStatus, SerialParams, SerialPort, SerialProbe,
};

/// ANSI colors for status formatting
const COLORS: &[(&str, &str)] = &[
    ("pass", "\x1b[92m"),  // green
    ("fail", "\x1b[91m"),  // red
    ("error", "\x1b[93m"), // yellow
];
const RESET: &str = "\x1b[0m";

#[derive(Parser, Debug)]
#[command(
    name = "serial_dev_tester",
    about = "Serial Port Loopback/Interconnect Verifier"
)]
struct Args {
    /// Path to TOML config file
    #[arg(long)]
    config: PathBuf,

    /// Optional path to write test results
    #[arg(long)]
    report: Option<PathBuf>,

    /// Enable verbose output
    #[arg(short, long)]
    verbosity: bool,

    /// Message read timeout in milliseconds (default 300)
    #[arg(long, default_value_t = 300)]
    timeout_ms: u64,

    /// Baud rate for tests (default 115200)
    #[arg(long, default_value_t = 115200)]
    baud: u32,
}

#[derive(Debug, Deserialize)]
struct Pair {
    label: Option<String>,
    #[serde(default)]
    r#type: String, // "loopback" or "interconnect" (default interconnect if absent)
    port: Option<String>,       // for loopback
    ports: Option<Vec<String>>, // for interconnect
}

#[derive(Debug, Deserialize)]
struct Config {
    pairs: Vec<Pair>,
}

#[derive(Debug)]
struct TestResult {
    label: String,
    r#type: String,
    status: String, // "pass" | "fail" | "error"
    detail: String,
}

fn main() {
    let args = Args::parse();

    if !args.config.exists() {
        eprintln!("Config file not found: {}", args.config.display());
        std::process::exit(1);
    }

    println!("Loading test config from: {}", args.config.display());

    let cfg_str = fs::read_to_string(&args.config).expect("Failed to read config file");
    let cfg: Config = toml::from_str(&cfg_str).expect("Invalid TOML format");

    if cfg.pairs.is_empty() {
        eprintln!("No pairs found in config.");
        std::process::exit(1);
    }

    let params = SerialParams {
        baud: match args.baud {
            9600 => BaudRate::B9600,
            19200 => BaudRate::B19200,
            115200 => BaudRate::B115200,
            921600 => BaudRate::B921600,
            other => BaudRate::Custom(other),
        },
        parity: Parity::None,
        stop_bits: NumStopBits::One,
    };
    let timeout = Duration::from_millis(args.timeout_ms);
    let verbose = args.verbosity;

    // We’ll do a quick probe first, then open for I/O.
    let probe_opts = ProbeOptions {
        use_lsof: true,
        apply_termios: true,
    };

    let mut results = Vec::new();
    for pair in cfg.pairs {
        let label = pair.label.unwrap_or_else(|| "Unnamed".into());
        let t = pair.r#type.to_lowercase();
        let r = if t == "loopback" {
            match pair.port {
                Some(port) => {
                    run_loopback(&label, &port, params.clone(), timeout, probe_opts, verbose)
                }
                None => TestResult {
                    label,
                    r#type: "loopback".into(),
                    status: "error".into(),
                    detail: "Missing 'port' for loopback".into(),
                },
            }
        } else {
            match pair.ports {
                Some(ports) if ports.len() == 2 => run_interconnect(
                    &label,
                    &ports[0],
                    &ports[1],
                    params.clone(),
                    timeout,
                    probe_opts,
                    verbose,
                ),
                Some(_) => TestResult {
                    label,
                    r#type: "interconnect".into(),
                    status: "error".into(),
                    detail: "Expected 2 ports".into(),
                },
                None => TestResult {
                    label,
                    r#type: "interconnect".into(),
                    status: "error".into(),
                    detail: "Missing 'ports'".into(),
                },
            }
        };
        results.push(r);
    }

    print_summary(&results);

    if let Some(rp) = args.report {
        generate_report(&results, &rp);
    }
}

fn make_test_message(port_path: &str) -> String {
    let mut rng = rand::rng();
    let suffix: String = (&mut rng)
        .sample_iter(Alphanumeric)
        .take(4)
        .map(char::from)
        .collect();
    format!("{port_path} RX {suffix}")
}

fn run_loopback(
    label: &str,
    port: &str,
    params: SerialParams,
    timeout: std::time::Duration,
    probe_opts: ProbeOptions,
    verbose: bool,
) -> TestResult {
    // Probe first
    let pr = SerialProbe::probe(port, probe_opts, Some(params.clone()), verbose);
    if !matches!(
        pr.status,
        ProbeStatus::ConnectedOpenable | ProbeStatus::ConnectedInUse
    ) {
        return TestResult {
            label: label.into(),
            r#type: "loopback".into(),
            status: "error".into(),
            detail: format!("Could not open {port}"),
        };
    }

    // Open for I/O
    let mut sp = match SerialPort::open(port, params) {
        Ok(p) => p,
        Err(e) => {
            return TestResult {
                label: label.into(),
                r#type: "loopback".into(),
                status: "error".into(),
                detail: format!("Open failed: {e}"),
            }
        }
    };

    // Send two messages and expect echo (hardware loopback)
    let mut passes = Vec::new();
    for _ in 0..2 {
        let msg = make_test_message(port);
        if verbose {
            println!("[DEBUG] Writing to {port}: {msg}");
        }
        if let Err(e) = sp.write_ascii(&(msg.clone() + "\n"), timeout) {
            return TestResult {
                label: label.into(),
                r#type: "loopback".into(),
                status: "error".into(),
                detail: format!("Write error: {e}"),
            };
        }
        let rx = match sp.read_bytes(1024, timeout) {
            Ok(b) => String::from_utf8_lossy(&b).trim().to_string(),
            Err(e) => {
                return TestResult {
                    label: label.into(),
                    r#type: "loopback".into(),
                    status: "error".into(),
                    detail: format!("Read error: {e}"),
                }
            }
        };
        if verbose {
            println!("[DEBUG] {port} received: {rx}");
        }
        passes.push(rx.contains(&msg));
    }

    if passes.iter().all(|x| *x) {
        TestResult {
            label: label.into(),
            r#type: "loopback".into(),
            status: "pass".into(),
            detail: "loopback verified".into(),
        }
    } else {
        TestResult {
            label: label.into(),
            r#type: "loopback".into(),
            status: "fail".into(),
            detail: "One or both loopback messages were not received correctly".into(),
        }
    }
}

fn run_interconnect(
    label: &str,
    a: &str,
    b: &str,
    params: SerialParams,
    timeout: std::time::Duration,
    probe_opts: ProbeOptions,
    verbose: bool,
) -> TestResult {
    // Probe both ends
    let ra = SerialProbe::probe(a, probe_opts, Some(params.clone()), verbose);
    let rb = SerialProbe::probe(b, probe_opts, Some(params.clone()), verbose);
    if !matches!(
        ra.status,
        ProbeStatus::ConnectedOpenable | ProbeStatus::ConnectedInUse
    ) || !matches!(
        rb.status,
        ProbeStatus::ConnectedOpenable | ProbeStatus::ConnectedInUse
    ) {
        return TestResult {
            label: label.into(),
            r#type: "interconnect".into(),
            status: "error".into(),
            detail: "One or both ports could not be opened".into(),
        };
    }

    // Open both for I/O
    let mut sp_a = match SerialPort::open(a, params.clone()) {
        Ok(p) => p,
        Err(e) => {
            return TestResult {
                label: label.into(),
                r#type: "interconnect".into(),
                status: "error".into(),
                detail: format!("Open {a} failed: {e}"),
            }
        }
    };
    let mut sp_b = match SerialPort::open(b, params) {
        Ok(p) => p,
        Err(e) => {
            return TestResult {
                label: label.into(),
                r#type: "interconnect".into(),
                status: "error".into(),
                detail: format!("Open {b} failed: {e}"),
            }
        }
    };

    // A → B
    let msg_a = make_test_message(a);
    if verbose {
        println!("[DEBUG] Writing from {a} → {b}: {msg_a}");
    }
    if let Err(e) = sp_a.write_ascii(&(msg_a.clone() + "\n"), timeout) {
        return TestResult {
            label: label.into(),
            r#type: "interconnect".into(),
            status: "error".into(),
            detail: format!("Write {a} error: {e}"),
        };
    }
    let rx_b = match sp_b.read_bytes(1024, timeout) {
        Ok(b) => String::from_utf8_lossy(&b).trim().to_string(),
        Err(e) => {
            return TestResult {
                label: label.into(),
                r#type: "interconnect".into(),
                status: "error".into(),
                detail: format!("Read {b} error: {e}"),
            }
        }
    };

    // B → A
    let msg_b = make_test_message(b);
    if verbose {
        println!("[DEBUG] Writing from {b} → {a}: {msg_b}");
    }
    if let Err(e) = sp_b.write_ascii(&(msg_b.clone() + "\n"), timeout) {
        return TestResult {
            label: label.into(),
            r#type: "interconnect".into(),
            status: "error".into(),
            detail: format!("Write {b} error: {e}"),
        };
    }
    let rx_a = match sp_a.read_bytes(1024, timeout) {
        Ok(b) => String::from_utf8_lossy(&b).trim().to_string(),
        Err(e) => {
            return TestResult {
                label: label.into(),
                r#type: "interconnect".into(),
                status: "error".into(),
                detail: format!("Read {a} error: {e}"),
            }
        }
    };

    if verbose {
        println!("[DEBUG] {b} received: {rx_b}");
        println!("[DEBUG] {a} received: {rx_a}");
    }

    let pass_a_to_b = rx_b.contains(&msg_a);
    let pass_b_to_a = rx_a.contains(&msg_b);

    if pass_a_to_b && pass_b_to_a {
        TestResult {
            label: label.into(),
            r#type: "interconnect".into(),
            status: "pass".into(),
            detail: "data verified in both directions".into(),
        }
    } else {
        let mut detail = String::new();
        if !pass_a_to_b {
            detail.push_str(&format!("{b} did not receive expected message from {a}. "));
        }
        if !pass_b_to_a {
            detail.push_str(&format!("{a} did not receive expected message from {b}."));
        }
        TestResult {
            label: label.into(),
            r#type: "interconnect".into(),
            status: "fail".into(),
            detail: detail.trim().into(),
        }
    }
}

fn print_summary(results: &[TestResult]) {
    println!("\nLoopback/Interconnect Test Results:");
    println!("{:<25} {:<13} {:<10} Details", "Label", "Type", "Status");
    println!("{}", "-".repeat(75));

    for r in results {
        let color = COLORS
            .iter()
            .find(|(s, _)| *s == r.status)
            .map(|(_, c)| *c)
            .unwrap_or("");
        println!(
            "{:<25} {:<13} {}{:<10}{} {}",
            r.label, r.r#type, color, r.status, RESET, r.detail
        );
    }
}

fn generate_report(results: &[TestResult], path: &PathBuf) {
    println!("\nWriting report to: {}", path.display());
    let mut out = String::new();
    for r in results {
        out.push_str(&format!(
            "{} ({}) : {} — {}\n",
            r.label, r.r#type, r.status, r.detail
        ));
    }
    fs::write(path, out).expect("Failed to write report");
}
