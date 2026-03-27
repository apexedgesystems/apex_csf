use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn bin() -> PathBuf {
    PathBuf::from(env!("CARGO_BIN_EXE_upx_tool"))
}

fn run_env(args: &[&str]) -> (i32, String, String) {
    let out = Command::new(bin())
        .env("UPX_TOOL_NO_CHECK", "1")
        .args(args)
        .output()
        .expect("spawn upx_tool");
    let code = out.status.code().unwrap_or(255);
    (
        code,
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

#[test]
fn help_works_without_upx() {
    let (code, out, _err) = run_env(&["--help"]);
    assert_eq!(code, 0, "help should exit 0");
    assert!(out.contains("UPX helper for single/bulk operations"));
}

#[test]
fn test_on_empty_dir_is_ok() {
    let td = tempfile::TempDir::new().unwrap();
    let (code, out, _err) = run_env(&["test", td.path().to_str().unwrap()]);
    assert_eq!(code, 0, "empty dir should produce ok summary");
    assert!(out.contains("Summary:"), "should print summary");
}

#[test]
fn verify_on_empty_dir_is_ok() {
    let td = tempfile::TempDir::new().unwrap();
    let (code, out, _err) = run_env(&["verify", td.path().to_str().unwrap()]);
    assert_eq!(code, 0);
    assert!(out.contains("Summary:"));
}

#[test]
fn compress_on_empty_dir_is_ok() {
    let td = tempfile::TempDir::new().unwrap();
    let (code, out, _err) = run_env(&["compress", "--jobs", "2", td.path().to_str().unwrap()]);
    assert_eq!(code, 0);
    assert!(out.contains("Summary:"));
}

#[test]
fn decompress_on_empty_dir_is_ok() {
    let td = tempfile::TempDir::new().unwrap();
    let (code, out, _err) = run_env(&["decompress", td.path().to_str().unwrap()]);
    assert_eq!(code, 0);
    assert!(out.contains("Summary:"));
}

#[test]
fn compare_on_empty_dir_is_ok() {
    let td = tempfile::TempDir::new().unwrap();
    fs::create_dir_all(td.path()).unwrap();
    let (code, out, _err) = run_env(&["compare", td.path().to_str().unwrap()]);
    assert_eq!(code, 0);
    assert!(out.contains("Summary:"));
}
