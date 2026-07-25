# TPRM conformance contract

The byte-level golden vectors for the TPRM formats. Any implementation
that produces or consumes TPRM data answers to these files, not to
another implementation's source code. `make compat-tprm` runs every
in-repo suite that pins this contract.

## Contents

| Path                  | What it is                                                                                     |
| --------------------- | ---------------------------------------------------------------------------------------------- |
| `toml/*.toml`         | Vector sources in the authoring-template format; the intended values are readable here         |
| `payloads/*.bin`      | Format A: raw little-endian struct images generated from the sources                           |
| `archives/basic.tprm` | Format B: a packed archive of the payloads (normal, multi-instance, and RTS-reserved fullUids) |

## Who pins what

- **tools/rust** (reference implementation):
  `tests/golden_vectors.rs` regenerates every vector and asserts
  byte-identity with the committed files.
- **C++ runtime**: `GoldenVectors_uTest.cpp` loads the same files
  through `hex2cpp` and `PackedTprmReader` and asserts every field and
  extraction byte.
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
