//! cfg2bin: Convert TOML/JSON config files to binary blobs.
//!
//! Usage:
//!   cfg2bin --config foo.toml --output foo.bin
//!   cfg2bin --config foo.json              # outputs foo.bin
//!   cfg2bin --batch ./configs/ --output ./binaries/
//!   cfg2bin --config foo.toml --verify existing.bin  # verify match

use std::{
    fs,
    path::{Path, PathBuf},
    process::ExitCode,
};

use apex_rust_tools::tunable_params::{binary, Error};
use clap::Parser;
use walkdir::WalkDir;

#[derive(Parser, Debug)]
#[command(name = "cfg2bin", about = "Convert TOML/JSON config to binary TPRM")]
struct Args {
    /// Path to the input TOML or JSON config file (single file mode)
    #[arg(short, long, conflicts_with = "batch")]
    config: Option<PathBuf>,

    /// Path to input directory for batch processing
    #[arg(short, long, conflicts_with = "config")]
    batch: Option<PathBuf>,

    /// Path to the output file or directory
    /// - Single mode: output binary file (default: same name with .tprm extension)
    /// - Batch mode: output directory (required)
    #[arg(short, long)]
    output: Option<PathBuf>,

    /// Verify generated binary matches existing file (instead of writing)
    #[arg(long)]
    verify: Option<PathBuf>,

    /// Show hex dump of generated binary (useful for debugging)
    #[arg(long)]
    hexdump: bool,

    /// Glob pattern to filter files in batch mode (default: *.toml,*.json)
    #[arg(long, default_value = "")]
    pattern: String,
}

fn main() -> ExitCode {
    let args = Args::parse();

    match run(&args) {
        Ok(stats) => {
            if stats.total > 1 {
                println!(
                    "Batch complete: {}/{} succeeded",
                    stats.succeeded, stats.total
                );
            }
            if stats.failed > 0 {
                ExitCode::FAILURE
            } else {
                ExitCode::SUCCESS
            }
        }
        Err(e) => {
            eprintln!("Error: {e}");
            ExitCode::FAILURE
        }
    }
}

struct Stats {
    total: usize,
    succeeded: usize,
    failed: usize,
}

fn run(args: &Args) -> Result<Stats, Error> {
    if let Some(batch_dir) = &args.batch {
        run_batch(batch_dir, args)
    } else if let Some(config) = &args.config {
        run_single(config, args)
    } else {
        Err(Error::InvalidArgs(
            "either --config or --batch must be specified".to_string(),
        ))
    }
}

fn run_single(config: &Path, args: &Args) -> Result<Stats, Error> {
    // Read and parse config to get binary data
    let content = fs::read_to_string(config)?;
    let data: serde_json::Value = if config.extension().map_or(false, |e| e == "toml") {
        let doc: toml_edit::DocumentMut = content
            .parse()
            .map_err(|e| Error::Parse(format!("TOML parse error: {e}")))?;
        toml_to_json(doc.as_item())
    } else {
        serde_json::from_str(&content)
            .map_err(|e| Error::Parse(format!("JSON parse error: {e}")))?
    };
    let binary_data = binary::serialize_value(&data)?;

    // Show hexdump if requested
    if args.hexdump {
        println!("Generated binary ({} bytes):", binary_data.len());
        hexdump(&binary_data);
    }

    // Verify mode: compare against existing file
    if let Some(verify_path) = &args.verify {
        let existing = fs::read(verify_path)?;
        if binary_data == existing {
            println!(
                "MATCH: {} ({} bytes)",
                verify_path.display(),
                binary_data.len()
            );
            return Ok(Stats {
                total: 1,
                succeeded: 1,
                failed: 0,
            });
        } else {
            eprintln!(
                "MISMATCH: generated {} bytes vs existing {} bytes",
                binary_data.len(),
                existing.len()
            );
            if binary_data.len() == existing.len() {
                // Show first difference
                for (i, (a, b)) in binary_data.iter().zip(existing.iter()).enumerate() {
                    if a != b {
                        eprintln!("  First difference at offset 0x{:04x}: generated 0x{:02x}, existing 0x{:02x}",
                                 i, a, b);
                        break;
                    }
                }
            }
            return Ok(Stats {
                total: 1,
                succeeded: 0,
                failed: 1,
            });
        }
    }

    // Write mode: save to output file
    let output_path = args.output.clone().unwrap_or_else(|| {
        let mut p = config.to_path_buf();
        p.set_extension("tprm");
        p
    });

    fs::write(&output_path, &binary_data)?;
    println!(
        "Binary written to: {} ({} bytes)",
        output_path.display(),
        binary_data.len()
    );

    Ok(Stats {
        total: 1,
        succeeded: 1,
        failed: 0,
    })
}

