//! Golden-vector conformance: the committed vectors under compat/tprm/
//! are the byte-level contract between this implementation, the C++
//! runtime (GoldenVectors_uTest.cpp), and any other consumer (zenith
//! copies the set). These tests pin the generator to the
//! committed bytes; the C++ test pins the reader to the same bytes.
//!
//! Regenerating after an intentional format change:
//!   cargo test --test golden_vectors regenerate -- --ignored
//! then review the binary diffs like any other contract change.

use std::fs;
use std::path::{Path, PathBuf};

use apex_rust_tools::tunable_params::binary::{config_to_binary, config_to_binary_v3};
use apex_rust_tools::tunable_params::pack::{pack, PackEntry};

fn vectors_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../compat/tprm")
}

/// The payload set: (TOML source stem, contract fullUid) -> committed
/// v3-stamped payload file. The uids are part of the contract: each
/// vector's prelude declares its target.
const PAYLOADS: &[(&str, u32)] = &[
    ("scalar_types", 0x000000),
    ("strings_arrays", 0x00D001),
    ("nested_enum", 0x00CA00),
    ("scheduler_shape", 0x000100),
];

/// The archive's entries. Component entries pack the v3-stamped
/// payloads; the RTS reserved-range slot packs a raw serialization
/// (sequence binaries carry no payload prelude).
const ARCHIVE_ENTRIES: &[(u32, &str)] = &[
    (0x000000, "scalar_types"),
    (0x00D001, "strings_arrays"),
    (0xFF0005, "nested_enum_raw"),
];

fn generate_payload(stem: &str, full_uid: u32, out_dir: &Path) -> PathBuf {
    let toml = vectors_dir().join("toml").join(format!("{stem}.toml"));
    let out = out_dir.join(format!("{stem}.bin"));
    config_to_binary_v3(&toml, &out, full_uid).expect("payload generation failed");
    out
}

/// Raw (unstamped) serialization: the form sequence entries pack.
fn generate_raw(stem: &str, out_dir: &Path) -> PathBuf {
    let toml = vectors_dir().join("toml").join(format!("{stem}.toml"));
    let out = out_dir.join(format!("{stem}_raw.bin"));
    config_to_binary(&toml, &out).expect("raw generation failed");
    out
}

fn generate_archive(payload_dir: &Path, out: &Path) {
    let entries: Vec<PackEntry> = ARCHIVE_ENTRIES
        .iter()
        .map(|(uid, stem)| PackEntry {
            full_uid: *uid,
            path: payload_dir.join(format!("{stem}.bin")),
        })
        .collect();
    pack(&entries, out).expect("archive generation failed");
}

#[test]
fn payloads_match_committed() {
    let tmp = tempfile::tempdir().unwrap();
    for (stem, uid) in PAYLOADS {
        let generated = generate_payload(stem, *uid, tmp.path());
        let committed = vectors_dir().join("payloads").join(format!("{stem}.bin"));
        let gen_bytes = fs::read(&generated).unwrap();
        let com_bytes = fs::read(&committed).unwrap_or_else(|_| {
            panic!("missing committed vector {stem}.bin -- run the regenerate test")
        });
        assert_eq!(
            gen_bytes, com_bytes,
            "{stem}: generator output diverged from the committed vector"
        );
    }
}

#[test]
fn archive_matches_committed() {
    let tmp = tempfile::tempdir().unwrap();
    for (stem, uid) in PAYLOADS {
        generate_payload(stem, *uid, tmp.path());
    }
    generate_raw("nested_enum", tmp.path());
    let generated = tmp.path().join("basic.tprm");
    generate_archive(tmp.path(), &generated);

    let committed = vectors_dir().join("archives").join("basic.tprm");
    let gen_bytes = fs::read(&generated).unwrap();
    let com_bytes = fs::read(&committed)
        .expect("missing committed archive basic.tprm -- run the regenerate test");
    assert_eq!(
        gen_bytes, com_bytes,
        "archive: packer output diverged from the committed vector"
    );
}

/// Writes the committed vector set from the TOML sources. Run explicitly
/// after an intentional format change; the diff is the contract change.
#[test]
#[ignore]
fn regenerate() {
    let payloads = vectors_dir().join("payloads");
    let archives = vectors_dir().join("archives");
    fs::create_dir_all(&payloads).unwrap();
    fs::create_dir_all(&archives).unwrap();
    for (stem, uid) in PAYLOADS {
        generate_payload(stem, *uid, &payloads);
    }
    generate_raw("nested_enum", &payloads);
    generate_archive(&payloads, &archives.join("basic.tprm"));
}
