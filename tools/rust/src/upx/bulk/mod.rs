// ===================== src/upx/bulk/mod.rs =====================
// High-level bulk entrypoints wired together from the lower-level pieces.
//
// Modules:
// - options: BulkOptions / ArchiveMode / BulkResult
// - scan:    directory walking & pair discovery
// - tasks:   per-file task units (compress/decompress/verify/compare)
// - worker:  parallel work scheduler + archive/delete post-actions
//
// Notes:
// * Callers should pass validated/normalized BulkOptions (CLI does this).
// * These helpers are thin adapters that pick inputs, capture needed fields,
//   and delegate the heavy lifting to `worker::process_*` with the right task.

mod options;
mod scan;
mod tasks;
mod worker;

pub use options::{ArchiveMode, BulkOptions, BulkResult};

use std::io;
use std::path::Path;

use crate::upx::bulk::scan::{find_pairs, visit_files};
use crate::upx::bulk::tasks::{compare_task, compress_task, decompress_task, verify_task};
use crate::upx::bulk::worker::{process_files, process_pairs};

/// Decompress single or bulk `.upx` files.
///
/// Picks files by extension, then runs `upx -t` + `upx -d` per file.
/// Honors `overwrite` and optional output template/dir.
pub fn run_bulk_decompress(target: &Path, opts: &BulkOptions) -> io::Result<BulkResult> {
    // Optional safety check; worker will create dirs but cannot validate mode.
    opts.validate()?;

    let files = visit_files(target, opts.recursive, "upx");
    // Capture only what the closure needs to avoid borrowing `opts`.
    let overwrite = opts.overwrite;

    process_files(files, opts, move |input, out_dir, tpl| {
        decompress_task(input, out_dir, tpl, overwrite)
    })
}

/// Compress single or bulk `.so` files (or a single explicit file path).
///
/// Picks files by extension (or uses `target` if it's a file),
/// then runs `upx` with either provided flags or sensible defaults.
pub fn run_bulk_compress(target: &Path, opts: &BulkOptions) -> io::Result<BulkResult> {
    opts.validate()?;

    let files = if target.is_file() {
        vec![target.to_path_buf()]
    } else {
        visit_files(target, opts.recursive, "so")
    };

    let overwrite = opts.overwrite;
    // Clone once outside the worker closure to avoid moving from `opts`.
    let flags_owned = opts.flags.clone();

    process_files(files, opts, move |input, out_dir, tpl| {
        let flags = flags_owned.as_deref();
        compress_task(input, out_dir, tpl, overwrite, flags)
    })
}

/// Integrity test (`upx -t`) over `.upx` files.
pub fn run_bulk_verify(target: &Path, opts: &BulkOptions) -> io::Result<BulkResult> {
    opts.validate()?;

    let files = visit_files(target, opts.recursive, "upx");
    process_files(files, opts, move |input, _out_dir, _tpl| verify_task(input))
}

/// Pair `*.so` with `*.so.upx` in the same directory and compare bytes.
///
/// For each `*.upx`, derives `<basename>.so`, decompresses the UPX to temp,
/// then performs a streaming compare. Temp cleanup is controlled by `cleanup_temp`.
pub fn run_bulk_compare_pairs(
    dir: &Path,
    cleanup_temp: bool,
    print_sha: bool,
    opts: &BulkOptions,
) -> io::Result<BulkResult> {
    opts.validate()?;

    let pairs = find_pairs(dir, opts.recursive);
    process_pairs(pairs, opts, move |orig, upx| {
        compare_task(orig, upx, cleanup_temp, print_sha)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    // These tests avoid calling `upx` by ensuring there are no matching files.

    #[test]
    fn empty_dir_noop_decompress() {
        let td = TempDir::new().unwrap();
        let res = run_bulk_decompress(td.path(), &BulkOptions::default()).unwrap();
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
    }

    #[test]
    fn empty_dir_noop_compress() {
        let td = TempDir::new().unwrap();
        let res = run_bulk_compress(td.path(), &BulkOptions::default()).unwrap();
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
    }

    #[test]
    fn empty_dir_noop_verify() {
        let td = TempDir::new().unwrap();
        let res = run_bulk_verify(td.path(), &BulkOptions::default()).unwrap();
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
    }

    #[test]
    fn empty_dir_noop_compare_pairs() {
        let td = TempDir::new().unwrap();
        let res = run_bulk_compare_pairs(td.path(), true, false, &BulkOptions::default()).unwrap();
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
    }
}
