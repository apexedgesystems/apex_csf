//! tprm_pack: Pack, unpack, list, and diff TPRM archives.
//!
//! Replaces the orphaned Python tools/tprm_pack.py script.
//!
//! Usage:
//!   tprm_pack pack -o master.tprm -e 0x000000:executive.tprm -e 0x006600:polynomial_0.tprm
//!   tprm_pack unpack -i master.tprm -o ./unpacked/
//!   tprm_pack list -i master.tprm
//!   tprm_pack diff old.tprm new.tprm
//!
//! Entry format: fullUid:filepath
//!   fullUid = (componentId << 8) | instanceIndex
//!   Can be specified as hex (0x006600) or decimal (26112)
//!
//! Examples:
//!   Executive (componentId=0, instance=0):     0x000000 or 0
//!   Scheduler (componentId=1, instance=0):     0x000100 or 256
//!   PolynomialModel instance 0 (id=102):       0x006600 or 26112
//!   PolynomialModel instance 1 (id=102):       0x006601 or 26113

use std::{path::PathBuf, process::ExitCode};

use apex_rust_tools::tunable_params::{pack, Error};
use clap::{Parser, Subcommand};

#[derive(Parser, Debug)]
#[command(
    name = "tprm_pack",
    about = "Pack, unpack, and list TPRM archives",
    version
)]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Debug)]
enum Command {
    /// Pack multiple .tprm files into a single archive
    Pack {
        /// Entry in format fullUid:filepath (e.g., 0x006600:polynomial_0.tprm)
        #[arg(short, long = "entry", action = clap::ArgAction::Append)]
        entries: Vec<String>,

        /// RTS sequence in format slot:filepath (e.g., 4:rts/rts_001.rts)
        #[arg(short = 'r', long = "rts", action = clap::ArgAction::Append)]
        rts_entries: Vec<String>,

        /// ATS sequence in format slot:filepath (e.g., 6:ats/ats_001.ats)
        #[arg(short = 'a', long = "ats", action = clap::ArgAction::Append)]
        ats_entries: Vec<String>,

        /// Output packed .tprm file
        #[arg(short, long)]
        output: PathBuf,
    },

    /// Unpack a .tprm archive to individual files
    Unpack {
        /// Input packed .tprm file
        #[arg(short, long)]
        input: PathBuf,

        /// Output directory for unpacked files
        #[arg(short, long)]
        output: PathBuf,
    },

    /// List entries in a .tprm archive
    List {
        /// Input packed .tprm file
        #[arg(short, long)]
        input: PathBuf,
    },

    /// Compare two .tprm archives and show differences
    Diff {
        /// Old (baseline) .tprm archive
        old: PathBuf,

        /// New (candidate) .tprm archive
        new: PathBuf,

        /// Show unchanged entries too
        #[arg(long)]
        show_unchanged: bool,
    },
}

fn main() -> ExitCode {
    let args = Args::parse();

    match run(args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("Error: {e}");
            ExitCode::FAILURE
        }
    }
}

