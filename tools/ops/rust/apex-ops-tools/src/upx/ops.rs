// ===================== src/upx/ops.rs =====================
// Thin wrappers around the system `upx` binary.
// - ensure_upx_available: quick capability probe (`upx --version`)
// - test:                 integrity check (`upx -t <file.upx>`)
// - decompress:           inflate packed file (`upx -d <in> -o <out>`)
// - compress:             pack file with flags (`upx [--best --lzma]|<flags> <in> -o <out>`)
//
// Notes:
// * We use `OsString`/`OsStr` to avoid UTF-8 assumptions on paths.
// * `overwrite` guard returns early before spawning `upx`.
// * Error messages include paths for easier debugging.

use std::ffi::{OsStr, OsString};
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// Invoke `upx` with the provided arguments, returning true on exit status success.
fn run_upx<I, S>(args: I) -> io::Result<bool>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    let status = Command::new("upx")
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::inherit())
        .status()?;
    Ok(status.success())
}

/// Check that `upx` is callable.
pub fn ensure_upx_available() -> io::Result<()> {
    if run_upx([OsString::from("--version")])? {
        Ok(())
    } else {
        Err(io::Error::other("upx not available"))
    }
}

/// `upx -t <file.upx>`
pub fn test(packed_upx: &Path) -> io::Result<()> {
    let args = [OsString::from("-t"), packed_upx.as_os_str().to_os_string()];
    if run_upx(args)? {
        Ok(())
    } else {
        Err(io::Error::other(format!(
            "upx test failed: {}",
            packed_upx.display()
        )))
    }
}

/// `upx -d <file.upx> -o <out>`
pub fn decompress(packed_upx: &Path, out_path: &Path, overwrite: bool) -> io::Result<PathBuf> {
    if !overwrite && out_path.exists() {
        return Err(io::Error::other(format!(
            "output exists; pass overwrite: {}",
            out_path.display()
        )));
    }

    let args = [
        OsString::from("-d"),
        packed_upx.as_os_str().to_os_string(),
        OsString::from("-o"),
        out_path.as_os_str().to_os_string(),
    ];

    let ok = run_upx(args)?;
    if ok {
        Ok(out_path.to_path_buf())
    } else {
        Err(io::Error::other(format!(
            "upx decompress failed: {} -> {}",
            packed_upx.display(),
            out_path.display()
        )))
    }
}

/// `upx --best --lzma <in> -o <out>` (flags override allowed)
pub fn compress(
    input: &Path,
    out_path: &Path,
    overwrite: bool,
    flags: Option<&str>,
) -> io::Result<PathBuf> {
    if !overwrite && out_path.exists() {
        return Err(io::Error::other(format!(
            "output exists; pass overwrite: {}",
            out_path.display()
        )));
    }

    // Build flag vector: either user-provided tokens or sensible defaults.
    let mut args: Vec<OsString> = Vec::new();
    match flags.map(str::trim).filter(|s| !s.is_empty()) {
        Some(f) => {
            for tok in f.split_whitespace() {
                args.push(OsString::from(tok));
            }
        }
        None => {
            args.push(OsString::from("--best"));
            args.push(OsString::from("--lzma"));
        }
    }

    // Append the input + output args.
    args.push(input.as_os_str().to_os_string());
    args.push(OsString::from("-o"));
    args.push(out_path.as_os_str().to_os_string());

    let ok = run_upx(args)?;
    if ok {
        Ok(out_path.to_path_buf())
    } else {
        Err(io::Error::other(format!(
            "upx compress failed: {} -> {}",
            input.display(),
            out_path.display()
        )))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    fn has_upx() -> bool {
        ensure_upx_available().is_ok()
    }

    #[test]
    fn version_check() {
        // Do not fail CI if upx isn't installed; just skip.
        if !has_upx() {
            eprintln!("skipping: upx not installed");
            return;
        }
        assert!(ensure_upx_available().is_ok());
    }

    #[test]
    fn decompress_overwrite_guard_blocks() {
        let td = TempDir::new().unwrap();
        let out = td.path().join("out.so");
        fs::write(&out, b"exists").unwrap();

        let res = decompress(
            Path::new("/no/such/file.upx"),
            &out,
            /*overwrite*/ false,
        );
        assert!(
            res.is_err(),
            "expected early error when output exists and overwrite=false"
        );
    }

    #[test]
    fn compress_overwrite_guard_blocks() {
        let td = TempDir::new().unwrap();
        let out = td.path().join("out.so.upx");
        fs::write(&out, b"exists").unwrap();

        let res = compress(
            Path::new("/no/such/file.so"),
            &out,
            /*overwrite*/ false,
            None,
        );
        assert!(
            res.is_err(),
            "expected early error when output exists and overwrite=false"
        );
    }

    #[test]
    fn upx_test_fails_for_missing_file_if_installed() {
        if !has_upx() {
            eprintln!("skipping: upx not installed");
            return;
        }
        let res = test(Path::new("/no/such/file.upx"));
        assert!(res.is_err(), "upx -t should fail on a missing file");
    }
}
