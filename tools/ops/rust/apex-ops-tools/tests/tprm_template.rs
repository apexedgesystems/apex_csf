//! Tests for tprm_template CLI tool.
//!
//! Tests both header mode and JSON mode.

use assert_cmd::prelude::*;
use predicates::prelude::*;
use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use tempfile::tempdir;

fn write_temp_header(dir: &tempfile::TempDir, name: &str, body: &str) -> PathBuf {
    let path = dir.path().join(name);
    let mut f = fs::File::create(&path).expect("create header");
    f.write_all(body.as_bytes()).expect("write header");
    path
}

fn write_temp_json(dir: &tempfile::TempDir, name: &str, body: &str) -> PathBuf {
    let path = dir.path().join(name);
    let mut f = fs::File::create(&path).expect("create json");
    f.write_all(body.as_bytes()).expect("write json");
    path
}

/* ----------------------------- Header Mode Tests --------------------------- */
/* These tests verify header mode functionality */

#[test]
fn header_mode_json_generation_basic() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(
        &dir,
        "sample_header_1.hpp",
        r#"
        #define N 32

        struct PrimitiveTypes {
            int id;
            float value;
            char name[N];
        };
        "#,
    );
    let out = dir.path().join("sample_header_1.json");

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--format",
        "json",
        "--struct",
        "PrimitiveTypes",
        "--output",
        out.to_str().unwrap(),
    ]);

    cmd.assert()
        .success()
        .stdout(predicate::str::contains("Metadata written to"));

    let content = fs::read_to_string(&out).expect("read output");
    assert!(content.contains("\"PrimitiveTypes\""));
    assert!(content.contains("\"name\""));
}

#[test]
fn header_mode_toml_generation_basic() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(
        &dir,
        "sample_header_2.hpp",
        r#"
        #define AN_EXAMPLE_LEN 10
        struct FixedArrayStruct {
            char description[AN_EXAMPLE_LEN];
            std::array<int32_t, 5> fixedArray;
        };
        "#,
    );
    let out = dir.path().join("sample_header_2.toml");

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--format",
        "toml",
        "--struct",
        "FixedArrayStruct",
        "--output",
        out.to_str().unwrap(),
    ]);

    cmd.assert().success();

    let content = fs::read_to_string(&out).expect("read output");
    assert!(content.contains("FixedArrayStruct"));
    assert!(content.contains("description"));
    assert!(content.contains("fixedArray"));
}

#[test]
fn header_mode_infers_output_when_not_provided() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(
        &dir,
        "infer_me.hpp",
        r#"
        struct S { int x; };
        "#,
    );

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--format",
        "toml",
        "--struct",
        "S",
    ]);

    cmd.assert().success();

    let inferred = header.with_extension("toml");
    assert!(inferred.exists(), "inferred output should exist");
    let content = fs::read_to_string(inferred).expect("read inferred");
    assert!(content.contains("S"));
}

#[test]
fn header_mode_invalid_header_path_errors() {
    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        "tests/does_not_exist.hpp",
        "--format",
        "json",
        "--struct",
        "Whatever",
        "--output",
        "target/tmp/should_not_exist.json",
    ]);
    cmd.assert()
        .failure()
        .stderr(predicate::str::contains("error"));
}

#[test]
fn header_mode_missing_format_argument_exits() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(&dir, "missing_format.hpp", r#"struct T { int y; };"#);

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args(["--header", header.to_str().unwrap(), "--struct", "T"]);
    cmd.assert().failure();
}

#[test]
fn header_mode_invalid_format_is_rejected() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(&dir, "badfmt.hpp", r#"struct T { int x; };"#);

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--format",
        "yaml",
        "--struct",
        "T",
    ]);
    cmd.assert()
        .failure()
        .stderr(predicate::str::contains("must be 'json' or 'toml'"));
}

