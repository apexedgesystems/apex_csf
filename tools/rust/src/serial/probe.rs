//! SerialProbe — validate and lightly open/lock tty devices (Linux/Unix).
//!
//! Responsibilities:
//!  - path validation (/dev/*, exists, char device)
//!  - optional "held by other process" check via `lsof`
//!  - exclusive open (O_EXCL|O_NOCTTY|O_NONBLOCK) + best-effort flock
//!  - returns a ProbeReport summarizing what happened
//!
//! Notes:
//!  - All termios/baud/parity/stop bits are handled in serial::io::SerialPort.
//!  - Use ProbeOptions to speed up bulk audits (skip lsof).

use std::os::unix::fs::{FileTypeExt, MetadataExt, OpenOptionsExt}; // is_char_device, uid, gid, custom_flags
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;
use std::{fs, fs::OpenOptions};

use nix::fcntl::{Flock, FlockArg}; // modern flock API (no deprecation)
use nix::libc; // O_EXCL, O_NOCTTY, O_NONBLOCK, EACCES, EBUSY

#[derive(Clone, Copy, Debug)]
pub enum NumStopBits {
    One,
    Two,
}

#[derive(Clone, Copy, Debug)]
pub enum Parity {
    None,
    Odd,
    Even,
}

#[derive(Clone, Copy, Debug)]
pub enum BaudRate {
    B9600,
    B19200,
    B115200,
    B921600,
    /// Placeholder for arbitrary rates; requires termios2/BOTHER to set precisely (in SerialPort).
    Custom(u32),
}

#[derive(Clone, Debug)]
pub struct SerialParams {
    pub baud: BaudRate,
    pub parity: Parity,
    pub stop_bits: NumStopBits,
}

