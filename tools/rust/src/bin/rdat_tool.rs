//! CLI tool for analyzing registry RDAT files.
//!
//! Usage:
//!   rdat_tool info registry.rdat           # Summary info
//!   rdat_tool dump registry.rdat           # Full human-readable dump
//!   rdat_tool json registry.rdat           # JSON output
//!   rdat_tool sqlite registry.rdat out.db  # Export to SQLite

use apex_rust_tools::registry::{ComponentType, DataCategory, RdatFile};
use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "rdat_tool")]
#[command(about = "Analyze registry RDAT files", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Show summary information about the RDAT file
    Info {
        /// Path to RDAT file
        rdat_file: PathBuf,
    },
    /// Dump full contents in human-readable format
    Dump {
        /// Path to RDAT file
        rdat_file: PathBuf,
        /// Show only components of this type
        #[arg(long, short = 't')]
        type_filter: Option<String>,
    },
    /// Output as JSON
    Json {
        /// Path to RDAT file
        rdat_file: PathBuf,
        /// Pretty-print JSON
        #[arg(long, short = 'p')]
        pretty: bool,
    },
    /// Export to SQLite database
    Sqlite {
        /// Path to RDAT file
        rdat_file: PathBuf,
        /// Path to output SQLite database
        sqlite_file: PathBuf,
    },
}

