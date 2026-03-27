//! upx_tool — UPX helper (test, compress, decompress, verify, compare, patch)
//!
//! Subcommands:
//!   upx_tool test <path>                 # run `upx -t` on all *.upx in path
//!   upx_tool decompress <path>           # unpack *.upx -> *.unpacked (or template)
//!   upx_tool compress <path>             # *.so -> *.upx (flags override)
//!   upx_tool verify <path>               # alias of `test`
//!   upx_tool compare <dir>               # pairs *.so <-> *.so.upx by basename
//!   upx_tool patch --upx F.upx --target T.so [--orig O.so] [--backup-dir DIR]
//!
//! Notes:
//! * By default we check that the `upx` binary is available before parsing CLI args.
//! * For test environments, set `UPX_TOOL_NO_CHECK=1` to bypass that availability check.

use std::path::PathBuf;

use clap::{Args, Parser, Subcommand};

use apex_rust_tools::upx::{
    compare::compare_upx_to_original, ensure_upx_available, run_bulk_compare_pairs,
    run_bulk_compress, run_bulk_decompress, run_bulk_verify, test, util, ArchiveMode, BulkOptions,
};

const GREEN: &str = "\x1b[92m";
const YELLOW: &str = "\x1b[93m";
const RED: &str = "\x1b[91m";
const GRAY: &str = "\x1b[90m";
const RESET: &str = "\x1b[0m";

#[derive(Parser, Debug)]
#[command(name = "upx_tool", about = "UPX helper for single/bulk operations")]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand, Debug)]
enum Cmd {
    /// Run `upx -t` on a file or all .upx in a dir
    Test(PathArg),

    /// Decompress a file.upx or all .upx in a dir
    Decompress(BulkDecompress),

    /// Compress a file.so or all .so in a dir
    Compress(BulkCompress),

    /// Test integrity (`upx -t`) on all .upx
    Verify(BulkVerify),

    /// Compare pairs: foo.so <-> foo.so.upx by basename in dir
    Compare(ComparePairs),

    /// Patch: verify -> (optional compare) -> atomic replace target (with backup)
    Patch(PatchArgs),
}

// IMPORTANT: PathArg ONLY carries the positional path. `--recursive` comes from CommonBulk.
#[derive(Args, Debug, Clone)]
struct PathArg {
    /// file or directory
    #[arg()]
    path: PathBuf,
}

#[derive(Args, Debug)]
struct CommonBulk {
    /// recurse into subdirs
    #[arg(long)]
    recursive: bool,

    /// output directory (defaults to input dir)
    #[arg(long)]
    out_dir: Option<PathBuf>,

    /// name template (placeholders: {base}, {ext}, {ts}, {pid})
    #[arg(long)]
    name_template: Option<String>,

    /// overwrite existing output
    #[arg(long)]
    overwrite: bool,

    /// after success: delete the source
    #[arg(long)]
    delete: bool,

    /// after success: archive source as .tar.gz into dir (implies delete)
    #[arg(long)]
    archive_dir: Option<PathBuf>,

    /// run in parallel
    #[arg(long)]
    parallel: bool,

    /// number of worker threads (default 1)
    #[arg(long, default_value_t = 1)]
    jobs: usize,

    /// pin workers to given CPU list (e.g. "0,1,3")
    #[arg(long)]
    cores: Option<String>,

    /// stop at first error
    #[arg(long)]
    stop_on_error: bool,
}

#[derive(Args, Debug)]
struct BulkDecompress {
    /// file.upx or directory with .upx files
    #[command(flatten)]
    target: PathArg,

    #[command(flatten)]
    common: CommonBulk,
}

#[derive(Args, Debug)]
struct BulkCompress {
    /// file.so or directory with .so files
    #[command(flatten)]
    target: PathArg,

    /// flags passed to `upx` (default: --best --lzma)
    #[arg(long, value_name = "FLAGS", allow_hyphen_values = true)]
    flags: Option<String>,

    /// Anything after `--` is forwarded to UPX (e.g. `-- --best --lzma`)
    #[arg(trailing_var_arg = true)]
    extra_flags: Vec<String>,

    #[command(flatten)]
    common: CommonBulk,
}

#[derive(Args, Debug)]
struct BulkVerify {
    /// file.upx or directory with .upx
    #[command(flatten)]
    target: PathArg,

    #[command(flatten)]
    common: CommonBulk,
}

#[derive(Args, Debug)]
struct ComparePairs {
    /// directory with pairs: foo.so and foo.so.upx
    #[command(flatten)]
    dir: PathArg,

    /// print sha256 for each pair
    #[arg(long)]
    print_sha: bool,

    #[command(flatten)]
    common: CommonBulk,
}

#[derive(Args, Debug)]
struct PatchArgs {
    /// source UPX file
    #[arg(long)]
    upx: PathBuf,

    /// destination .so to replace atomically
    #[arg(long)]
    target: PathBuf,

    /// directory to place backups (default: target dir, suffix .bak-<ts>)
    #[arg(long)]
    backup_dir: Option<PathBuf>,

    /// also verify equality against an original (optional)
    #[arg(long)]
    orig: Option<PathBuf>,

    /// print sha256 during compare
    #[arg(long)]
    print_sha: bool,
}

