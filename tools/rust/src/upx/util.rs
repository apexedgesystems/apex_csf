// ===================== src/upx/util.rs =====================
// General utilities used across the UPX helpers:
// - now_ts(): local timestamp "YYYYMMDD_HHMMSS" without chrono
// - NameTemplate + make_output_name(): templated output naming
// - unique_path(): add numeric suffix to avoid collisions
// - ensure_dir(), move_file(), remove_if_exists(): filesystem helpers
// - archive_tar_gz(): shell out to `tar` to archive and then delete
// - temp_unpacked_path(): derive `<file>.unpacked.tmp` neighbor path
// - parse_cores(), pin_current_thread_to(): CPU pinning helpers (no-op pin)
//
// Notes:
// * `now_ts` uses libc for minimal deps. If formatting fails we return a
//   sentinel "00000000_000000" rather than erroring.
// * `move_file` is atomic on the same filesystem; it also ensures the
//   destination parent directory exists.
// * `archive_tar_gz` requires `/usr/bin/tar` (or `tar` in PATH). We
//   pre-create the archive directory and remove the source after success.

use std::ffi::OsStr;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

use nix::libc; // for time/strftime only

/// Local timestamp "YYYYMMDD_HHMMSS" using libc (no chrono dependency).
pub fn now_ts() -> String {
    unsafe {
        let t = libc::time(std::ptr::null_mut());
        let mut tm: libc::tm = std::mem::zeroed();
        // Convert to local time
        if libc::localtime_r(&t, &mut tm).is_null() {
            return "00000000_000000".to_string();
        }

        // Format with strftime
        let mut buf = [0u8; 32];
        let fmt = b"%Y%m%d_%H%M%S\0";
        let n = libc::strftime(
            buf.as_mut_ptr() as *mut libc::c_char,
            buf.len(),
            fmt.as_ptr() as *const libc::c_char,
            &tm as *const libc::tm,
        );
        if n == 0 {
            "00000000_000000".to_string()
        } else {
            String::from_utf8_lossy(&buf[..n as usize]).into_owned()
        }
    }
}

/// Simple template: {base}, {ext}, {ts}, {pid}
#[derive(Clone, Copy, Debug)]
pub struct NameTemplate<'a>(pub &'a str);

/// Build an output name by applying a `NameTemplate` to `input`.
///
/// Placeholders:
/// - `{base}`: `file_stem()` (e.g., "libfoo.so" for "libfoo.so.upx")
/// - `{ext}`: `extension()` without dot (e.g., "upx")
/// - `{ts}`:  local timestamp "YYYYMMDD_HHMMSS"
/// - `{pid}`: current process id
///
/// If the rendered string is empty, falls back to `input.with_extension("out")`.
pub fn make_output_name(input: &Path, tpl: &NameTemplate) -> PathBuf {
    let base = input.file_stem().and_then(OsStr::to_str).unwrap_or("out");
    let ext = input.extension().and_then(OsStr::to_str).unwrap_or("");
    let ts = now_ts();
    let pid = std::process::id();

    let s = tpl
        .0
        .replace("{base}", base)
        .replace("{ext}", ext)
        .replace("{ts}", &ts)
        .replace("{pid}", &pid.to_string());

    if s.is_empty() {
        input.with_extension("out")
    } else {
        PathBuf::from(s)
    }
}

/// If `candidate` exists, return a path with `.N` appended to the filename,
/// incrementing N until a non-existing path is found.
pub fn unique_path(candidate: PathBuf) -> PathBuf {
    if !candidate.exists() {
        return candidate;
    }
    let mut i = 1usize;
    loop {
        let mut c = candidate.clone();
        let suffix = format!(".{}", i);
        let name = c
            .file_name()
            .and_then(OsStr::to_str)
            .map(|n| format!("{n}{suffix}"))
            .unwrap_or_else(|| format!("out{suffix}"));
        c.set_file_name(name);
        if !c.exists() {
            return c;
        }
        i += 1;
    }
}

/// Ensure a directory exists. An empty path is a no-op.
pub fn ensure_dir(path: &Path) -> io::Result<()> {
    if path.as_os_str().is_empty() {
        return Ok(());
    }
    fs::create_dir_all(path)
}

/// Remove file if it exists; otherwise it’s a no-op.
pub fn remove_if_exists(p: &Path) -> io::Result<()> {
    if p.exists() {
        fs::remove_file(p)
    } else {
        Ok(())
    }
}

/// Move/rename `src` to `dst`, creating the destination parent directory if needed.
/// Note: `rename` is atomic only when source and destination are on the same filesystem.
pub fn move_file(src: &Path, dst: &Path) -> io::Result<()> {
    if let Some(parent) = dst.parent() {
        ensure_dir(parent)?;
    }
    fs::rename(src, dst)
}

