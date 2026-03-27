use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};
use std::thread;

use walkdir::WalkDir;

use super::compare::compare_upx_to_original;
use super::ops::{compress as upx_compress, decompress as upx_decompress, test};
use super::util::{
    archive_tar_gz, ensure_dir, make_output_name, parse_cores, pin_current_thread_to, unique_path,
    NameTemplate,
};

#[derive(Clone, Copy, Debug)]
pub enum ArchiveMode {
    Keep,         // leave source in place
    Delete,       // delete source after success
    ArchiveTarGz, // tar.gz into provided dir, remove source
}

#[derive(Clone, Copy, Debug)]
pub enum DecompressMode {
    Default, // out = {base}.unpacked
}

#[derive(Clone, Copy, Debug)]
pub enum CompressMode {
    Default, // out = {base}.upx
}

#[derive(Clone, Debug)]
pub struct BulkOptions<'a> {
    pub recursive: bool,
    pub out_dir: Option<&'a Path>,
    pub name_template: Option<NameTemplate<'a>>,
    pub overwrite: bool,
    pub archive_mode: ArchiveMode,
    pub archive_dir: Option<&'a Path>, // required if ArchiveTarGz
    pub parallel: bool,
    pub jobs: usize,            // number of worker threads
    pub cores: Option<&'a str>, // "0,1,3"
    pub stop_on_error: bool,
    pub flags: Option<&'a str>, // compression flags
}

#[derive(Default, Debug)]
pub struct BulkResult {
    pub ok: usize,
    pub skipped: usize,
    pub failed: usize,
}

fn visit_files(root: &Path, recursive: bool, pattern_ext: &str) -> Vec<PathBuf> {
    if root.is_file() {
        return vec![root.to_path_buf()];
    }
    let mut v = Vec::new();
    if recursive {
        for e in WalkDir::new(root).into_iter().filter_map(Result::ok) {
            if e.file_type().is_file() {
                if e.path().extension().and_then(|s| s.to_str()) == Some(pattern_ext) {
                    v.push(e.path().to_path_buf());
                }
            }
        }
    } else if let Ok(rd) = fs::read_dir(root) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_file() && p.extension().and_then(|s| s.to_str()) == Some(pattern_ext) {
                v.push(p);
            }
        }
    }
    v
}

pub fn run_bulk_decompress<'a>(target: &Path, opts: &BulkOptions<'a>) -> io::Result<BulkResult> {
    let files = visit_files(target, opts.recursive, "upx");
    process_files(files, opts, move |input, out_dir, tpl| {
        // output naming
        let out_name = if let Some(tpl_s) = tpl {
            make_output_name(input, &NameTemplate(tpl_s))
        } else {
            // foo.so.upx -> foo.so.unpacked
            let base = input.file_stem().unwrap_or_default(); // "foo.so"
            let mut o = PathBuf::from(base);
            o.set_file_name(format!(
                "{}.unpacked",
                o.file_name().unwrap().to_string_lossy()
            ));
            o
        };
        let out_path = unique_under(out_dir, out_name);

        // op
        test(input)?;
        let out_written = upx_decompress(input, &out_path, /*overwrite*/ true)?;

        Ok(out_written)
    })
}

pub fn run_bulk_compress<'a>(target: &Path, opts: &BulkOptions<'a>) -> io::Result<BulkResult> {
    // Accept either a single file or dir; filter to likely libs by default (.so).
    let files = if target.is_file() {
        vec![target.to_path_buf()]
    } else {
        visit_files(target, opts.recursive, "so")
    };

    let flags = opts.flags.map(|s| s.to_string());

    process_files(files, opts, move |input, out_dir, tpl| {
        let out_name = if let Some(tpl_s) = tpl {
            make_output_name(input, &NameTemplate(tpl_s))
        } else {
            let mut o = input
                .file_name()
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("out"));
            o.set_file_name(format!("{}.upx", o.file_name().unwrap().to_string_lossy()));
            o
        };
        let out_path = unique_under(out_dir, out_name);

        let out_written =
            upx_compress(input, &out_path, /*overwrite*/ true, flags.as_deref())?;

        Ok(out_written)
    })
}

pub fn run_bulk_verify<'a>(target: &Path, opts: &BulkOptions<'a>) -> io::Result<BulkResult> {
    let files = visit_files(target, opts.recursive, "upx");
    process_files(files, opts, move |input, _out_dir, _tpl| {
        test(input)?;
        Ok(input.to_path_buf())
    })
}

