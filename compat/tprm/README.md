# TPRM conformance contract

The byte-level golden vectors for the TPRM formats. Any implementation
that produces or consumes TPRM data answers to these files, not to
another implementation's source code. `make compat-tprm` runs every
in-repo suite that pins this contract.

## Contents

| Path                     | What it is                                                                                     |
| ------------------------ | ---------------------------------------------------------------------------------------------- |
| `toml/*.toml`            | Vector sources in the authoring-template format; the intended values are readable here         |
| `payloads/*.bin`         | Format A v3: 20-byte prelude (magic APV3, version, size, fullUid, layout hash, CRC-32) + body  |
| `payloads/*_raw.bin`     | The unstamped serialization -- the form sequence entries pack                                  |
| `archives/basic.tprm`    | Format B: a packed archive (v3 component entries + a raw RTS-reserved entry)                   |

The payload uids are part of the contract: `scalar_types` targets
0x000000, `strings_arrays` 0x00D001, `nested_enum` 0x00CA00, and
`scheduler_shape` 0x000100 (the scheduler TPRM: header + 15-byte task
entries, the shape a C2 backend needs to render or author schedules). A reader
must verify the prelude (wrong magic, version, target, size, or CRC
each reject distinctly) before touching the body.

## Who pins what

- **tools/rust** (reference implementation):
  `tests/golden_vectors.rs` regenerates every vector and asserts
  byte-identity with the committed files.
- **C++ runtime**: `GoldenVectors_uTest.cpp` verifies the same files
  through the v3 payload reader and `PackedTprmReader` and asserts
  every prelude check, field, and extraction byte.
- **External consumers** (e.g. the Zenith C2 backend): copy this
  directory and assert your encoder/decoder against it. Copy, don't
  reference -- the vector set is the contract, the repos stay
  independent.

## Changing the format

A format change is a contract change: update the implementation, run
the rust regeneration (`cargo test --test golden_vectors regenerate --
--ignored`), and review the binary diffs alongside the code. Both
language suites failing together on an unregenerated change is the
mechanism working.
