// ===================== src/upx/compare.rs =====================
// Helpers for file equality and content hashing, plus a convenience
// routine to verify a UPX-packed file by decompressing to a temp path
// and comparing to an expected original.
//
// - sha256_file(path) -> hex digest
// - files_identical(a, b) -> streaming byte compare (size + content)
// - compare_upx_to_original(orig, upx, cleanup_temp, print_sha)
//     -> (identical, temp_out_path, optional (sha_orig, sha_unpacked))
//
// Notes:
// * `files_identical` previously compared chunks only when both reads
//   returned the same length; that can be incorrect because `read()`
//   may legally return fewer bytes than requested. We fix this by
//   implementing a small `read_fill` helper to align chunk sizes.

use std::fs::File;
use std::io::{self, BufReader, Read};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

use super::ops::{decompress, test};
use super::util::{remove_if_exists, temp_unpacked_path};

/// SHA-256 of a file, returned as lowercase hex.
pub fn sha256_file(path: &Path) -> io::Result<String> {
    let f = File::open(path)?;
    let mut r = BufReader::new(f);
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 64 * 1024];

    loop {
        let n = r.read(&mut buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(hex::encode(hasher.finalize()))
}

/// Fill `buf` as much as possible unless EOF occurs. Returns total bytes read.
///
/// This loops until either `buf` is full or a read returns 0 (EOF).
#[inline]
fn read_fill<R: Read>(r: &mut R, mut buf: &mut [u8]) -> io::Result<usize> {
    let mut filled = 0;
    while !buf.is_empty() {
        match r.read(buf) {
            Ok(0) => break, // EOF
            Ok(n) => {
                filled += n;
                let tmp = buf;
                buf = &mut tmp[n..];
            }
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(filled)
}

/// Streaming byte-for-byte compare with aligned chunking.
/// First compares sizes, then reads both files in fixed-size chunks
/// (aligned using `read_fill`) and compares the slices.
///
/// Returns `Ok(true)` iff contents are identical.
pub fn files_identical(a: &Path, b: &Path) -> io::Result<bool> {
    let ma = a.metadata()?;
    let mb = b.metadata()?;
    if ma.len() != mb.len() {
        return Ok(false);
    }

    let mut ra = BufReader::new(File::open(a)?);
    let mut rb = BufReader::new(File::open(b)?);
    let mut ba = [0u8; 64 * 1024];
    let mut bb = [0u8; 64 * 1024];

    loop {
        let na = read_fill(&mut ra, &mut ba)?;
        let nb = read_fill(&mut rb, &mut bb)?;
        if na == 0 && nb == 0 {
            break; // both EOF
        }
        if na != nb {
            // With equal file sizes this should not happen; treat as mismatch.
            return Ok(false);
        }
        if &ba[..na] != &bb[..nb] {
            return Ok(false);
        }
    }
    Ok(true)
}

/// Decompress `upx_path` to a temp file, compare with `original_path`.
///
/// Returns `(identical, temp_path, Option<(sha_orig, sha_unpacked)>)`.
/// If `cleanup_temp` is `true` and the files are identical, the temp file
/// is removed; otherwise it's left on disk for inspection.
pub fn compare_upx_to_original(
    original_path: &Path,
    upx_path: &Path,
    cleanup_temp: bool,
    print_sha: bool,
) -> io::Result<(bool, PathBuf, Option<(String, String)>)> {
    test(upx_path)?; // quick integrity check

    let tmp = temp_unpacked_path(upx_path);
    let out = decompress(upx_path, &tmp, /*overwrite*/ true)?;

    let identical = files_identical(original_path, &out)?;
    let shas = if print_sha {
        Some((sha256_file(original_path)?, sha256_file(&out)?))
    } else {
        None
    };

    if cleanup_temp && identical {
        let _ = remove_if_exists(&out);
    }
    Ok((identical, out, shas))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn sha256_matches_known() {
        let mut f = NamedTempFile::new().unwrap();
        write!(f, "hello").unwrap();
        let sum = sha256_file(f.path()).unwrap();
        // echo -n hello | sha256sum
        assert_eq!(
            sum,
            "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
        );
    }

    #[test]
    fn files_identical_true_for_same_contents() {
        let td = TempDir::new().unwrap();
        let a = td.path().join("a.bin");
        let b = td.path().join("b.bin");
        std::fs::write(&a, vec![1u8; 150_000]).unwrap(); // force multiple chunks
        std::fs::write(&b, vec![1u8; 150_000]).unwrap();
        assert!(files_identical(&a, &b).unwrap());
    }

    #[test]
    fn files_identical_false_for_different_sizes() {
        let td = TempDir::new().unwrap();
        let a = td.path().join("a.bin");
        let b = td.path().join("b.bin");
        std::fs::write(&a, vec![1u8; 10]).unwrap();
        std::fs::write(&b, vec![1u8; 11]).unwrap();
        assert!(!files_identical(&a, &b).unwrap());
    }

    #[test]
    fn files_identical_false_for_same_size_different_bytes() {
        let td = TempDir::new().unwrap();
        let a = td.path().join("a.bin");
        let b = td.path().join("b.bin");
        std::fs::write(&a, [0u8; 4096]).unwrap();
        let mut data = [0u8; 4096];
        data[1234] = 1;
        std::fs::write(&b, &data).unwrap();
        assert!(!files_identical(&a, &b).unwrap());
    }

    #[test]
    fn compare_upx_to_original_skips_when_upx_missing() {
        // This test intentionally only checks that calling compare would bail
        // early via `upx -t` if `upx` is installed; otherwise we skip.
        // We do not depend on real UPX artifacts in unit tests.
        let have_upx = super::super::ops::ensure_upx_available().is_ok();
        if !have_upx {
            eprintln!("skipping: upx not installed");
            return;
        }

        // With real upx present, comparing non-existent paths should error.
        let res = compare_upx_to_original(
            Path::new("/no/such/original.so"),
            Path::new("/no/such/file.so.upx"),
            true,
            false,
        );
        assert!(res.is_err());
    }
}