#[test]
fn header_mode_strict_mode_works() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(
        &dir,
        "strict.hpp",
        r#"
        struct P {
            int* ptr;
            int ok;
        };
        "#,
    );
    let out = dir.path().join("strict.json");

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--format",
        "json",
        "--struct",
        "P",
        "--output",
        out.to_str().unwrap(),
        "--strict",
    ]);

    cmd.assert().success();
    let content = fs::read_to_string(out).expect("read output");
    assert!(content.contains("\"P\""));
}

/* ----------------------------- JSON Mode Tests ----------------------------- */

#[test]
fn json_mode_generates_toml() {
    let dir = tempdir().unwrap();
    let json = write_temp_json(
        &dir,
        "Component.json",
        r#"{
            "component": "TestComponent",
            "structs": {
                "TestTunableParams": {
                    "category": "TUNABLE_PARAM",
                    "size": 16,
                    "fields": [
                        { "name": "alpha", "type": "float", "offset": 0, "size": 8, "value": 1.0 },
                        { "name": "beta", "type": "float", "offset": 8, "size": 8, "value": 2.5 }
                    ]
                }
            }
        }"#,
    );
    let out = dir.path().join("test.toml");

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--json",
        json.to_str().unwrap(),
        "--struct",
        "TestTunableParams",
        "--output",
        out.to_str().unwrap(),
    ]);

    cmd.assert()
        .success()
        .stdout(predicate::str::contains("Template written to"));

    let content = fs::read_to_string(&out).expect("read output");
    assert!(
        content.contains("TestTunableParams"),
        "should contain struct name"
    );
    assert!(content.contains("alpha"), "should contain field alpha");
    assert!(content.contains("beta"), "should contain field beta");
}

#[test]
fn json_mode_outputs_to_stdout_by_default() {
    let dir = tempdir().unwrap();
    let json = write_temp_json(
        &dir,
        "Stdout.json",
        r#"{
            "component": "StdoutComponent",
            "structs": {
                "StdoutParams": {
                    "category": "TUNABLE_PARAM",
                    "size": 8,
                    "fields": [
                        { "name": "val", "type": "float", "offset": 0, "size": 8, "value": 0.0 }
                    ]
                }
            }
        }"#,
    );

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args(["--json", json.to_str().unwrap(), "--struct", "StdoutParams"]);

    cmd.assert()
        .success()
        .stdout(predicate::str::contains("StdoutParams"))
        .stdout(predicate::str::contains("val"));
}

#[test]
fn json_mode_struct_not_found_errors() {
    let dir = tempdir().unwrap();
    let json = write_temp_json(
        &dir,
        "NoMatch.json",
        r#"{
            "component": "NoMatch",
            "structs": {
                "OtherStruct": {
                    "category": "STATE",
                    "size": 8,
                    "fields": []
                }
            }
        }"#,
    );

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args(["--json", json.to_str().unwrap(), "--struct", "NonExistent"]);

    cmd.assert()
        .failure()
        .stderr(predicate::str::contains("not found"));
}

#[test]
fn json_mode_invalid_path_errors() {
    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args(["--json", "does_not_exist.json", "--struct", "Whatever"]);
    cmd.assert()
        .failure()
        .stderr(predicate::str::contains("error"));
}

/* ----------------------------- Input Conflict Tests ------------------------ */

#[test]
fn requires_either_header_or_json() {
    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args(["--format", "toml", "--struct", "S"]);
    cmd.assert().failure();
}

#[test]
fn header_and_json_conflict() {
    let dir = tempdir().unwrap();
    let header = write_temp_header(&dir, "conflict.hpp", r#"struct T { int x; };"#);
    let json = write_temp_json(&dir, "conflict.json", r#"{"component":"C","structs":{}}"#);

    let mut cmd = Command::cargo_bin("tprm_template").unwrap();
    cmd.args([
        "--header",
        header.to_str().unwrap(),
        "--json",
        json.to_str().unwrap(),
        "--format",
        "toml",
        "--struct",
        "T",
    ]);

    cmd.assert().failure();
}
