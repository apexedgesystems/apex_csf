use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

fn bin() -> PathBuf {
    PathBuf::from(env!("CARGO_BIN_EXE_serial_dev_tester"))
}

fn tmp_path(prefix: &str) -> PathBuf {
    let when = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    std::env::temp_dir().join(format!("{prefix}.{when}.tmp"))
}

fn write_tmp(path: &PathBuf, contents: &str) {
    let mut f = fs::File::create(path).expect("create temp file");
    f.write_all(contents.as_bytes()).expect("write temp file");
}

/// Run the tester and return (code, stdout, stderr)
fn run(args: &[&str]) -> (i32, String, String) {
    let out = Command::new(bin())
        .args(args)
        .output()
        .expect("spawn tester");
    let code = out.status.code().unwrap_or(255);
    (
        code,
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

#[test]
fn exits_with_error_for_missing_config() {
    let missing = tmp_path("no_such_config");
    let (code, _out, err) = run(&["--config", missing.to_str().unwrap()]);
    assert_eq!(code, 1, "expected exit 1 for missing config");
    assert!(
        err.contains("Config file not found"),
        "stderr should mention missing config, got: {err:?}"
    );
}

#[test]
fn exits_with_error_for_empty_pairs() {
    let cfg = tmp_path("empty_pairs");
    write_tmp(&cfg, r#"pairs = []"#);

    let (code, _out, err) = run(&["--config", cfg.to_str().unwrap()]);
    assert_eq!(code, 1, "expected exit 1 when no pairs found");
    assert!(
        err.contains("No pairs found in config."),
        "stderr should contain 'No pairs found in config.', got: {err:?}"
    );
}

#[test]
fn prints_errors_for_invalid_pairs_and_counts_them() {
    let cfg = tmp_path("invalid_pairs");
    // Three entries:
    // 1) loopback missing 'port' -> error
    // 2) interconnect with only one port -> error
    // 3) loopback with nonexistent port -> probe/open error
    write_tmp(
        &cfg,
        r#"
        [[pairs]]
        label = "LB missing"
        type = "loopback"

        [[pairs]]
        label = "X-conn bad list"
        type = "interconnect"
        ports = ["/dev/ttyDefinitelyNotReal"]  # wrong count (only 1)

        [[pairs]]
        label = "LB nonexistent"
        type = "loopback"
        port = "/dev/ttyDefinitelyNotReal"
    "#,
    );

    let (code, out, _err) = run(&["--config", cfg.to_str().unwrap()]);
    // Program should complete with code 0 and print a summary with error rows.
    assert_eq!(code, 0, "should complete even with per-pair errors");

    assert!(
        out.contains("Loopback/Interconnect Test Results:"),
        "stdout should contain summary header"
    );

    // For each label, ensure its line exists and contains the word `error`
    for lbl in ["LB missing", "X-conn bad list", "LB nonexistent"] {
        let line = out
            .lines()
            .find(|l| l.contains(lbl))
            .unwrap_or_else(|| panic!("stdout should contain label {lbl:?}, got:\n{out}"));
        assert!(
            line.contains("error"),
            "line for {lbl:?} should show error status, got:\n{line}"
        );
    }
}

#[test]
fn writes_report_file_with_expected_lines() {
    let cfg = tmp_path("report_cfg");
    let rpt = tmp_path("report_out");
    write_tmp(
        &cfg,
        r#"
        [[pairs]]
        label = "A"
        type = "loopback"
        port = "/dev/ttyDefinitelyNotReal"

        [[pairs]]
        label = "B"
        type = "interconnect"
        ports = ["/dev/ttyFake0"]  # wrong count -> error
    "#,
    );

    let (code, out, _err) = run(&[
        "--config",
        cfg.to_str().unwrap(),
        "--report",
        rpt.to_str().unwrap(),
    ]);
    assert_eq!(code, 0, "should finish and write report");
    assert!(
        out.contains("Writing report to:"),
        "stdout should mention report path; got:\n{out}"
    );

    let report = fs::read_to_string(&rpt).expect("read report");
    // Expect two lines, one per pair, with error status
    let lines: Vec<_> = report.lines().collect();
    assert_eq!(lines.len(), 2, "report should have one line per pair");
    assert!(
        report.contains("A (loopback) : error"),
        "report line for A should be error; got:\n{report}"
    );
    assert!(
        report.contains("B (interconnect) : error"),
        "report line for B should be error; got:\n{report}"
    );
}
