//! CLI tool for analyzing scheduler SDAT files.
//!
//! Usage:
//!   sdat_tool info sched.rdat     # Summary info
//!   sdat_tool dump sched.rdat     # Full human-readable dump
//!   sdat_tool json sched.rdat     # JSON output

use apex_rust_tools::scheduler::SdatFile;
use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "sdat_tool")]
#[command(about = "Analyze scheduler SDAT files", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Show summary information about the SDAT file
    Info {
        /// Path to SDAT file
        sdat_file: PathBuf,
    },
    /// Dump full contents in human-readable format
    Dump {
        /// Path to SDAT file
        sdat_file: PathBuf,
    },
    /// Output as JSON
    Json {
        /// Path to SDAT file
        sdat_file: PathBuf,
        /// Pretty-print JSON
        #[arg(long, short = 'p')]
        pretty: bool,
    },
}

fn main() {
    let cli = Cli::parse();

    let result = match cli.command {
        Commands::Info { sdat_file } => cmd_info(&sdat_file),
        Commands::Dump { sdat_file } => cmd_dump(&sdat_file),
        Commands::Json { sdat_file, pretty } => cmd_json(&sdat_file, pretty),
    };

    if let Err(e) = result {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}

/* ----------------------------- Commands ----------------------------- */

fn cmd_info(path: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let sdat = SdatFile::from_file(path)?;

    println!("SDAT File: {}", path.display());
    println!("========================================");
    println!();
    println!("Format:");
    println!("  Version:          {}", sdat.version);
    println!("  Flags:            0x{:04X}", sdat.flags);
    println!();
    println!("Schedule Configuration:");
    println!("  Fundamental Freq: {} Hz", sdat.fundamental_freq);
    println!("  Total Ticks:      {}", sdat.tick_count);
    println!(
        "  Frame Period:     {:.3} ms",
        1000.0 / sdat.fundamental_freq as f64
    );
    println!();
    println!("Tasks:");
    println!("  Total Tasks:      {}", sdat.task_count());
    println!(
        "  Active Ticks:     {} / {}",
        sdat.active_tick_count(),
        sdat.tick_count
    );
    println!("  Max Tasks/Tick:   {}", sdat.max_tasks_per_tick());
    println!("  Avg Tasks/Tick:   {:.2}", sdat.avg_tasks_per_tick());
    println!();

    // Frequency distribution
    println!("Frequency Distribution:");
    let by_freq = sdat.tasks_by_frequency();
    let mut freqs: Vec<_> = by_freq.keys().collect();
    freqs.sort_by(|a, b| {
        let freq_a: f64 = a.trim_end_matches("Hz").parse().unwrap_or(0.0);
        let freq_b: f64 = b.trim_end_matches("Hz").parse().unwrap_or(0.0);
        freq_b.partial_cmp(&freq_a).unwrap()
    });
    for freq in freqs {
        let tasks = &by_freq[freq];
        println!("  {}: {} task(s)", freq, tasks.len());
    }
    println!();

    // Pool distribution
    let mut pools: std::collections::HashMap<u8, usize> = std::collections::HashMap::new();
    for task in &sdat.tasks {
        *pools.entry(task.pool_index).or_default() += 1;
    }
    println!("Thread Pool Distribution:");
    let mut pool_ids: Vec<_> = pools.keys().collect();
    pool_ids.sort();
    for pool_id in pool_ids {
        println!("  Pool {}: {} task(s)", pool_id, pools[pool_id]);
    }

    Ok(())
}

fn cmd_dump(path: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let sdat = SdatFile::from_file(path)?;

    println!("SDAT File: {}", path.display());
    println!("========================================");
    println!();

    // Header info
    println!("Header:");
    println!("  Version:          {}", sdat.version);
    println!("  Fundamental Freq: {} Hz", sdat.fundamental_freq);
    println!("  Tick Count:       {}", sdat.tick_count);
    println!("  Task Count:       {}", sdat.task_count());
    println!();

    // Task table
    println!("Tasks:");
    println!("========================================");
    for (i, task) in sdat.tasks.iter().enumerate() {
        println!();
        println!("Task [{}]: {}", i, task.name);
        println!(
            "  fullUid:    0x{:06X} (component 0x{:04X}, instance {})",
            task.full_uid,
            task.component_id(),
            task.instance_index()
        );
        println!("  taskUid:    {}", task.task_uid);
        println!(
            "  frequency:  {:.1} Hz (N={}, D={})",
            task.frequency(),
            task.freq_n,
            task.freq_d
        );
        println!("  offset:     {}", task.offset);
        println!("  priority:   {}", task.priority);
        println!("  pool:       {}", task.pool_index);
        if task.is_sequenced() {
            println!(
                "  sequencing: group={}, phase={}",
                task.sequence_group, task.sequence_phase
            );
        } else {
            println!("  sequencing: none");
        }
    }
    println!();

    // Tick schedule
    println!("Tick Schedule:");
    println!("========================================");
    for tick_entry in &sdat.tick_schedule {
        let task_names: Vec<&str> = tick_entry
            .task_indices
            .iter()
            .filter_map(|&idx| sdat.tasks.get(idx as usize).map(|t| t.name.as_str()))
            .collect();
        println!(
            "  Tick {:3}: {} task(s) - {:?}",
            tick_entry.tick,
            tick_entry.task_indices.len(),
            task_names
        );
    }

    Ok(())
}

fn cmd_json(path: &PathBuf, pretty: bool) -> Result<(), Box<dyn std::error::Error>> {
    let sdat = SdatFile::from_file(path)?;

    // Build JSON structure
    let tasks_json: Vec<serde_json::Value> = sdat
        .tasks
        .iter()
        .map(|t| {
            serde_json::json!({
                "name": t.name,
                "fullUid": format!("0x{:06X}", t.full_uid),
                "componentId": format!("0x{:04X}", t.component_id()),
                "instanceIndex": t.instance_index(),
                "taskUid": t.task_uid,
                "frequency": t.frequency(),
                "freqN": t.freq_n,
                "freqD": t.freq_d,
                "offset": t.offset,
                "priority": t.priority,
                "poolIndex": t.pool_index,
                "sequenced": t.is_sequenced(),
                "sequenceGroup": if t.is_sequenced() { Some(t.sequence_group) } else { None },
                "sequencePhase": if t.is_sequenced() { Some(t.sequence_phase) } else { None },
            })
        })
        .collect();

    let ticks_json: Vec<serde_json::Value> = sdat
        .tick_schedule
        .iter()
        .map(|t| {
            serde_json::json!({
                "tick": t.tick,
                "taskIndices": t.task_indices,
            })
        })
        .collect();

    let json = serde_json::json!({
        "version": sdat.version,
        "flags": sdat.flags,
        "fundamentalFreq": sdat.fundamental_freq,
        "tickCount": sdat.tick_count,
        "tasks": tasks_json,
        "tickSchedule": ticks_json,
    });

    if pretty {
        println!("{}", serde_json::to_string_pretty(&json)?);
    } else {
        println!("{}", serde_json::to_string(&json)?);
    }

    Ok(())
}
