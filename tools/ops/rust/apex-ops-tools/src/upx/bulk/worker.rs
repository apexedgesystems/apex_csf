// ===================== src/upx/bulk/worker.rs =====================
// Work scheduler used by bulk entrypoints.
//
// Responsibilities:
// - Fan out a list of paths (or path pairs) across N worker threads.
// - Optionally pin workers to specific CPUs.
// - Run the provided task function per item.
// - Apply post-action policy to *source* files (Keep/Delete/ArchiveTarGz).
// - Accumulate simple success/failure counters.
//
// Notes:
// * We clone owned options once outside the thread loop to avoid borrowing `opts`.
// * CPU pinning is best-effort (no error is fatal).
// * Counters use atomics; memory ordering isn't critical here—Relaxed would do—but
//   we keep SeqCst for simplicity and readability.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};
use std::thread;

use super::options::{ArchiveMode, BulkOptions, BulkResult};
use crate::upx::util::{archive_tar_gz, parse_cores, pin_current_thread_to};

/// Internal helper: pre-parse core list once (if provided).
fn parse_core_list(spec: &Option<String>) -> Option<Vec<usize>> {
    spec.as_ref().map(|s| parse_cores(s))
}

/// Process a list of files using a per-file task `f`.
///
/// The task receives:
/// - `&Path` for the input file
/// - optional output directory
/// - optional name template string slice
///
/// Returns a `BulkResult` with counts of ok/failed/skipped.
pub fn process_files<F>(files: Vec<PathBuf>, opts: &BulkOptions, f: F) -> io::Result<BulkResult>
where
    F: Fn(&Path, Option<&Path>, Option<&str>) -> io::Result<PathBuf> + Send + Sync + 'static,
{
    if files.is_empty() {
        return Ok(BulkResult::default());
    }

    // Pre-create output/archive dirs if provided (idempotent).
    if let Some(od) = opts.out_dir.as_deref() {
        crate::upx::util::ensure_dir(od)?;
    }
    if let Some(ad) = opts.archive_dir.as_deref() {
        crate::upx::util::ensure_dir(ad)?;
    }

    // Shared collections / counters
    let files_arc = Arc::new(files);
    let total = files_arc.len();
    let idx = Arc::new(AtomicUsize::new(0));
    let errs = Arc::new(AtomicUsize::new(0));
    let oks = Arc::new(AtomicUsize::new(0));

    // Owned options for threads
    let out_dir_owned = opts.out_dir.clone();
    let tpl_owned = opts.name_template.clone();
    let cores_list = parse_core_list(&opts.cores);
    let stop_on_error = opts.stop_on_error;
    let archive_mode = opts.archive_mode;
    let archive_dir_owned = opts.archive_dir.clone();

    // Wrap task in Arc so each thread can call it
    let func = Arc::new(f);

    let n_workers = if opts.parallel { opts.jobs.max(1) } else { 1 };
    let mut threads = Vec::with_capacity(n_workers);

    for t in 0..n_workers {
        let files_arc = Arc::clone(&files_arc);
        let func = Arc::clone(&func);
        let idx = Arc::clone(&idx);
        let errs = Arc::clone(&errs);
        let oks = Arc::clone(&oks);
        let out_dir_owned = out_dir_owned.clone();
        let tpl_owned = tpl_owned.clone();
        let archive_dir_owned = archive_dir_owned.clone();
        let cores_list = cores_list.clone(); // small vec, cheap to clone

        threads.push(thread::spawn(move || {
            // Optional CPU pinning
            if let Some(ref cores) = cores_list {
                if !cores.is_empty() {
                    let cpu = cores[t % cores.len()];
                    let _ = pin_current_thread_to(cpu);
                }
            }

            loop {
                let i = idx.fetch_add(1, Ordering::SeqCst);
                if i >= total {
                    break;
                }
                let p = &files_arc[i];

                // Build per-call params
                let out_dir_ref = out_dir_owned.as_deref();
                let tpl_ref = tpl_owned.as_deref();

                // Execute task
                let res = (func)(p.as_path(), out_dir_ref, tpl_ref);

                // Post-op action (keep/delete/archive)
                let res = res.and_then(|written| match archive_mode {
                    ArchiveMode::Keep => Ok(written),
                    ArchiveMode::Delete => {
                        let _ = fs::remove_file(p); // best-effort
                        Ok(written)
                    }
                    ArchiveMode::ArchiveTarGz => {
                        // `validate()` should have ensured this is present; still handle gracefully.
                        let ad = archive_dir_owned
                            .as_deref()
                            .ok_or_else(|| io::Error::other("archive dir required"))?;
                        let _ = archive_tar_gz(p, ad)?; // best-effort; propagate tar failure
                        Ok(written)
                    }
                });

                match res {
                    Ok(_) => {
                        oks.fetch_add(1, Ordering::SeqCst);
                    }
                    Err(e) => {
                        eprintln!("\x1b[91m[FAIL]\x1b[0m {}: {}", p.display(), e);
                        errs.fetch_add(1, Ordering::SeqCst);
                        if stop_on_error {
                            // Fast-forward index to end; other workers will exit on next fetch.
                            idx.store(total, Ordering::SeqCst);
                            break;
                        }
                    }
                }
            }
        }));
    }

    for t in threads {
        let _ = t.join();
    }

    let okc = oks.load(Ordering::SeqCst);
    let failc = errs.load(Ordering::SeqCst);
    let skipc = total - okc - failc;

    Ok(BulkResult {
        ok: okc,
        skipped: skipc,
        failed: failc,
    })
}

