//! Tests for proto-authored manifests through the cdef_gen CLI: the
//! resolution rules that only exist at the file level (one definition
//! source per component, package/namespace agreement, message/struct
//! registration) and the generated-surface parity with TOML authoring.

use assert_cmd::prelude::*;
use std::fs;
use std::path::Path;
use std::process::Command;
use tempfile::tempdir;

const PROTO: &str = r#"syntax = "proto3";

package appsim.spec;

import "apex/options.proto";

message MoveRequest {
  // Commanded target.
  float position = 1;
}
"#;

const MANIFEST: &str = r#"
component = "SpecActuator"
namespace = "appsim::spec"
proto_spec = "spec.proto"

[structs]
MoveRequest = { category = "COMMAND", opcode = "0x0210" }
"#;

fn write(dir: &Path, name: &str, content: &str) {
    fs::write(dir.join(name), content).expect("write fixture");
}

fn run_cdef_gen(dir: &Path) -> std::process::Output {
    Command::cargo_bin("cdef_gen")
        .unwrap()
        .args(["--manifest", dir.join("apex_data.toml").to_str().unwrap()])
        .output()
        .expect("run cdef_gen")
}

#[test]
fn proto_authored_manifest_generates_the_full_surface() {
    let dir = tempdir().unwrap();
    write(dir.path(), "apex_data.toml", MANIFEST);
    write(dir.path(), "spec.proto", PROTO);

    let output = run_cdef_gen(dir.path());
    assert!(output.status.success());

    let auto = dir.path().join(".auto");
    let header = fs::read_to_string(auto.join("MoveRequest_auto.hpp")).unwrap();
    assert!(header.contains("struct MoveRequest {"));
    assert!(header.contains("float position{};"));
    let emitted = fs::read_to_string(auto.join("SpecActuator.proto")).unwrap();
    assert!(emitted.contains("message MoveRequest {"));
    assert!(emitted.contains("float position = 1;"));
}

#[test]
fn dual_definition_source_is_refused() {
    let dir = tempdir().unwrap();
    let manifest = format!(
        "{MANIFEST}\n[[fields.MoveRequest]]\nname = \"position\"\ntype = \"float\"\nsize = 4\n"
    );
    write(dir.path(), "apex_data.toml", &manifest);
    write(dir.path(), "spec.proto", PROTO);

    let output = run_cdef_gen(dir.path());
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("one definition source per component"),
        "{stderr}"
    );
}

#[test]
fn package_must_match_namespace() {
    let dir = tempdir().unwrap();
    write(dir.path(), "apex_data.toml", MANIFEST);
    write(
        dir.path(),
        "spec.proto",
        &PROTO.replace("package appsim.spec;", "package other.place;"),
    );

    let output = run_cdef_gen(dir.path());
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("does not match namespace"), "{stderr}");
}

#[test]
fn unregistered_message_is_a_typo_not_an_extension() {
    let dir = tempdir().unwrap();
    write(dir.path(), "apex_data.toml", MANIFEST);
    write(
        dir.path(),
        "spec.proto",
        &format!("{PROTO}\nmessage Rogue {{\n  float x = 1;\n}}\n"),
    );

    let output = run_cdef_gen(dir.path());
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("'Rogue' is not a registered struct"),
        "{stderr}"
    );
}

#[test]
fn missing_proto_file_names_the_path() {
    let dir = tempdir().unwrap();
    write(dir.path(), "apex_data.toml", MANIFEST);

    let output = run_cdef_gen(dir.path());
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("spec.proto"), "{stderr}");
}