fn main() {
    let cli = Cli::parse();

    let result = match cli.command {
        Commands::Info { rdat_file } => cmd_info(&rdat_file),
        Commands::Dump {
            rdat_file,
            type_filter,
        } => cmd_dump(&rdat_file, type_filter.as_deref()),
        Commands::Json { rdat_file, pretty } => cmd_json(&rdat_file, pretty),
        Commands::Sqlite {
            rdat_file,
            sqlite_file,
        } => cmd_sqlite(&rdat_file, &sqlite_file),
    };

    if let Err(e) = result {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}

/* ----------------------------- Commands ----------------------------- */

fn cmd_info(path: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let rdat = RdatFile::from_file(path)?;

    println!("RDAT File: {}", path.display());
    println!("========================================");
    println!();
    println!("Format:");
    println!("  Version: {}", rdat.version);
    println!("  Flags:   0x{:04X}", rdat.flags);
    println!();
    println!("Counts:");
    println!("  Components:   {}", rdat.component_count());
    println!("  Tasks:        {}", rdat.task_count());
    println!("  Data Entries: {}", rdat.data_count());
    println!("  Total Data:   {} bytes", rdat.total_data_size());
    println!();

    // Component type breakdown
    println!("Component Types:");
    let types = [
        ComponentType::Executive,
        ComponentType::Core,
        ComponentType::SwModel,
        ComponentType::HwModel,
        ComponentType::Support,
        ComponentType::Driver,
    ];
    for ct in types {
        let count = rdat.components_by_type(ct).len();
        if count > 0 {
            println!("  {:12} {}", format!("{}:", ct.as_str()), count);
        }
    }
    println!();

    // Data category breakdown
    println!("Data Categories:");
    let mut category_counts = std::collections::HashMap::new();
    let mut category_sizes = std::collections::HashMap::new();
    for entry in rdat.data_entries() {
        *category_counts.entry(entry.category).or_insert(0usize) += 1;
        *category_sizes.entry(entry.category).or_insert(0u64) += entry.size as u64;
    }
    let categories = [
        DataCategory::State,
        DataCategory::StaticParam,
        DataCategory::TunableParam,
        DataCategory::Input,
        DataCategory::Output,
    ];
    for cat in categories {
        if let Some(&count) = category_counts.get(&cat) {
            let size = category_sizes.get(&cat).unwrap_or(&0);
            println!(
                "  {:14} {} entries, {} bytes",
                format!("{}:", cat.as_str()),
                count,
                size
            );
        }
    }

    Ok(())
}

fn cmd_dump(path: &PathBuf, type_filter: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let rdat = RdatFile::from_file(path)?;

    let filter_type = type_filter
        .map(|s| match s.to_uppercase().as_str() {
            "EXECUTIVE" => Some(ComponentType::Executive),
            "CORE" => Some(ComponentType::Core),
            "SW_MODEL" | "SWMODEL" => Some(ComponentType::SwModel),
            "HW_MODEL" | "HWMODEL" => Some(ComponentType::HwModel),
            "SUPPORT" => Some(ComponentType::Support),
            "DRIVER" => Some(ComponentType::Driver),
            _ => None,
        })
        .flatten();

    println!("RDAT File: {}", path.display());
    println!("Version: {}, Flags: 0x{:04X}", rdat.version, rdat.flags);
    println!();
    println!("========================================");
    println!("           REGISTERED COMPONENTS        ");
    println!("========================================");
    println!();

    for comp in rdat.components() {
        // Apply type filter if specified
        if let Some(ft) = filter_type {
            if comp.component_type != ft {
                continue;
            }
        }

        println!("Component: {} (fullUid={})", comp.name, comp.full_uid_hex());
        println!("----------------------------------------");
        println!("  componentId:    {}", comp.component_id);
        println!("  instanceIndex:  {}", comp.instance_index);
        println!("  type:           {}", comp.component_type);
        println!("  Tasks:          {}", comp.task_count);
        println!("  Data Entries:   {}", comp.data_count);

        // Show tasks
        let tasks = rdat.get_tasks_for_component(comp);
        if !tasks.is_empty() {
            println!("  Tasks:");
            for task in tasks {
                println!(
                    "    [{}] taskUid={} \"{}\"",
                    tasks
                        .iter()
                        .position(|t| t.task_uid == task.task_uid)
                        .unwrap_or(0),
                    task.task_uid,
                    task.name
                );
            }
        }

        // Show data entries
        let data = rdat.get_data_for_component(comp);
        if !data.is_empty() {
            println!("  Data:");
            for (i, entry) in data.iter().enumerate() {
                println!(
                    "    [{}] {} \"{}\" ({} bytes)",
                    i, entry.category, entry.name, entry.size
                );
            }
        }

        println!();
    }

    Ok(())
}

fn cmd_json(path: &PathBuf, pretty: bool) -> Result<(), Box<dyn std::error::Error>> {
    let rdat = RdatFile::from_file(path)?;

    // Build JSON structure
    let components: Vec<serde_json::Value> = rdat
        .components()
        .iter()
        .map(|comp| {
            let tasks: Vec<serde_json::Value> = rdat
                .get_tasks_for_component(comp)
                .iter()
                .map(|t| {
                    serde_json::json!({
                        "taskUid": t.task_uid,
                        "name": t.name
                    })
                })
                .collect();

            let data: Vec<serde_json::Value> = rdat
                .get_data_for_component(comp)
                .iter()
                .map(|d| {
                    serde_json::json!({
                        "category": d.category.as_str(),
                        "name": d.name,
                        "size": d.size
                    })
                })
                .collect();

            serde_json::json!({
                "fullUid": comp.full_uid,
                "fullUidHex": comp.full_uid_hex(),
                "componentId": comp.component_id,
                "instanceIndex": comp.instance_index,
                "name": comp.name,
                "type": comp.component_type.as_str(),
                "tasks": tasks,
                "data": data
            })
        })
        .collect();

    let output = serde_json::json!({
        "version": rdat.version,
        "flags": rdat.flags,
        "componentCount": rdat.component_count(),
        "taskCount": rdat.task_count(),
        "dataCount": rdat.data_count(),
        "totalDataSize": rdat.total_data_size(),
        "components": components
    });

    let json_str = if pretty {
        serde_json::to_string_pretty(&output)?
    } else {
        serde_json::to_string(&output)?
    };

    println!("{}", json_str);
    Ok(())
}

fn cmd_sqlite(
    rdat_path: &PathBuf,
    sqlite_path: &PathBuf,
) -> Result<(), Box<dyn std::error::Error>> {
    use rusqlite::Connection;

    let rdat = RdatFile::from_file(rdat_path)?;

    // Remove existing file if present
    if sqlite_path.exists() {
        std::fs::remove_file(sqlite_path)?;
    }

    let conn = Connection::open(sqlite_path)?;

    // Create tables
    conn.execute_batch(
        "
        CREATE TABLE metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE components (
            full_uid INTEGER PRIMARY KEY,
            component_id INTEGER NOT NULL,
            instance_index INTEGER NOT NULL,
            name TEXT NOT NULL,
            component_type TEXT NOT NULL,
            task_count INTEGER NOT NULL,
            data_count INTEGER NOT NULL
        );

        CREATE TABLE tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            full_uid INTEGER NOT NULL,
            task_uid INTEGER NOT NULL,
            name TEXT NOT NULL,
            FOREIGN KEY (full_uid) REFERENCES components(full_uid)
        );

        CREATE TABLE data_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            full_uid INTEGER NOT NULL,
            category TEXT NOT NULL,
            name TEXT NOT NULL,
            size INTEGER NOT NULL,
            FOREIGN KEY (full_uid) REFERENCES components(full_uid)
        );

        CREATE INDEX idx_tasks_full_uid ON tasks(full_uid);
        CREATE INDEX idx_data_full_uid ON data_entries(full_uid);
        CREATE INDEX idx_components_type ON components(component_type);
        CREATE INDEX idx_data_category ON data_entries(category);
        ",
    )?;

    // Insert metadata
    {
        let mut stmt = conn.prepare("INSERT INTO metadata (key, value) VALUES (?1, ?2)")?;
        stmt.execute(["version", &rdat.version.to_string()])?;
        stmt.execute(["flags", &format!("0x{:04X}", rdat.flags)])?;
        stmt.execute(["source_file", &rdat_path.display().to_string()])?;
        stmt.execute(["component_count", &rdat.component_count().to_string()])?;
        stmt.execute(["task_count", &rdat.task_count().to_string()])?;
        stmt.execute(["data_count", &rdat.data_count().to_string()])?;
        stmt.execute(["total_data_size", &rdat.total_data_size().to_string()])?;
    }

    // Insert components
    {
        let mut stmt = conn.prepare(
            "INSERT INTO components (full_uid, component_id, instance_index, name, component_type, task_count, data_count)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        )?;
        for comp in rdat.components() {
            stmt.execute((
                comp.full_uid,
                comp.component_id,
                comp.instance_index,
                &comp.name,
                comp.component_type.as_str(),
                comp.task_count,
                comp.data_count,
            ))?;
        }
    }

    // Insert tasks
    {
        let mut stmt =
            conn.prepare("INSERT INTO tasks (full_uid, task_uid, name) VALUES (?1, ?2, ?3)")?;
        for task in rdat.tasks() {
            stmt.execute((task.full_uid, task.task_uid, &task.name))?;
        }
    }

    // Insert data entries
    {
        let mut stmt = conn.prepare(
            "INSERT INTO data_entries (full_uid, category, name, size) VALUES (?1, ?2, ?3, ?4)",
        )?;
        for entry in rdat.data_entries() {
            stmt.execute((
                entry.full_uid,
                entry.category.as_str(),
                &entry.name,
                entry.size,
            ))?;
        }
    }

    println!("Exported to: {}", sqlite_path.display());
    println!("  Components: {}", rdat.component_count());
    println!("  Tasks:      {}", rdat.task_count());
    println!("  Data:       {}", rdat.data_count());

    Ok(())
}