/// Archive file by creating a `.tar.gz` in `archive_dir` and then deleting the source.
/// Returns the path of the created archive.
pub fn archive_tar_gz(src_file: &Path, archive_dir: &Path) -> io::Result<PathBuf> {
    ensure_dir(archive_dir)?;
    let base = src_file
        .file_name()
        .and_then(OsStr::to_str)
        .unwrap_or("file");
    let out = archive_dir.join(format!("{}-{}.tar.gz", base, now_ts()));

    // Use system tar for minimal footprint: tar -czf <out> -C <dir> <file>
    let (dir, file) = match (src_file.parent(), src_file.file_name()) {
        (Some(d), Some(f)) => (d, f),
        _ => (Path::new("."), src_file.as_os_str()),
    };

    let ok = Command::new("tar")
        .args(["-czf"])
        .arg(&out)
        .args(["-C"])
        .arg(dir)
        .arg(file)
        .status()?
        .success();

    if !ok {
        return Err(io::Error::other("tar archive failed"));
    }

    // Remove the source after a successful archive (archive mode semantics)
    fs::remove_file(src_file)?;
    Ok(out)
}

/// Build a temp unpacked path next to the input: `<file>.unpacked.tmp`
pub fn temp_unpacked_path(packed_upx: &Path) -> PathBuf {
    let mut p = packed_upx.to_path_buf();
    let fname = p.file_name().and_then(OsStr::to_str).unwrap_or("temp");
    p.set_file_name(format!("{fname}.unpacked.tmp"));
    p
}

/// Parse CPU list like "0,1,3" into indices.
pub fn parse_cores(spec: &str) -> Vec<usize> {
    spec.split(',')
        .filter_map(|s| s.trim().parse::<usize>().ok())
        .collect()
}

/// Best-effort CPU pinning. Currently a no-op unless you add a proper affinity implementation.
/// (We avoid brittle libc layout differences here.)
pub fn pin_current_thread_to(_cpu: usize) -> io::Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::process::Stdio;
    use tempfile::TempDir;

    #[test]
    fn now_ts_shape() {
        let ts = now_ts();
        assert_eq!(ts.len(), 15, "expected YYYYMMDD_HHMMSS");
        assert_eq!(&ts[8..9], "_");
        assert!(ts.chars().enumerate().all(|(i, c)| if i == 8 {
            c == '_'
        } else {
            c.is_ascii_digit()
        }));
    }

    #[test]
    fn template_builds() {
        let p = Path::new("/tmp/libfoo.so.upx");
        let out = make_output_name(p, &NameTemplate("{base}-{ext}-{pid}-{ts}"));
        let s = out.file_name().unwrap().to_string_lossy();
        assert!(s.starts_with("libfoo.so-upx-"), "got: {s}");
    }

    #[test]
    fn unique_suffix_increments_when_exists() {
        let td = TempDir::new().unwrap();
        let base = td.path().join("x");
        fs::write(&base, b"hi").unwrap();
        let u = unique_path(base.clone());
        assert_ne!(u, base);
        assert!(u.file_name().unwrap().to_string_lossy().starts_with("x."));
    }

    #[test]
    fn temp_unpacked_derivation() {
        let p = Path::new("/opt/lib/libbar.so.upx");
        let t = temp_unpacked_path(p);
        assert_eq!(t.file_name().unwrap(), "libbar.so.upx.unpacked.tmp");
    }

    #[test]
    fn parse_cores_parses_numbers() {
        let v = parse_cores("0, 2,7,  9");
        assert_eq!(v, vec![0, 2, 7, 9]);
        let v2 = parse_cores("");
        assert!(v2.is_empty());
    }

    #[test]
    fn move_file_creates_parent() {
        let td = TempDir::new().unwrap();
        let src = td.path().join("src.bin");
        let dst = td.path().join("a/b/c/dst.bin");
        fs::write(&src, b"data").unwrap();
        move_file(&src, &dst).unwrap();
        assert!(!src.exists());
        assert!(dst.exists());
    }

    #[test]
    fn archive_tar_gz_creates_archive_and_removes_source_or_skips() {
        // Skip if `tar` is not available
        let have_tar = Command::new("tar")
            .arg("--version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        if !have_tar {
            eprintln!("skipping: tar not available");
            return;
        }

        let td = TempDir::new().unwrap();
        let src_dir = td.path().join("src");
        let arc_dir = td.path().join("arc");
        fs::create_dir(&src_dir).unwrap();
        let f = src_dir.join("file.bin");
        fs::write(&f, b"abc").unwrap();

        let out = archive_tar_gz(&f, &arc_dir).unwrap();
        assert!(out.exists(), "archive should exist");
        assert!(out
            .extension()
            .and_then(|e| e.to_str())
            .unwrap_or("")
            .ends_with("gz"));
        assert!(!f.exists(), "source should be removed");
    }
}
