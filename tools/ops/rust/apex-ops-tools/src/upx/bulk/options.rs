// ===================== src/upx/bulk/options.rs =====================
// Bulk configuration types and simple result counters.
//
// Provides:
// - ArchiveMode: what to do with the *source* file after success
// - BulkOptions: knobs for traversal, naming, parallelism, and safety
// - BulkResult:  aggregate counters (ok/skipped/failed)
//
// Notes:
// * BulkOptions owns its data so worker threads can clone cheaply.
// * `normalized()` ensures consistent/safe values (e.g., jobs >= 1).
// * `validate()` enforces cross-field invariants (e.g., ArchiveTarGz needs a dir).

use std::io;
use std::path::PathBuf;

/// What to do with the *source* artifact after a successful operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArchiveMode {
    /// Leave the source file untouched.
    Keep,
    /// Delete the source file.
    Delete,
    /// Create a `.tar.gz` under `archive_dir` and remove the source.
    ArchiveTarGz,
}

impl ArchiveMode {
    /// Returns true if this mode requires an `archive_dir`.
    #[inline]
    pub fn needs_dir(self) -> bool {
        matches!(self, ArchiveMode::ArchiveTarGz)
    }
}

/// Options that control bulk traversal and per-file behavior.
/// These are intentionally owned types so the worker threads can clone safely.
#[derive(Clone, Debug)]
pub struct BulkOptions {
    /// Recurse into subdirectories when the target is a directory.
    pub recursive: bool,

    /// Output directory; when `None`, outputs are placed alongside inputs.
    pub out_dir: Option<PathBuf>,

    /// Optional output filename template (placeholders: `{base}`, `{ext}`, `{ts}`, `{pid}`).
    /// If `None`, a sensible default is chosen per operation.
    pub name_template: Option<String>,

    /// Overwrite existing outputs (if false, a unique suffix is used by helpers).
    pub overwrite: bool,

    /// Post-action handling for the *source* artifact.
    pub archive_mode: ArchiveMode,

    /// Destination directory used when `archive_mode == ArchiveMode::ArchiveTarGz`.
    pub archive_dir: Option<PathBuf>,

    /// Enable parallel workers (combined with `jobs`).
    pub parallel: bool,

    /// Number of worker threads when `parallel == true` (minimum 1).
    pub jobs: usize,

    /// Optional CPU list pinning for workers, e.g. `"0,1,3"`.
    pub cores: Option<String>,

    /// Stop processing at the first error (fast-fail).
    pub stop_on_error: bool,

    /// Flags passed to `upx` in compress mode (e.g. `"--best --lzma"`).
    pub flags: Option<String>,
}

impl Default for BulkOptions {
    fn default() -> Self {
        Self {
            recursive: false,
            out_dir: None,
            name_template: None,
            overwrite: false,
            archive_mode: ArchiveMode::Keep,
            archive_dir: None,
            parallel: false,
            jobs: 1,
            cores: None,
            stop_on_error: false,
            flags: None,
        }
    }
}

impl BulkOptions {
    /// Return a normalized copy with consistent/safe values:
    /// - jobs is clamped to at least 1
    /// - parallel is forced `true` iff jobs > 1
    #[inline]
    pub fn normalized(mut self) -> Self {
        if self.jobs == 0 {
            self.jobs = 1;
        }
        if self.jobs > 1 {
            self.parallel = true;
        }
        self
    }

    /// Validate cross-field invariants. Call once near the boundary (e.g. CLI).
    ///
    /// Currently checks:
    /// - `ArchiveTarGz` requires `archive_dir`
    pub fn validate(&self) -> io::Result<()> {
        if self.archive_mode.needs_dir() && self.archive_dir.is_none() {
            return Err(io::Error::other(
                "archive_mode=ArchiveTarGz requires archive_dir",
            ));
        }
        Ok(())
    }

    /// Effective number of worker threads after normalization.
    #[inline]
    pub fn effective_jobs(&self) -> usize {
        if !self.parallel {
            1
        } else {
            self.jobs.max(1)
        }
    }

    /// Should the worker run parallel at all?
    #[inline]
    pub fn should_parallel(&self) -> bool {
        self.effective_jobs() > 1
    }
}

/// Simple counters for bulk results.
#[must_use]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct BulkResult {
    pub ok: usize,
    pub skipped: usize,
    pub failed: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_is_sane() {
        let d = BulkOptions::default();
        assert!(!d.recursive);
        assert!(!d.parallel);
        assert_eq!(d.jobs, 1);
        assert_eq!(d.archive_mode, ArchiveMode::Keep);
        assert!(d.archive_dir.is_none());
        assert!(d.validate().is_ok());
    }

    #[test]
    fn normalization_forces_parallel_with_jobs_gt1() {
        let opts = BulkOptions {
            jobs: 4,
            ..Default::default()
        }
        .normalized();
        assert!(opts.parallel, "parallel should be true when jobs > 1");
        assert_eq!(opts.effective_jobs(), 4);
    }

    #[test]
    fn normalization_clamps_jobs_min_one() {
        let opts = BulkOptions {
            jobs: 0,
            parallel: true,
            ..Default::default()
        }
        .normalized();
        assert_eq!(opts.effective_jobs(), 1);
    }

    #[test]
    fn archive_tar_gz_requires_dir() {
        let bad = BulkOptions {
            archive_mode: ArchiveMode::ArchiveTarGz,
            archive_dir: None,
            ..Default::default()
        };
        assert!(bad.validate().is_err());

        let ok = BulkOptions {
            archive_mode: ArchiveMode::ArchiveTarGz,
            archive_dir: Some(PathBuf::from("/tmp")),
            ..Default::default()
        };
        assert!(ok.validate().is_ok());
    }

    #[test]
    fn should_parallel_matches_effective_jobs() {
        let a = BulkOptions {
            parallel: false,
            jobs: 8,
            ..Default::default()
        }
        .normalized();
        assert_eq!(a.effective_jobs(), 8);
        assert!(a.should_parallel());

        let b = BulkOptions {
            parallel: false,
            jobs: 1,
            ..Default::default()
        }
        .normalized();
        assert_eq!(b.effective_jobs(), 1);
        assert!(!b.should_parallel());
    }
}
