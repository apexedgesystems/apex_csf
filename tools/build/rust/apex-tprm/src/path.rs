//! Path helpers for deriving output filenames from header paths.

use std::path::{Path, PathBuf};

use super::{Error, Format};

/// Replace a header's `.hpp`/`.h` extension with `.json` or `.toml`.
///
/// Returns `Error::InvalidHeaderSuffix` if the input path does not end in
/// `.hpp` or `.h`.
pub(crate) fn infer_output_path_impl(header_path: &Path, format: Format) -> Result<PathBuf, Error> {
    let ext = header_path
        .extension()
        .and_then(|e| e.to_str())
        .ok_or_else(|| Error::InvalidHeaderSuffix(header_path.to_path_buf()))?;

    if !matches!(ext, "hpp" | "h") {
        return Err(Error::InvalidHeaderSuffix(header_path.to_path_buf()));
    }

    let new_ext = match format {
        Format::Json => "json",
        Format::Toml => "toml",
    };

    Ok(header_path.with_extension(new_ext))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn infers_from_hpp_to_json() {
        let p = Path::new("include/foo/bar.hpp");
        let out = infer_output_path_impl(p, Format::Json).unwrap();
        assert_eq!(out, Path::new("include/foo/bar.json"));
    }

    #[test]
    fn infers_from_h_to_toml() {
        let p = Path::new("hdr/baz.h");
        let out = infer_output_path_impl(p, Format::Toml).unwrap();
        assert_eq!(out, Path::new("hdr/baz.toml"));
    }

    #[test]
    fn rejects_bad_suffix() {
        let p = Path::new("src/not_a_header.hhxx");
        let err = infer_output_path_impl(p, Format::Json).unwrap_err();
        match err {
            Error::InvalidHeaderSuffix(x) => assert_eq!(x, p),
            _ => panic!("expected InvalidHeaderSuffix"),
        }
    }

    #[test]
    fn rejects_when_no_extension() {
        let p = Path::new("include/header_without_ext");
        let err = infer_output_path_impl(p, Format::Toml).unwrap_err();
        match err {
            Error::InvalidHeaderSuffix(x) => assert_eq!(x, p),
            _ => panic!("expected InvalidHeaderSuffix"),
        }
    }

    #[test]
    fn rejects_double_extension_like_hpp_gz() {
        let p = Path::new("include/foo/bar.hpp.gz");
        let err = infer_output_path_impl(p, Format::Json).unwrap_err();
        match err {
            Error::InvalidHeaderSuffix(x) => assert_eq!(x, p),
            _ => panic!("expected InvalidHeaderSuffix"),
        }
    }

    #[test]
    fn uppercase_extensions_are_rejected_currently() {
        let p = Path::new("include/foo/BAR.HPP");
        let err = infer_output_path_impl(p, Format::Toml).unwrap_err();
        match err {
            Error::InvalidHeaderSuffix(x) => assert_eq!(x, p),
            _ => panic!("expected InvalidHeaderSuffix"),
        }
    }
}