impl Default for SerialParams {
    fn default() -> Self {
        Self {
            baud: BaudRate::B115200,
            parity: Parity::None,
            stop_bits: NumStopBits::One,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct ProbeOptions {
    /// Run `lsof` to detect PIDs holding the device.
    pub use_lsof: bool,
    /// (Kept for API compatibility; the validator no longer configures termios.)
    pub apply_termios: bool,
}

impl Default for ProbeOptions {
    fn default() -> Self {
        // Validator is now purely about open/lock; termios belongs to SerialPort.
        Self {
            use_lsof: true,
            apply_termios: false,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProbeStatus {
    InvalidPath,
    DoesNotExist,
    NotADevice,
    PermissionDenied,
    ConnectedOpenable,
    ConnectedInUse,      // openable but contention (held/excl/lock)
    Error(i32),          // unexpected errno on open
    UnsupportedPlatform, // non-Unix
}

impl ProbeStatus {
    pub fn human_status(&self) -> &'static str {
        match self {
            ProbeStatus::InvalidPath => "invalid_path",
            ProbeStatus::DoesNotExist => "does_not_exist",
            ProbeStatus::NotADevice => "not_a_device",
            ProbeStatus::PermissionDenied => "permission_denied",
            ProbeStatus::ConnectedOpenable => "connected_openable",
            ProbeStatus::ConnectedInUse => "connected_in_use",
            ProbeStatus::Error(_) => "unreachable_device",
            ProbeStatus::UnsupportedPlatform => "unreachable_device",
        }
    }
}

#[derive(Debug, Clone)]
pub struct ProbeReport {
    pub path: String,
    pub friendly_name: Option<String>,
    pub status: ProbeStatus,
    pub is_char_device: bool,
    pub can_open_rw: bool,
    pub readable: bool,
    pub writable: bool,
    pub held_by_other: bool,
    pub excl_open_failed: bool,
    pub flock_failed: bool,
    pub uid: u32,
    pub gid: u32,
    pub open_latency_us: Option<u64>,
    pub notes: Vec<String>,
}

pub struct SerialProbe;

impl SerialProbe {
    /// Enumerate likely serial device paths (Linux).
    #[cfg(unix)]
    pub fn enumerate() -> Vec<String> {
        let mut out = Vec::new();
        // stable symlink names first (friendly)
        if let Ok(entries) = fs::read_dir("/dev/serial/by-id") {
            for e in entries.flatten() {
                if let Ok(t) = fs::read_link(e.path()) {
                    let p = Path::new("/dev").join(t);
                    if p.exists() {
                        out.push(p.display().to_string());
                    }
                }
            }
        }
        // common device families
        let roots = ["/dev/ttyS", "/dev/ttyUSB", "/dev/ttyACM", "/dev/ttyAMA"];
        for root in roots {
            for i in 0..256 {
                let p = format!("{root}{i}");
                if Path::new(&p).exists() {
                    out.push(p);
                }
            }
        }
        // de-dup while preserving order
        dedup_stable(out)
    }

    #[cfg(not(unix))]
    pub fn enumerate() -> Vec<String> {
        Vec::new()
    }

    /// Probe a single device (no termios here).
    pub fn probe(
        path: &str,
        opts: ProbeOptions,
        _params: Option<SerialParams>,
        verbose: bool,
    ) -> ProbeReport {
        #[cfg(unix)]
        {
            probe_unix(path, opts, verbose)
        }

        #[cfg(not(unix))]
        {
            ProbeReport {
                path: path.to_string(),
                friendly_name: None,
                status: ProbeStatus::UnsupportedPlatform,
                is_char_device: false,
                can_open_rw: false,
                readable: false,
                writable: false,
                held_by_other: false,
                excl_open_failed: false,
                flock_failed: false,
                uid: 0,
                gid: 0,
                open_latency_us: None,
                notes: vec!["Unsupported platform".into()],
            }
        }
    }
}

#[cfg(unix)]
fn probe_unix(path: &str, opts: ProbeOptions, verbose: bool) -> ProbeReport {
    let mut notes = Vec::new();

    // Validate path
    if path.is_empty() || !path.starts_with("/dev/") {
        return empty_report(path, ProbeStatus::InvalidPath, notes);
    }
    if !Path::new(path).exists() {
        return empty_report(path, ProbeStatus::DoesNotExist, notes);
    }

    // Metadata & perms
    let meta = match fs::metadata(path) {
        Ok(m) => m,
        Err(e) => {
            notes.push(format!("metadata failed: {e}"));
            return empty_report(
                path,
                ProbeStatus::Error(e.raw_os_error().unwrap_or(-1)),
                notes,
            );
        }
    };
    let is_char = meta.file_type().is_char_device();
    if !is_char {
        return empty_report(path, ProbeStatus::NotADevice, notes);
    }
    // capture uid/gid once and reuse below
    let (uid, gid) = (meta.uid(), meta.gid());

    // Friendly name via /dev/serial/by-id/*
    let friendly = reverse_by_id(Path::new(path)).map(|p| p.display().to_string());

    // lsof detection (optional)
    let held_by_other = if opts.use_lsof {
        held_by_lsof(path, verbose, &mut notes)
    } else {
        false
    };

    // Try exclusive open (O_EXCL | O_NOCTTY | O_NONBLOCK)
    let start = Instant::now();
    let open_res = OpenOptions::new()
        .read(true)
        .write(true)
        .custom_flags(libc::O_EXCL | libc::O_NOCTTY | libc::O_NONBLOCK)
        .open(path);

    let mut excl_failed = false;
    let mut flock_failed = false;
    let open_latency_us = Some(start.elapsed().as_micros() as u64);

    let file = match open_res {
        Ok(f) => f,
        Err(e) => {
            let status = match e.raw_os_error() {
                Some(libc::EACCES) => {
                    // Helpful hint for typical Linux setups:
                    notes.push("permission denied: consider adding your user to the 'dialout' group (then relog)".into());
                    ProbeStatus::PermissionDenied
                }
                Some(libc::EBUSY) => {
                    excl_failed = true;
                    ProbeStatus::ConnectedInUse
                }
                Some(code) => {
                    notes.push(format!("open errno={code}"));
                    ProbeStatus::Error(code)
                }
                None => {
                    notes.push(format!("open error: {e}"));
                    ProbeStatus::Error(-1)
                }
            };
            return ProbeReport {
                path: path.to_string(),
                friendly_name: friendly,
                status,
                is_char_device: true,
                can_open_rw: false,
                readable: false,
                writable: false,
                held_by_other,
                excl_open_failed: excl_failed,
                flock_failed,
                uid,
                gid,
                open_latency_us,
                notes,
            };
        }
    };

    // flock (non-blocking) using modern API on the File itself
    match Flock::lock(file, FlockArg::LockExclusiveNonblock) {
        Ok(lock_guard) => {
            // Locked successfully; immediately drop the guard to release the lock.
            drop(lock_guard);
        }
        Err((_file_back, errno)) => {
            // Keep behavior identical: record failure; file will drop/close at scope end.
            flock_failed = true;
            if verbose {
                notes.push(format!("flock nonblocking failed: {errno:?}"));
            }
        }
    }

    // We no longer flush/configure here. SerialPort::open() will handle that.
    // Consider the device 'usable' if open+flock succeeded.
    let readable = true;
    let writable = true;

    // Decide final status
    let status = if held_by_other || excl_failed || flock_failed {
        ProbeStatus::ConnectedInUse
    } else {
        ProbeStatus::ConnectedOpenable
    };

    ProbeReport {
        path: path.to_string(),
        friendly_name: friendly,
        status,
        is_char_device: true,
        can_open_rw: true,
        readable,
        writable,
        held_by_other,
        excl_open_failed: excl_failed,
        flock_failed,
        uid,
        gid,
        open_latency_us,
        notes,
    }
}

#[cfg(unix)]
fn empty_report(path: &str, status: ProbeStatus, notes: Vec<String>) -> ProbeReport {
    ProbeReport {
        path: path.to_string(),
        friendly_name: None,
        status,
        is_char_device: false,
        can_open_rw: false,
        readable: false,
        writable: false,
        held_by_other: false,
        excl_open_failed: false,
        flock_failed: false,
        uid: 0,
        gid: 0,
        open_latency_us: None,
        notes,
    }
}

#[cfg(unix)]
fn held_by_lsof(path: &str, verbose: bool, notes: &mut Vec<String>) -> bool {
    match Command::new("lsof").args(["-F", "p", path]).output() {
        Ok(out) if out.status.success() => {
            let me = std::process::id().to_string();
            let pids = String::from_utf8_lossy(&out.stdout)
                .lines()
                .filter_map(|l| l.strip_prefix('p'))
                .filter(|p| !p.is_empty() && *p != me)
                .count();
            pids > 0
        }
        _ => {
            if verbose {
                notes.push("lsof unavailable or returned no data".into());
            }
            false
        }
    }
}

/// Try to map /dev/ttyUSB0 → /dev/serial/by-id/<friendly> if such a symlink exists.
#[cfg(unix)]
fn reverse_by_id(dev: &Path) -> Option<PathBuf> {
    let by_id = Path::new("/dev/serial/by-id");
    let entries = fs::read_dir(by_id).ok()?;
    for e in entries.flatten() {
        if let Ok(t) = fs::read_link(e.path()) {
            let p = Path::new("/dev").join(t);
            if p == dev {
                return Some(e.path());
            }
        }
    }
    None
}

#[cfg(unix)]
fn dedup_stable(mut v: Vec<String>) -> Vec<String> {
    let mut seen = std::collections::HashSet::new();
    v.retain(|s| seen.insert(s.clone()));
    v
}

// ----- Inline unit tests (Unix-only) ------------------------------------------------------------
#[cfg(test)]
mod tests {
    use super::*;

    // These tests avoid opening real TTYs and do not rely on lsof (fast & deterministic).
    #[cfg(unix)]
    fn probe_fast(path: &str) -> ProbeStatus {
        SerialProbe::probe(
            path,
            ProbeOptions {
                use_lsof: false,
                apply_termios: false,
            },
            None,
            /*verbose*/ false,
        )
        .status
    }

    #[test]
    #[cfg(unix)]
    fn rejects_invalid_path_without_dev_prefix() {
        assert_eq!(probe_fast("ttyUSB0"), ProbeStatus::InvalidPath);
        assert_eq!(probe_fast(""), ProbeStatus::InvalidPath);
    }

    #[test]
    #[cfg(unix)]
    fn rejects_nonexistent_under_dev() {
        assert_eq!(
            probe_fast("/dev/this_device_better_not_exist_123456"),
            ProbeStatus::DoesNotExist
        );
    }

    #[test]
    #[cfg(unix)]
    fn classifies_existing_non_char_entry_as_not_a_device() {
        // Pick a well-known directory under /dev (directories are not char devices).
        let candidates = ["/dev/shm", "/dev/fd", "/dev/pts", "/dev/core"];
        let some_dir = candidates.iter().find(|p| std::path::Path::new(p).exists());
        let path = some_dir.expect("Need at least one known directory in /dev");
        assert_eq!(probe_fast(path), ProbeStatus::NotADevice);
    }

    #[test]
    fn human_status_strings_are_stable() {
        use ProbeStatus::*;
        assert_eq!(InvalidPath.human_status(), "invalid_path");
        assert_eq!(DoesNotExist.human_status(), "does_not_exist");
        assert_eq!(NotADevice.human_status(), "not_a_device");
        assert_eq!(PermissionDenied.human_status(), "permission_denied");
        assert_eq!(ConnectedOpenable.human_status(), "connected_openable");
        assert_eq!(ConnectedInUse.human_status(), "connected_in_use");
        assert_eq!(Error(5).human_status(), "unreachable_device");
        assert_eq!(UnsupportedPlatform.human_status(), "unreachable_device");
    }

    // Optional / environment-dependent (permissions vary). Run manually when helpful:
    // cargo test -p apex_rust_tools --lib serial::probe -- --ignored --nocapture
    #[test]
    #[ignore]
    #[cfg(unix)]
    fn may_report_permission_denied_on_protected_ttys() {
        // If none of these exist on the host, the test prints a skip message and returns.
        let candidates = ["/dev/ttyS0", "/dev/ttyS1", "/dev/ttyS2"];
        let Some(path) = candidates.iter().find(|p| std::path::Path::new(p).exists()) else {
            eprintln!("(skipped) none of {:?} exist on this host", candidates);
            return;
        };

        let status = probe_fast(path);
        eprintln!("Probed {path}: {:?}", status);
        // Don't flap: accept either PermissionDenied or any other value.
        let _ = status;
    }

    #[test]
    #[cfg(unix)]
    fn enumerate_does_not_panic_and_returns_unique_paths() {
        let list = SerialProbe::enumerate();
        // Should not contain duplicates given dedup_stable.
        let set: std::collections::HashSet<_> = list.iter().collect();
        assert_eq!(set.len(), list.len());
        // No further requirements; the host may or may not have serial devices attached.
    }

    #[test]
    #[cfg(unix)]
    fn empty_report_is_sane() {
        let rep = super::empty_report("/dev/fake", ProbeStatus::DoesNotExist, vec!["n".into()]);
        assert_eq!(rep.path, "/dev/fake");
        assert_eq!(rep.status, ProbeStatus::DoesNotExist);
        assert!(!rep.is_char_device);
        assert!(!rep.can_open_rw);
        assert_eq!(rep.uid, 0);
        assert_eq!(rep.gid, 0);
        assert!(rep.open_latency_us.is_none());
        assert_eq!(rep.notes.len(), 1);
    }

    #[test]
    fn dedup_stable_preserves_order() {
        let input = vec![
            "/dev/ttyACM0".to_string(),
            "/dev/ttyACM1".to_string(),
            "/dev/ttyACM0".to_string(), // dup
            "/dev/ttyS0".to_string(),
            "/dev/ttyS0".to_string(), // dup
        ];
        let out = super::dedup_stable(input.clone());
        // No duplicates
        let set: std::collections::HashSet<_> = out.iter().collect();
        assert_eq!(set.len(), out.len());
        // Order preserved for first occurrences
        assert_eq!(out, vec!["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyS0",]);
    }
}