/// Pair `*.so` with `*.so.upx` in the same dir (or across provided dirs in a future iteration).
pub fn run_bulk_compare_pairs<'a>(
    dir: &Path,
    cleanup_temp: bool,
    print_sha: bool,
    opts: &BulkOptions<'a>,
) -> io::Result<BulkResult> {
    let mut pairs = Vec::new();
    // scan .upx files, derive .so
    for p in visit_files(dir, opts.recursive, "upx") {
        if let Some(stem) = p.file_stem().and_then(|s| s.to_str()) {
            // stem is "foo.so"
            let orig = p.parent().unwrap_or_else(|| Path::new(".")).join(stem);
            pairs.push((orig, p));
        }
    }

    process_pairs(pairs, opts, move |orig, upx| {
        let (_identical, _tmp, _shas) =
            compare_upx_to_original(&orig, &upx, cleanup_temp, print_sha)?;
        Ok(upx)
    })
}

// ----- internals -----

fn unique_under(out_dir: Option<&Path>, out_name: PathBuf) -> PathBuf {
    let mut out = out_name;
    if let Some(od) = out_dir {
        out = od.join(out);
    }
    unique_path(out)
}

fn process_files<F>(files: Vec<PathBuf>, opts: &BulkOptions, f: F) -> io::Result<BulkResult>
where
    F: Fn(&Path, Option<&Path>, Option<&str>) -> io::Result<PathBuf> + Send + Sync + 'static,
{
    if files.is_empty() {
        return Ok(BulkResult::default());
    }
    if let Some(od) = opts.out_dir {
        ensure_dir(od)?;
    }
    if let Some(ad) = opts.archive_dir {
        ensure_dir(ad)?;
    }

    // Shared collections / counters
    let files_arc = Arc::new(files);
    let idx = Arc::new(AtomicUsize::new(0));
    let errs = Arc::new(AtomicUsize::new(0));
    let oks = Arc::new(AtomicUsize::new(0));
    let total = files_arc.len();

    // Copy options needed inside threads (owned, thread-safe)
    let out_dir_owned = opts.out_dir.map(|p| p.to_path_buf());
    let tpl_owned = opts.name_template.map(|nt| nt.0.to_string());
    let cores_owned = opts.cores.map(|s| s.to_string());
    let stop_on_error = opts.stop_on_error;
    let archive_mode = opts.archive_mode;
    let archive_dir_owned = opts.archive_dir.map(|p| p.to_path_buf());

    // Wrap the per-file function
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
        let cores_owned = cores_owned.clone();
        let archive_dir_owned = archive_dir_owned.clone();

        threads.push(thread::spawn(move || {
            // Optional CPU pinning
            if let Some(spec) = &cores_owned {
                if !spec.is_empty() {
                    let cores = parse_cores(spec);
                    if !cores.is_empty() {
                        let cpu = cores[t % cores.len()];
                        let _ = pin_current_thread_to(cpu);
                    }
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

                // Do the operation (decompress/compress/verify)
                let res = (func)(p.as_path(), out_dir_ref, tpl_ref);

                // Post-op: handle archive/delete if requested
                let res = res.and_then(|written| match archive_mode {
                    ArchiveMode::Keep => Ok(written),
                    ArchiveMode::Delete => {
                        let _ = fs::remove_file(p);
                        Ok(written)
                    }
                    ArchiveMode::ArchiveTarGz => {
                        let ad = archive_dir_owned
                            .as_deref()
                            .ok_or_else(|| io::Error::other("archive dir required"))?;
                        let _ = archive_tar_gz(p, ad)?;
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
                            // fast-forward index to end
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

fn process_pairs<F>(
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
    let idx = Arc::new(AtomicUsize::new(0));
    let errs = Arc::new(AtomicUsize::new(0));
    let oks = Arc::new(AtomicUsize::new(0));
    let total = pairs_arc.len();

    let func = Arc::new(f);

    let cores_owned = opts.cores.map(|s| s.to_string());
    let stop_on_error = opts.stop_on_error;

    let n_workers = if opts.parallel { opts.jobs.max(1) } else { 1 };
    let mut threads = Vec::with_capacity(n_workers);

    for t in 0..n_workers {
        let pairs_arc = Arc::clone(&pairs_arc);
        let func = Arc::clone(&func);
        let idx = Arc::clone(&idx);
        let errs = Arc::clone(&errs);
        let oks = Arc::clone(&oks);
        let cores_owned = cores_owned.clone();

        threads.push(thread::spawn(move || {
            if let Some(spec) = &cores_owned {
                if !spec.is_empty() {
                    let cores = parse_cores(spec);
                    if !cores.is_empty() {
                        let cpu = cores[t % cores.len()];
                        let _ = pin_current_thread_to(cpu);
                    }
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