/// Print hex dump of binary data (similar to xxd)
fn hexdump(data: &[u8]) {
    for (i, chunk) in data.chunks(16).enumerate() {
        print!("{:08x}: ", i * 16);
        for (j, byte) in chunk.iter().enumerate() {
            if j == 8 {
                print!(" ");
            }
            print!("{:02x} ", byte);
        }
        // Pad if last chunk is short
        for j in chunk.len()..16 {
            if j == 8 {
                print!(" ");
            }
            print!("   ");
        }
        print!(" ");
        for byte in chunk {
            let c = if *byte >= 0x20 && *byte < 0x7f {
                *byte as char
            } else {
                '.'
            };
            print!("{}", c);
        }
        println!();
    }
}

/// Convert toml_edit Item to serde_json Value
fn toml_to_json(item: &toml_edit::Item) -> serde_json::Value {
    use serde_json::Value as Json;
    use toml_edit::Item;

    match item {
        Item::None => Json::Null,
        Item::Value(v) => toml_value_to_json(v),
        Item::Table(t) => {
            let mut map = serde_json::Map::new();
            for (k, v) in t.iter() {
                map.insert(k.to_string(), toml_to_json(v));
            }
            Json::Object(map)
        }
        Item::ArrayOfTables(aot) => Json::Array(
            aot.iter()
                .map(|t| {
                    let mut map = serde_json::Map::new();
                    for (k, v) in t.iter() {
                        map.insert(k.to_string(), toml_to_json(v));
                    }
                    Json::Object(map)
                })
                .collect(),
        ),
    }
}

fn toml_value_to_json(v: &toml_edit::Value) -> serde_json::Value {
    use serde_json::Value as Json;
    use toml_edit::Value;

    match v {
        Value::String(s) => Json::String(s.value().clone()),
        Value::Integer(i) => Json::Number((*i.value()).into()),
        Value::Float(f) => serde_json::Number::from_f64(*f.value())
            .map(Json::Number)
            .unwrap_or(Json::Null),
        Value::Boolean(b) => Json::Bool(*b.value()),
        Value::Datetime(dt) => Json::String(dt.value().to_string()),
        Value::Array(arr) => Json::Array(arr.iter().map(toml_value_to_json).collect()),
        Value::InlineTable(t) => {
            let mut map = serde_json::Map::new();
            for (k, v) in t.iter() {
                map.insert(k.to_string(), toml_value_to_json(v));
            }
            Json::Object(map)
        }
    }
}

fn run_batch(input_dir: &Path, args: &Args) -> Result<Stats, Error> {
    let output_dir = args.output.as_ref().ok_or_else(|| {
        Error::InvalidArgs("--output directory required for batch mode".to_string())
    })?;

    if !input_dir.is_dir() {
        return Err(Error::InvalidArgs(format!(
            "batch input must be a directory: {}",
            input_dir.display()
        )));
    }

    fs::create_dir_all(output_dir)?;

    // Collect matching files
    let files: Vec<PathBuf> = WalkDir::new(input_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().is_file())
        .filter(|e| {
            let path = e.path();
            let ext = path.extension().and_then(|s| s.to_str()).unwrap_or("");
            ext == "toml" || ext == "json"
        })
        .map(|e| e.into_path())
        .collect();

    if files.is_empty() {
        println!("No .toml or .json files found in {}", input_dir.display());
        return Ok(Stats {
            total: 0,
            succeeded: 0,
            failed: 0,
        });
    }

    println!(
        "Processing {} files from {} -> {}",
        files.len(),
        input_dir.display(),
        output_dir.display()
    );

    let mut succeeded = 0;
    let mut failed = 0;

    for input_path in &files {
        // Derive output path: preserve relative structure
        let rel_path = input_path.strip_prefix(input_dir).unwrap_or(input_path);
        let mut output_path = output_dir.join(rel_path);
        output_path.set_extension("bin");

        // Create parent directories if needed
        if let Some(parent) = output_path.parent() {
            let _ = fs::create_dir_all(parent);
        }

        match binary::config_to_binary(input_path, &output_path) {
            Ok(()) => {
                println!("  OK: {} -> {}", rel_path.display(), output_path.display());
                succeeded += 1;
            }
            Err(e) => {
                eprintln!("  FAIL: {}: {}", rel_path.display(), e);
                failed += 1;
            }
        }
    }

    Ok(Stats {
        total: files.len(),
        succeeded,
        failed,
    })
}
