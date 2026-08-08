//! Tests for the apex_data_gen CLI tool.
//!
//! Covers the spec-derived dictionary surface: struct entries from
//! ordered field specs plus the command/telemetry sections that ground
//! tooling reads to learn a component's C2 surface.

use assert_cmd::prelude::*;
use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use tempfile::tempdir;

fn write_manifest(dir: &tempfile::TempDir, body: &str) -> PathBuf {
    let path = dir.path().join("apex_data.toml");
    let mut f = fs::File::create(&path).expect("create manifest");
    f.write_all(body.as_bytes()).expect("write manifest");
    path
}

const SPEC_MANIFEST: &str = r#"
component = "SpecSensor"
namespace = "appsim::spec"
component_id = 212

[structs]
SpecSensorOutput = { category = "OUTPUT" }
SetModeRequest = { category = "COMMAND", opcode = "0x0200" }

[[fields.SpecSensorOutput]]
name = "value"
type = "float"
size = 4

[[fields.SetModeRequest]]
name = "mode"
type = "uint"
size = 1

[[commands]]
name = "SetMode"
opcode = "0x0200"
request = "SetModeRequest"
doc = "Select the operating mode."

[[commands]]
name = "Reset"
opcode = "0x0203"

[[telemetry]]
name = "Measurement"
opcode = "0x0300"
struct = "SpecSensorOutput"
"#;

#[test]
fn dictionary_carries_spec_commands_and_telemetry() {
    let dir = tempdir().unwrap();
    let manifest = write_manifest(&dir, SPEC_MANIFEST);

    let output = Command::cargo_bin("apex_data_gen")
        .unwrap()
        .args(["--manifest", manifest.to_str().unwrap(), "--output", "-"])
        .output()
        .expect("run apex_data_gen");
    assert!(output.status.success());

    let dict: serde_json::Value = serde_json::from_slice(&output.stdout).expect("valid JSON");
    assert_eq!(dict["component"], "SpecSensor");
    assert_eq!(dict["component_id"], 212);
    assert!(dict["structs"]["SetModeRequest"].is_object());

    let commands = dict["commands"].as_array().expect("commands array");
    assert_eq!(commands.len(), 2);
    assert_eq!(commands[0]["name"], "SetMode");
    assert_eq!(commands[0]["opcode"], "0x0200");
    assert_eq!(commands[0]["request"], "SetModeRequest");
    assert_eq!(commands[1]["name"], "Reset");
    assert!(commands[1].get("request").is_none());

    let telemetry = dict["telemetry"].as_array().expect("telemetry array");
    assert_eq!(telemetry.len(), 1);
    assert_eq!(telemetry[0]["struct"], "SpecSensorOutput");
}

#[test]
fn command_referencing_unregistered_struct_fails() {
    let dir = tempdir().unwrap();
    let manifest = write_manifest(
        &dir,
        r#"
component = "X"

[structs]
A = { category = "OUTPUT" }

[[fields.A]]
name = "v"
type = "uint"
size = 4

[[commands]]
name = "Bad"
opcode = "0x0300"
request = "MissingStruct"
"#,
    );

    let output = Command::cargo_bin("apex_data_gen")
        .unwrap()
        .args(["--manifest", manifest.to_str().unwrap(), "--output", "-"])
        .output()
        .expect("run apex_data_gen");
    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("MissingStruct"));
}
