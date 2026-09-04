//! cfg2bin: Convert TOML/JSON config files to binary blobs.
//!
//! Usage:
//!   cfg2bin --config foo.toml --output foo.tprm --fulluid 0x000100
//!   cfg2bin --config foo.toml                # sequence/blob, no prelude
//!   cfg2bin --batch ./configs/ --output ./binaries/
//!   cfg2bin --config foo.toml --verify existing.tprm  # verify match
//!
//! With --fulluid the output is a format A v3 component payload: the
//! serialized bytes behind the 20-byte prelude (magic, version, size,
//! fullUid, layout hash, CRC-32) that readers verify before loading.
//! Without it the output is the raw serialization -- the form sequence
//! binaries and generic blobs use.
//!
//! Batch mode compiles every .toml/.json under the input directory
//! into the output directory, preserving relative structure. Batch
//! outputs are raw (a directory carries no per-file fullUid); stamp
//! component payloads individually or through the build's packing
//! manifests (cmake/apex/Tprm.cmake).

use std::{
    fs,
    path::{Path, PathBuf},
    process::ExitCode,
};

use apex_rust_tools::tunable_params::{binary, cdef, manifest, payload, Error};
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

    /// Target fullUid (hex, e.g. 0x000100): stamp the v3 payload
    /// prelude for this component (single mode only)
    #[arg(long, conflicts_with = "batch")]
    fulluid: Option<String>,

    /// Write the payload's constraint rows (JSON) here: the source the
    /// build compiles into the on-board constraint table
    #[arg(long, value_name = "PATH", requires = "fulluid")]
    constraint_rows: Option<PathBuf>,

    /// Pin the serialization against a spec-defined struct
    /// (<apex_data.toml>:<StructName>): error unless the serialized
    /// walk's layout hash equals the spec's canonical hash. The
    /// build-time rail for raw (preludeless) products like sequence
    /// binaries, whose readers cannot check a stamped hash.
    #[arg(long, value_name = "MANIFEST:STRUCT", conflicts_with = "batch")]
    pin_spec: Option<String>,
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
    let data = binary::load_config(config)?;

    if let Some(pin) = &args.pin_spec {
        let (manifest_path, struct_name) = pin.rsplit_once(':').ok_or_else(|| {
            Error::InvalidArgs(format!(
                "--pin-spec wants <apex_data.toml>:<Struct>, got '{pin}'"
            ))
        })?;
        let manifest = manifest::parse_manifest(Path::new(manifest_path))?;
        let spec = manifest.fields.get(struct_name).ok_or_else(|| {
            Error::InvalidArgs(format!(
                "--pin-spec: no [[fields.{struct_name}]] in {manifest_path}"
            ))
        })?;
        let canonical = cdef::canonical_spec(struct_name, spec, &manifest.fields)?;
        let expected = payload::crc32(canonical.as_bytes());
        let (bytes, walk_hash) = binary::serialize_value_with_layout(&data)?;
        if walk_hash != expected {
            return Err(Error::Emit(format!(
                "{}: layout diverged from spec {struct_name} \
                 (walk hash 0x{walk_hash:08X}, spec hash 0x{expected:08X}, \
                 walk {} bytes, spec {} bytes) -- field names, order, sizes, \
                 and total must match {manifest_path}",
                config.display(),
                bytes.len(),
                cdef::layout_size(struct_name, spec, &manifest.fields)?,
            )));
        }
    }

    let binary_data = if let Some(uid_str) = &args.fulluid {
        let uid = parse_full_uid(uid_str)?;
        let (payload, layout_hash, rows) = binary::serialize_value_with_layout_and_rows(&data)?;
        if let Some(rows_path) = &args.constraint_rows {
            fs::write(
                rows_path,
                serde_json::to_string_pretty(&rows)
                    .map_err(|e| Error::Emit(format!("constraint rows: {e}")))?,
            )?;
        }
        payload::stamp(uid, layout_hash, &payload)?
    } else {
        binary::serialize_value(&data)?
    };

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

/// Parse a --fulluid argument: hex with or without the 0x prefix.
fn parse_full_uid(s: &str) -> Result<u32, Error> {
    let trimmed = s.trim();
    let digits = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
        .unwrap_or(trimmed);
    u32::from_str_radix(digits, 16)
        .map_err(|e| Error::InvalidArgs(format!("bad --fulluid '{s}': {e}")))
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
        output_path.set_extension("tprm");

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