fn run(args: Args) -> Result<(), Error> {
    match args.command {
        Command::Pack {
            entries,
            rts_entries,
            ats_entries,
            output,
        } => {
            let mut pack_entries = parse_entries(&entries)?;

            // Parse RTS entries: slot -> fullUid = 0xFF0000 | slot
            for rts_str in &rts_entries {
                let (slot, path) = parse_sequence_entry(rts_str, "rts")?;
                pack_entries.push(pack::PackEntry {
                    full_uid: 0xFF0000 | (slot as u32),
                    path,
                });
            }

            // Parse ATS entries: slot -> fullUid = 0xFE0000 | slot
            for ats_str in &ats_entries {
                let (slot, path) = parse_sequence_entry(ats_str, "ats")?;
                pack_entries.push(pack::PackEntry {
                    full_uid: 0xFE0000 | (slot as u32),
                    path,
                });
            }

            if pack_entries.is_empty() {
                return Err(Error::InvalidArgs(
                    "at least one entry (-e, -r, or -a) required".to_string(),
                ));
            }

            println!("Packing {} entries:", pack_entries.len());
            for e in &pack_entries {
                let label = if e.full_uid & 0xFFFF00 == 0xFF0000 {
                    format!("RTS slot {}", e.full_uid & 0xFF)
                } else if e.full_uid & 0xFFFF00 == 0xFE0000 {
                    format!("ATS slot {}", e.full_uid & 0xFF)
                } else {
                    format!("fullUid 0x{:06x}", e.full_uid)
                };
                println!("  {}: {}", label, e.path.display());
            }

            let result = pack::pack(&pack_entries, &output)?;
            println!(
                "Packed {} entries -> {} ({} bytes)",
                result.entry_count,
                output.display(),
                result.total_size
            );
        }

        Command::Unpack { input, output } => {
            println!("Unpacking {} to {}", input.display(), output.display());

            let entries = pack::unpack(&input, &output)?;
            println!("Extracted {} entries:", entries.len());
            for e in &entries {
                println!(
                    "  {:06x}.tprm (fullUid 0x{:06x}, {} bytes)",
                    e.full_uid, e.full_uid, e.size
                );
            }
        }

        Command::List { input } => {
            let info = pack::list(&input)?;

            println!("Archive: {}", input.display());
            println!("Version: {}", info.version);
            println!("Entries: {}", info.entries.len());
            println!();
            println!("{:>10}  {:>10}  {:>10}", "fullUid", "Offset", "Size");
            println!("{:-<10}  {:-<10}  {:-<10}", "", "", "");

            for e in &info.entries {
                let label = if e.full_uid & 0xFFFF00 == 0xFF0000 {
                    format!("RTS:{:>3}", e.full_uid & 0xFF)
                } else if e.full_uid & 0xFFFF00 == 0xFE0000 {
                    format!("ATS:{:>3}", e.full_uid & 0xFF)
                } else {
                    format!("0x{:06x}", e.full_uid)
                };
                println!("{:>10}  {:>10}  {:>10}", label, e.offset, e.size);
            }
        }

        Command::Diff {
            old,
            new,
            show_unchanged,
        } => {
            let result = pack::diff(&old, &new)?;

            println!("Comparing: {} -> {}", old.display(), new.display());
            println!();

            let has_changes = !result.added.is_empty()
                || !result.removed.is_empty()
                || !result.modified.is_empty();

            if !result.added.is_empty() {
                println!("ADDED ({}):", result.added.len());
                for e in &result.added {
                    println!(
                        "  + fullUid 0x{:06x}: {} bytes",
                        e.full_uid,
                        e.new_size.unwrap_or(0)
                    );
                }
                println!();
            }

            if !result.removed.is_empty() {
                println!("REMOVED ({}):", result.removed.len());
                for e in &result.removed {
                    println!(
                        "  - fullUid 0x{:06x}: {} bytes",
                        e.full_uid,
                        e.old_size.unwrap_or(0)
                    );
                }
                println!();
            }

            if !result.modified.is_empty() {
                println!("MODIFIED ({}):", result.modified.len());
                for e in &result.modified {
                    let old_sz = e.old_size.unwrap_or(0);
                    let new_sz = e.new_size.unwrap_or(0);
                    let size_info = if old_sz == new_sz {
                        format!("{} bytes", old_sz)
                    } else {
                        format!("{} -> {} bytes", old_sz, new_sz)
                    };
                    let diff_info = e
                        .first_diff_offset
                        .map(|off| format!(" (first diff at 0x{:04x})", off))
                        .unwrap_or_default();
                    println!(
                        "  ~ fullUid 0x{:06x}: {}{}",
                        e.full_uid, size_info, diff_info
                    );
                }
                println!();
            }

            if show_unchanged && !result.unchanged.is_empty() {
                println!("UNCHANGED ({}):", result.unchanged.len());
                for e in &result.unchanged {
                    println!(
                        "  = fullUid 0x{:06x}: {} bytes",
                        e.full_uid,
                        e.old_size.unwrap_or(0)
                    );
                }
                println!();
            }

            if !has_changes {
                println!("No differences found.");
            } else {
                println!(
                    "Summary: {} added, {} removed, {} modified, {} unchanged",
                    result.added.len(),
                    result.removed.len(),
                    result.modified.len(),
                    result.unchanged.len()
                );
            }
        }
    }

    Ok(())
}

fn parse_entries(entries: &[String]) -> Result<Vec<pack::PackEntry>, Error> {
    let mut result = Vec::with_capacity(entries.len());

    for entry_str in entries {
        let parts: Vec<&str> = entry_str.splitn(2, ':').collect();
        if parts.len() != 2 {
            return Err(Error::InvalidArgs(format!(
                "invalid entry format '{}'. Expected 'fullUid:filepath' (e.g., 0x006600:file.tprm)",
                entry_str
            )));
        }

        let full_uid = parse_full_uid(parts[0]).map_err(|_| {
            Error::InvalidArgs(format!(
                "invalid fullUid '{}'. Use hex (0x006600) or decimal. Max 0xFFFFFF (24-bit)",
                parts[0]
            ))
        })?;

        result.push(pack::PackEntry {
            full_uid,
            path: PathBuf::from(parts[1]),
        });
    }

    Ok(result)
}

/// Parse a sequence entry in format "slot:filepath" (e.g., "4:rts/noop.rts").
fn parse_sequence_entry(s: &str, kind: &str) -> Result<(u8, PathBuf), Error> {
    let parts: Vec<&str> = s.splitn(2, ':').collect();
    if parts.len() != 2 {
        return Err(Error::InvalidArgs(format!(
            "invalid {} entry format '{}'. Expected 'slot:filepath' (e.g., 4:rts/noop.rts)",
            kind, s
        )));
    }

    let slot: u8 = parts[0].trim().parse().map_err(|_| {
        Error::InvalidArgs(format!(
            "invalid {} slot '{}'. Must be 0-255",
            kind, parts[0]
        ))
    })?;

    Ok((slot, PathBuf::from(parts[1])))
}

/// Parse fullUid from string, supporting hex (0x...) and decimal formats.
fn parse_full_uid(s: &str) -> Result<u32, std::num::ParseIntError> {
    let s = s.trim();
    if s.starts_with("0x") || s.starts_with("0X") {
        u32::from_str_radix(&s[2..], 16)
    } else {
        s.parse()
    }
}
