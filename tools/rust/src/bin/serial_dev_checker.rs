//! serial_dev_checker — Serial Port Status Checker
//!
//! Usage:
//!   cargo run --bin serial_dev_checker -- --config ports.toml [--report status.txt] [-v] [--show-causes]

use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

use clap::Parser;
use serde::Deserialize;

use apex_rust_tools::serial::{ProbeOptions, ProbeReport, SerialProbe};

/// ANSI color codes
const COLORS: &[(&str, &str)] = &[
    ("connected_openable", "\x1b[92m"), // green
    ("connected_in_use", "\x1b[93m"),   // yellow
    ("permission_denied", "\x1b[91m"),  // red
    ("does_not_exist", "\x1b[90m"),     // gray
    ("invalid_path", "\x1b[95m"),       // magenta
    ("unreachable_device", "\x1b[91m"), // red
    ("not_a_device", "\x1b[94m"),       // blue
];
const RESET: &str = "\x1b[0m";

#[derive(Parser, Debug)]
#[command(name = "serial_dev_checker", about = "Serial Port Status Checker")]
struct Args {
    /// Path to TOML config file
    #[arg(long)]
    config: PathBuf,

    /// Optional path to write status report
    #[arg(long)]
    report: Option<PathBuf>,

    /// Enable verbose output
    #[arg(short, long)]
    verbosity: bool,

    /// Faster mode: skip lsof and termios apply (bulk scans)
    #[arg(long)]
    fast: bool,

    /// Show trigger flags (held_by_other/excl_failed/flock_failed) next to status
    #[arg(long)]
    show_causes: bool,
}

#[derive(Debug, Deserialize)]
struct PortEntry {
    path: String,
    #[serde(default)]
    label: String,
}

#[derive(Debug, Deserialize)]
struct Config {
    ports: Vec<PortEntry>,
}

fn main() {
    let args = Args::parse();

    if !args.config.exists() {
        eprintln!("Config file not found: {}", args.config.display());
        std::process::exit(1);
    }

    println!("Loading port map from: {}", args.config.display());

    let cfg_str = fs::read_to_string(&args.config).expect("Failed to read config file");
    let cfg: Config = toml::from_str(&cfg_str).expect("Invalid TOML format");

    if cfg.ports.is_empty() {
        eprintln!("No ports found in config.");
        std::process::exit(1);
    }

    // Probe options. Allow --fast to skip heavier checks.
    let opts = if args.fast {
        ProbeOptions {
            use_lsof: false,
            apply_termios: false,
        }
    } else {
        ProbeOptions::default()
    };

    let verbose = args.verbosity;

    let mut results: Vec<(String, String, ProbeReport)> = Vec::new();
    for p in &cfg.ports {
        let label = if p.label.is_empty() {
            "Unnamed".into()
        } else {
            p.label.clone()
        };

        // Probe with our shared validator
        let r = SerialProbe::probe(&p.path, opts, None, verbose);

        if verbose {
            eprintln!(
                "Assessing {} ({}): {}",
                p.path,
                label,
                r.status.human_status()
            );
        }

        results.push((label, p.path.clone(), r));
    }

    // show_causes if explicitly requested OR when verbose
    let show_causes = args.show_causes || args.verbosity;
    print_summary(&results, show_causes);

    if let Some(rp) = args.report {
        generate_report(&results, &rp);
    }
}

/// Normalize status
fn normalize_status(human: &str) -> &str {
    // human already returns: invalid_path, does_not_exist, not_a_device, permission_denied,
    // connected_openable, connected_in_use, unreachable_device
    human
}

fn color_for(status: &str) -> &str {
    COLORS
        .iter()
        .find(|(s, _)| *s == status)
        .map(|(_, c)| *c)
        .unwrap_or("")
}

fn print_summary(results: &[(String, String, ProbeReport)], show_causes: bool) {
    println!("\nSerial Port Summary:");
    println!("{:<20} {:<43} Status", "Label", "Path");

    println!("{}", "-".repeat(80));

    for (label, path, report) in results {
        let human = report.status.human_status();
        let color = color_for(human);

        let causes = if show_causes && human == "connected_in_use" {
            format!(
                " [held_by_other={}, excl_failed={}, flock_failed={}]",
                report.held_by_other, report.excl_open_failed, report.flock_failed
            )
        } else {
            String::new()
        };

        // If we have a friendly by-id name, display it under the path for convenience.
        if let Some(friendly) = &report.friendly_name {
            println!(
                "{:<20} {:<43} {}{}{}{}",
                label, path, color, human, causes, RESET
            );
            println!("{:<20} {:<43} (by-id: {})", "", "", friendly);
        } else {
            println!(
                "{:<20} {:<43} {}{}{}{}",
                label, path, color, human, causes, RESET
            );
        }
    }

    println!("\nStatus Summary:");
    // Count by normalized status string
    let mut counts: HashMap<&str, usize> = HashMap::new();
    for (_, _, report) in results {
        let s = normalize_status(report.status.human_status());
        *counts.entry(s).or_default() += 1;
    }

    // Severity ordering
    fn severity(s: &str) -> i32 {
        match s {
            "unreachable_device" => 0,
            "invalid_path" => 1,
            "does_not_exist" => 2,
            "not_a_device" => 3,
            "permission_denied" => 4,
            "connected_in_use" => 5,
            "connected_openable" => 6,
            _ => 99,
        }
    }

    let mut items: Vec<_> = counts.into_iter().collect();
    items.sort_by_key(|(s, _)| severity(s));

    for (status, count) in items {
        let color = color_for(status);
        println!("{}{: <20}: {}{}", color, status, count, RESET);
    }
}

fn generate_report(results: &[(String, String, ProbeReport)], path: &PathBuf) {
    println!("\nWriting report to: {}", path.display());
    let mut out = String::new();
    for (label, p, report) in results {
        // "<label> (<path>): <status>"
        out.push_str(&format!(
            "{} ({}) : {}\n",
            label,
            p,
            report.status.human_status()
        ));
    }
    fs::write(path, out).expect("Failed to write report");
}
