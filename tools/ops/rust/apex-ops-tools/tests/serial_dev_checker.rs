#![cfg(unix)] // relies on /dev paths

use assert_cmd::prelude::*;
use predicates::prelude::*;
use std::fs;
use std::io::Write;
use std::process::Command;
use tempfile::NamedTempFile;

/// Create a temp TOML file and keep the handle alive for the duration of the test.
/// IMPORTANT: returning `PathBuf` from `into_temp_path()` would delete the file when the
/// TempPath drops; we must keep `NamedTempFile` in scope instead.
fn make_ports_toml(contents: &str) -> NamedTempFile {
    let mut tf = NamedTempFile::new().expect("create temp toml");
    tf.write_all(contents.as_bytes()).expect("write toml");
    tf.flush().expect("flush toml");
    tf
}

#[test]
fn prints_summary_and_counts_for_various_statuses() {
    // Deterministic cases only:
    // - invalid_path (no /dev prefix)
    // - does_not_exist (clearly fake path)
    // - not_a_device (directory under /dev)
    let non_char = if std::path::Path::new("/dev/shm").exists() {
        "/dev/shm"
    } else if std::path::Path::new("/dev/pts").exists() {
        "/dev/pts"
    } else {
        "/dev" // fallback dir under /dev
    };

    let toml = format!(
        r#"
        [[ports]]
        label = "BadPrefix"
        path = "ttyLOL0"

        [[ports]]
        label = "Missing"
        path = "/dev/this_device_better_not_exist_123456"

        [[ports]]
        label = "Dir"
        path = "{non_char}"
        "#
    );
    let cfg = make_ports_toml(&toml);

    let mut cmd = Command::cargo_bin("serial_dev_checker").expect("binary built");
    cmd.args([
        "--config",
        cfg.path().to_str().unwrap(),
        "--fast",
        "--show-causes",
    ]);

    cmd.assert()
        .success()
        .stdout(predicate::str::contains("Serial Port Summary:"))
        .stdout(predicate::str::contains("BadPrefix").and(predicate::str::contains("invalid_path")))
        .stdout(predicate::str::contains("Missing").and(predicate::str::contains("does_not_exist")))
        .stdout(predicate::str::contains("Dir").and(predicate::str::contains("not_a_device")))
        .stdout(predicate::str::contains("Status Summary:"))
        .stdout(predicate::str::contains("invalid_path"))
        .stdout(predicate::str::contains("does_not_exist"))
        .stdout(predicate::str::contains("not_a_device"));
}

#[test]
fn writes_report_file_with_expected_lines() {
    let cfg = make_ports_toml(
        r#"
        [[ports]]
        label = "Missing"
        path = "/dev/this_device_better_not_exist_123456"
        "#,
    );

    // Keep the report file handle alive too (the binary will open the same path and write).
    let report_file = NamedTempFile::new().expect("create report file");
    let report_path = report_file.path().to_path_buf();

    let mut cmd = Command::cargo_bin("serial_dev_checker").expect("binary built");
    cmd.args([
        "--config",
        cfg.path().to_str().unwrap(),
        "--fast",
        "--report",
        report_path.to_str().unwrap(),
    ]);

    cmd.assert().success();

    let contents = fs::read_to_string(&report_path).expect("read report");
    assert!(
        contents.contains("Missing (/dev/this_device_better_not_exist_123456) : does_not_exist"),
        "unexpected report contents: {contents:?}"
    );
}

#[test]
fn handles_empty_ports_gracefully_with_exit_code() {
    // An empty ports array is valid and should produce the "No ports found..." error path.
    let cfg = make_ports_toml("ports = []");

    let mut cmd = Command::cargo_bin("serial_dev_checker").expect("binary built");
    cmd.args(["--config", cfg.path().to_str().unwrap(), "--fast"]);

    cmd.assert()
        .failure()
        .stderr(predicate::str::contains("No ports found in config."));
}
