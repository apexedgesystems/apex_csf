// ===================== src/upx/bulk/tasks.rs =====================
// Small, focused task functions used by the bulk worker:
// - decompress_task
// - compress_task
// - verify_task
// - compare_task
//
// Notes:
// * We generate output names either from a user template or via sensible defaults.
// * We now respect `overwrite`: only uniquify when overwrite == false.

use std::io;
use std::path::{Path, PathBuf};

use crate::upx::compare::compare_upx_to_original;
use crate::upx::ops::{compress as upx_compress, decompress as upx_decompress, test};
use crate::upx::util::{make_output_name, unique_path, NameTemplate};

/// Join `out_dir` (if any) and `out_name`, and uniquify only when `overwrite` is false.
fn resolve_out_path(out_dir: Option<&Path>, out_name: PathBuf, overwrite: bool) -> PathBuf {
    let joined = if let Some(od) = out_dir {
        od.join(out_name)
    } else {
        out_name
    };
    if overwrite {
        joined
    } else {
        unique_path(joined)
    }
}

/// Default name for decompression:
/// `foo.so.upx` -> `foo.so.unpacked`
fn default_decompress_name(input: &Path) -> PathBuf {
    let base = input.file_stem().unwrap_or_default(); // "foo.so"
    let mut o = PathBuf::from(base);
    o.set_file_name(format!(
        "{}.unpacked",
        o.file_name().unwrap().to_string_lossy()
    ));
    o
}

/// Default name for compression:
/// `foo.so` -> `foo.so.upx`
fn default_compress_name(input: &Path) -> PathBuf {
    let mut o = input
        .file_name()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("out"));
    o.set_file_name(format!("{}.upx", o.file_name().unwrap().to_string_lossy()));
    o
}

/// Decompress a single `.upx` file into an output path derived from dir/template or defaults.
pub fn decompress_task(
    input: &Path,
    out_dir: Option<&Path>,
    tpl: Option<&str>,
    overwrite: bool,
) -> io::Result<PathBuf> {
    let out_name = if let Some(tpl_s) = tpl {
        make_output_name(input, &NameTemplate(tpl_s))
    } else {
        default_decompress_name(input)
    };
    let out_path = resolve_out_path(out_dir, out_name, overwrite);

    test(input)?;
    let out_written = upx_decompress(input, &out_path, overwrite)?;
    Ok(out_written)
}

/// Compress a single input (typically `.so`) into a `.upx`.
pub fn compress_task(
    input: &Path,
    out_dir: Option<&Path>,
    tpl: Option<&str>,
    overwrite: bool,
    flags: Option<&str>,
) -> io::Result<PathBuf> {
    let out_name = if let Some(tpl_s) = tpl {
        make_output_name(input, &NameTemplate(tpl_s))
    } else {
        default_compress_name(input)
    };
    let out_path = resolve_out_path(out_dir, out_name, overwrite);

    let out_written = upx_compress(input, &out_path, overwrite, flags)?;
    Ok(out_written)
}

/// Run `upx -t` on a file; return the same path if successful.
pub fn verify_task(input: &Path) -> io::Result<PathBuf> {
    test(input)?;
    Ok(input.to_path_buf())
}

/// Decompress `upx` to temp and compare to `orig`. Caller decides how to use the result.
pub fn compare_task(
    orig: PathBuf,
    upx: PathBuf,
    cleanup_temp: bool,
    print_sha: bool,
) -> io::Result<PathBuf> {
    let (_identical, _tmp, _shas) = compare_upx_to_original(&orig, &upx, cleanup_temp, print_sha)?;
    Ok(upx)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn default_name_mappings() {
        let a = Path::new("/tmp/libfoo.so.upx");
        let dn = super::default_decompress_name(a);
        assert_eq!(dn, PathBuf::from("libfoo.so.unpacked"));

        let b = Path::new("/opt/lib/libbar.so");
        let cn = super::default_compress_name(b);
        assert_eq!(cn, PathBuf::from("libbar.so.upx"));
    }

    #[test]
    fn resolve_out_path_respects_overwrite() {
        let td = TempDir::new().unwrap();
        let od = td.path();

        let base = PathBuf::from("x.out");
        let pre = od.join(&base);
        fs::write(&pre, b"exists").unwrap();

        // overwrite = true => keep same path even if it exists
        let p1 = super::resolve_out_path(Some(od), base.clone(), true);
        assert_eq!(p1, pre);

        // overwrite = false => uniquify
        let p2 = super::resolve_out_path(Some(od), base.clone(), false);
        assert_ne!(p2, pre);
        assert!(p2.starts_with(od));
        assert!(p2
            .file_name()
            .unwrap()
            .to_string_lossy()
            .starts_with("x.out"));
    }
}