/// Process a list of (original, upx) pairs using a per-pair task `f`.
pub fn process_pairs<F>(
    pairs: Vec<(PathBuf, PathBuf)>,
    opts: &BulkOptions,
    f: F,
) -> io::Result<BulkResult>
where
    F: Fn(PathBuf, PathBuf) -> io::Result<PathBuf> + Send + Sync + 'static,
{
    if pairs.is_empty() {
        return Ok(BulkResult::default());
    }

    let pairs_arc = Arc::new(pairs);
    let total = pairs_arc.len();
    let idx = Arc::new(AtomicUsize::new(0));
    let errs = Arc::new(AtomicUsize::new(0));
    let oks = Arc::new(AtomicUsize::new(0));

    let func = Arc::new(f);

    let cores_list = parse_core_list(&opts.cores);
    let stop_on_error = opts.stop_on_error;

    let n_workers = if opts.parallel { opts.jobs.max(1) } else { 1 };
    let mut threads = Vec::with_capacity(n_workers);

    for t in 0..n_workers {
        let pairs_arc = Arc::clone(&pairs_arc);
        let func = Arc::clone(&func);
        let idx = Arc::clone(&idx);
        let errs = Arc::clone(&errs);
        let oks = Arc::clone(&oks);
        let cores_list = cores_list.clone();

        threads.push(thread::spawn(move || {
            // Optional CPU pinning
            if let Some(ref cores) = cores_list {
                if !cores.is_empty() {
                    let cpu = cores[t % cores.len()];
                    let _ = pin_current_thread_to(cpu);
                }
            }

            loop {
                let i = idx.fetch_add(1, Ordering::SeqCst);
                if i >= total {
                    break;
                }
                let (orig, upx) = &pairs_arc[i];

                let res = (func)(orig.clone(), upx.clone());

                match res {
                    Ok(_) => {
                        oks.fetch_add(1, Ordering::SeqCst);
                    }
                    Err(e) => {
                        eprintln!("\x1b[91m[FAIL]\x1b[0m {}: {}", upx.display(), e);
                        errs.fetch_add(1, Ordering::SeqCst);
                        if stop_on_error {
                            idx.store(total, Ordering::SeqCst);
                            break;
                        }
                    }
                }
            }
        }));
    }

    for t in threads {
        let _ = t.join();
    }

    let okc = oks.load(Ordering::SeqCst);
    let failc = errs.load(Ordering::SeqCst);
    let skipc = total - okc - failc;

    Ok(BulkResult {
        ok: okc,
        skipped: skipc,
        failed: failc,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn opts_serial() -> BulkOptions {
        BulkOptions {
            parallel: false,
            jobs: 1,
            ..Default::default()
        }
    }

    #[test]
    fn process_files_noop_returns_zeroes() {
        let res = process_files(Vec::new(), &opts_serial(), |_p, _od, _tpl| {
            Ok(PathBuf::from("unused"))
        })
        .unwrap();
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
    }

    #[test]
    fn process_files_sequential_success() {
        let td = TempDir::new().unwrap();
        let p1 = td.path().join("a.so.upx");
        let p2 = td.path().join("b.so.upx");
        fs::File::create(&p1).unwrap();
        fs::File::create(&p2).unwrap();

        let files = vec![p1.clone(), p2.clone()];

        let res = process_files(files, &opts_serial(), |_p, _od, _tpl| {
            // pretend we wrote something
            Ok(PathBuf::from("out"))
        })
        .unwrap();

        assert_eq!(res.ok, 2);
        assert_eq!(res.failed, 0);
        assert_eq!(res.skipped, 0);
        // sources should remain (Keep mode by default)
        assert!(p1.exists() && p2.exists());
    }

    #[test]
    fn process_files_delete_mode_removes_sources() {
        let td = TempDir::new().unwrap();
        let p1 = td.path().join("a.upx");
        let p2 = td.path().join("b.upx");
        fs::File::create(&p1).unwrap();
        fs::File::create(&p2).unwrap();

        let files = vec![p1.clone(), p2.clone()];

        let mut opts = opts_serial();
        opts.archive_mode = ArchiveMode::Delete;

        let res = process_files(files, &opts, |_p, _od, _tpl| Ok(PathBuf::from("out"))).unwrap();
        assert_eq!(res.ok, 2);
        assert!(!p1.exists() && !p2.exists());
    }

    #[test]
    fn process_files_stop_on_error_stops_early() {
        use std::sync::atomic::AtomicUsize;

        let td = TempDir::new().unwrap();
        let p1 = td.path().join("a.upx");
        let p2 = td.path().join("b.upx");
        let p3 = td.path().join("c.upx");
        fs::File::create(&p1).unwrap();
        fs::File::create(&p2).unwrap();
        fs::File::create(&p3).unwrap();

        let files = vec![p1.clone(), p2.clone(), p3.clone()];

        let mut opts = opts_serial();
        opts.stop_on_error = true;

        // Use atomic counter so the closure can stay `Fn + Sync`.
        let seen = Arc::new(AtomicUsize::new(0));
        let seen_cl = Arc::clone(&seen);

        let res = process_files(files, &opts, move |p, _od, _tpl| {
            seen_cl.fetch_add(1, Ordering::SeqCst);
            if p.ends_with("a.upx") {
                Err(io::Error::other("boom"))
            } else {
                Ok(PathBuf::from("out"))
            }
        })
        .unwrap();

        // With jobs=1, the first error should halt processing.
        assert_eq!(res.ok, 0);
        assert_eq!(res.failed, 1);
        assert_eq!(res.skipped, 2);
        assert_eq!(seen.load(Ordering::SeqCst), 1);
        // sources remain (Keep mode)
        assert!(p1.exists() && p2.exists() && p3.exists());
    }

    #[test]
    fn process_pairs_mixed_results() {
        let td = TempDir::new().unwrap();
        let a = (td.path().join("a.so"), td.path().join("a.so.upx"));
        let b = (td.path().join("b.so"), td.path().join("b.so.upx"));
        // touch only the upx side; logic under test doesn't require real files
        fs::File::create(&a.1).unwrap();
        fs::File::create(&b.1).unwrap();

        let pairs = vec![a.clone(), b.clone()];
        let opts = opts_serial();

        let res = process_pairs(pairs, &opts, |orig, upx| {
            if upx.ends_with("a.so.upx") {
                Ok(upx) // ok
            } else {
                let _ = orig; // unused in this test
                Err(io::Error::other("fail"))
            }
        })
        .unwrap();

        assert_eq!(res.ok, 1);
        assert_eq!(res.failed, 1);
        assert_eq!(res.skipped, 0);
    }
}