fn main() {
    // Allow tests to bypass the upx binary presence check.
    let skip_check = std::env::var_os("UPX_TOOL_NO_CHECK").is_some();
    if !skip_check {
        if let Err(e) = ensure_upx_available() {
            eprintln!("{RED}error: upx not available: {e}{RESET}");
            std::process::exit(1);
        }
    }

    let cli = Cli::parse();

    match cli.cmd {
        Cmd::Test(arg) => {
            let opts = bulk_defaults();
            run_or_bail("Test", || run_bulk_verify(&arg.path, &opts));
        }
        Cmd::Decompress(args) => {
            let opts = build_bulk_options(&args.common, None);
            run_or_bail("Decompress", || {
                run_bulk_decompress(&args.target.path, &opts)
            });
        }
        Cmd::Compress(args) => {
            let opts = build_bulk_options(&args.common, args.flags.as_deref());
            run_or_bail("Compress", || run_bulk_compress(&args.target.path, &opts));
        }
        Cmd::Verify(args) => {
            let opts = build_bulk_options(&args.common, None);
            run_or_bail("Verify", || run_bulk_verify(&args.target.path, &opts));
        }
        Cmd::Compare(args) => {
            let opts = build_bulk_options(&args.common, None);
            run_or_bail("Compare", || {
                run_bulk_compare_pairs(&args.dir.path, /*cleanup*/ true, args.print_sha, &opts)
            });
        }
        Cmd::Patch(p) => {
            // ideal flow: test -> (optional) compare -> backup -> atomic replace
            if let Err(e) = test(&p.upx) {
                eprintln!("{RED}UPX test failed: {e}{RESET}");
                std::process::exit(1);
            }

            if let Some(orig) = &p.orig {
                match compare_upx_to_original(orig, &p.upx, /*cleanup*/ true, p.print_sha) {
                    Ok((ident, _tmp, shas)) => {
                        if let Some((so, upx)) = shas {
                            println!("sha256 orig={so}\nsha256 unpacked={upx}");
                        }
                        if !ident {
                            eprintln!("{YELLOW}warning: decompressed bytes != original{RESET}");
                        } else {
                            println!("{GREEN}compare: identical{RESET}");
                        }
                    }
                    Err(e) => {
                        eprintln!("{RED}compare failed: {e}{RESET}");
                        std::process::exit(1);
                    }
                }
            }

            // Backup current target
            let backup_dir = p.backup_dir.unwrap_or_else(|| {
                p.target
                    .parent()
                    .unwrap_or_else(|| std::path::Path::new("."))
                    .to_path_buf()
            });
            let backup_path = backup_dir.join(format!(
                "{}.bak-{}",
                p.target.file_name().unwrap().to_string_lossy(),
                util::now_ts()
            ));
            if let Some(parent) = backup_path.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            if let Err(e) = std::fs::copy(&p.target, &backup_path) {
                eprintln!("{RED}backup failed: {e}{RESET}");
                std::process::exit(1);
            }
            println!("{GREEN}backup:{RESET} {}", backup_path.display());

            // Decompress source into a sibling temp file then atomic rename
            let tmp = util::temp_unpacked_path(&p.upx);
            if let Err(e) = apex_rust_tools::upx::ops::decompress(&p.upx, &tmp, true) {
                eprintln!("{RED}decompress failed: {e}{RESET}");
                std::process::exit(1);
            }

            // Move tmp into place (rename is atomic on same fs)
            if let Err(e) = std::fs::rename(&tmp, &p.target) {
                eprintln!("{RED}atomic replace failed: {e}{RESET}");
                // attempt restore
                let _ = std::fs::rename(&backup_path, &p.target);
                std::process::exit(1);
            }

            println!("{GREEN}patched:{RESET} {}", p.target.display());
        }
    }
}

/// Minimal defaults; refined later via `build_bulk_options` -> `normalized()`.
fn bulk_defaults() -> BulkOptions {
    BulkOptions::default()
}

/// Build owned, validated, normalized bulk options from CLI inputs.
fn build_bulk_options(c: &CommonBulk, flags: Option<&str>) -> BulkOptions {
    let archive_mode = if c.archive_dir.is_some() {
        ArchiveMode::ArchiveTarGz
    } else if c.delete {
        ArchiveMode::Delete
    } else {
        ArchiveMode::Keep
    };

    let opts = BulkOptions {
        recursive: c.recursive,
        out_dir: c.out_dir.clone(),
        name_template: c.name_template.clone(),
        overwrite: c.overwrite,
        archive_mode,
        archive_dir: c.archive_dir.clone(),
        parallel: c.parallel || c.jobs > 1,
        jobs: c.jobs.max(1),
        cores: c.cores.clone(),
        stop_on_error: c.stop_on_error,
        flags: flags.map(|s| s.to_string()),
    }
    .normalized();

    if let Err(e) = opts.validate() {
        eprintln!("{RED}invalid options: {e}{RESET}");
        std::process::exit(1);
    }
    opts
}

/// Print a short, colored summary and exit(1) if any failures.
fn print_summary(kind: &str, res: apex_rust_tools::upx::bulk::BulkResult) {
    println!("\n{kind} Summary:");
    println!("{}ok{:>12}: {}{}", GREEN, "", res.ok, RESET);
    println!("{}skipped{:>7}: {}{}", GRAY, "", res.skipped, RESET);
    let color = if res.failed == 0 { GREEN } else { RED };
    println!("{}failed{:>8}: {}{}", color, "", res.failed, RESET);

    if res.failed > 0 {
        std::process::exit(1);
    }
}

/// Helper: run an operation, print a labeled summary on success, or bail with
/// a consistent, colored error message.
fn run_or_bail<F>(label: &str, f: F)
where
    F: FnOnce() -> std::io::Result<apex_rust_tools::upx::bulk::BulkResult>,
{
    match f() {
        Ok(res) => print_summary(label, res),
        Err(e) => {
            eprintln!("{RED}{label} failed: {e}{RESET}");
            std::process::exit(1);
        }
    }
}
