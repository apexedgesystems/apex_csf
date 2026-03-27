// ===================== src/upx/bulk/scan.rs =====================
// Directory scanning utilities used by bulk operations.
//
// - visit_files: collect files with a given extension under a root (optionally recursive)
// - find_pairs:  map `*.so.upx` files to their presumed originals `*.so` by basename
//
// Notes:
// * If `root` is a file, it is returned as-is (caller validates semantics).
// * We do not follow symlinks during recursive walks.
// * I/O errors on individual entries are skipped (best-effort traversal).
// * Result ordering is not guaranteed; sort in callers if needed.

use std::fs;
use std::path::{Path, PathBuf};

use walkdir::WalkDir;

/// Return all files under `root` whose extension equals `pattern_ext`.
///
/// - If `root` is a file, it is returned as-is (caller is responsible for later checks).
/// - If `recursive` is true, descends into subdirectories (symlinks are not followed).
/// - If reading a directory entry fails, the entry is skipped.
/// - Result ordering is not guaranteed; callers should sort if they need determinism.
pub fn visit_files(root: &Path, recursive: bool, pattern_ext: &str) -> Vec<PathBuf> {
    if root.is_file() {
        return vec![root.to_path_buf()];
    }

    let mut out = Vec::new();

    if recursive {
        for e in WalkDir::new(root)
            .follow_links(false)
            .into_iter()
            .filter_map(Result::ok)
        {
            let p = e.path();
            if e.file_type().is_file() && has_ext(p, pattern_ext) {
                out.push(p.to_path_buf());
            }
        }
    } else if let Ok(rd) = fs::read_dir(root) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_file() && has_ext(&p, pattern_ext) {
                out.push(p);
            }
        }
    }

    out
}

/// Return pairs of (original .so, packed .so.upx) co-located by basename.
///
/// For each `*.upx` found, derive the original path by stripping the `.upx`
/// suffix, i.e. `dir/foo.so.upx -> dir/foo.so`. The original may or may not
/// exist; higher layers decide whether to treat that as an error.
pub fn find_pairs(dir: &Path, recursive: bool) -> Vec<(PathBuf, PathBuf)> {
    let mut pairs = Vec::new();
    for upx in visit_files(dir, recursive, "upx") {
        if let Some(stem) = upx.file_stem().and_then(|s| s.to_str()) {
            let orig = upx.parent().unwrap_or_else(|| Path::new(".")).join(stem);
            pairs.push((orig, upx));
        }
    }
    pairs
}

#[inline]
fn has_ext(p: &Path, want: &str) -> bool {
    p.extension().and_then(|s| s.to_str()) == Some(want)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn visit_files_non_recursive_filters_by_ext() {
        let td = TempDir::new().unwrap();
        let root = td.path();

        // files in root
        let f1 = root.join("a.so");
        let f2 = root.join("b.so.upx");
        let f3 = root.join("c.txt");
        // subdir content (should be ignored in non-recursive)
        let sub = root.join("sub");
        fs::create_dir(&sub).unwrap();
        let _ = fs::File::create(&f1).unwrap();
        let _ = fs::File::create(&f2).unwrap();
        let _ = fs::File::create(&f3).unwrap();
        let _ = fs::File::create(sub.join("d.so.upx")).unwrap();

        let mut upx = visit_files(root, false, "upx");
        upx.sort();
        assert_eq!(upx, vec![f2]);

        let mut so = visit_files(root, false, "so");
        so.sort();
        assert_eq!(so, vec![f1]);
    }

    #[test]
    fn visit_files_recursive_descends() {
        let td = TempDir::new().unwrap();
        let root = td.path();

        let _ = fs::File::create(root.join("r1.so.upx")).unwrap();
        fs::create_dir(root.join("sub")).unwrap();
        let _ = fs::File::create(root.join("sub/s1.so.upx")).unwrap();

        let mut upx = visit_files(root, true, "upx");
        upx.sort();
        assert_eq!(upx.len(), 2);
        assert!(upx.iter().any(|p| p.ends_with("r1.so.upx")));
        assert!(upx.iter().any(|p| p.ends_with("s1.so.upx")));
    }

    #[test]
    fn find_pairs_maps_upx_to_so_basename() {
        let td = TempDir::new().unwrap();
        let root = td.path();

        let upx = root.join("libfoo.so.upx");
        let _ = fs::File::create(&upx).unwrap();
        let expected_orig = root.join("libfoo.so");

        let pairs = find_pairs(root, false);
        assert_eq!(pairs.len(), 1);
        assert_eq!(pairs[0].0, expected_orig);
        assert_eq!(pairs[0].1, upx);
    }

    #[test]
    fn visit_files_accepts_file_root() {
        let td = TempDir::new().unwrap();
        let file = td.path().join("only.so.upx");
        let mut f = fs::File::create(&file).unwrap();
        writeln!(f, "x").unwrap();

        let v = visit_files(&file, true, "upx");
        assert_eq!(v, vec![file]);
    }
}
