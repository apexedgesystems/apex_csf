# Rust tools

Small, focused command-line tools sharing library crates, organized by
the role that consumes them. How they build and where they install:
[rust_workspace.md](rust_workspace.md).

---

## Build role: what the apex build stands on

Crates `apex-tprm` (library) and `apex-build-tools` under
`tools/build/rust/`; executables in `bin/tools/build/`.

- **[cfg2bin](../build/rust/apex-build-tools/docs/cfg2bin.md)**
  Converts TOML/JSON config files to binary blobs for tunable parameters.

- **[tprm_pack](../build/rust/apex-build-tools/docs/tprm_pack.md)**
  Pack/unpack/list/diff TPRM archives for multi-component configuration.

- **cdef_gen**
  Generates the `.auto` component headers from their specs (see the
  [proto profile](../build/rust/apex-tprm/docs/proto_profile.md) it reads).

- **[apex_data_gen](../build/rust/apex-build-tools/docs/apex_data_gen.md)**
  Generates JSON struct dictionaries from `apex_data.toml` manifests for
  external C2 systems, fault injection, and debugging.

## Ops role: run beside a deployed system

Crate `apex-ops-tools` under `tools/ops/rust/`; executables in
`bin/tools/ops/`.

- **[tprm_template](../ops/rust/apex-ops-tools/docs/tprm_template.md)**
  Generates TOML templates from C++ headers or JSON struct dictionaries.

- **[upx_tool](../ops/rust/apex-ops-tools/docs/upx_tool.md)**
  UPX helper to test, compress, decompress, verify, compare `.so <-> .so.upx`
  pairs, and patch binaries safely.

- **[serial_dev_checker](../ops/rust/apex-ops-tools/docs/serial_dev_checker.md)**
  Scans and reports the status of serial devices.

- **[serial_dev_tester](../ops/rust/apex-ops-tools/docs/serial_dev_tester.md)**
  Verifies serial loopback and interconnect wiring.

## Analysis role: inspect what a run left behind

Crate `apex-analysis-tools` under `tools/analysis/rust/`; executables in
`bin/tools/analysis/`.

- **[rdat_tool](../analysis/rust/apex-analysis-tools/docs/rdat_tool.md)**
  Registry RDAT file analysis -- info, dump, JSON export, and SQLite export.

- **[sdat_tool](../analysis/rust/apex-analysis-tools/docs/sdat_tool.md)**
  Scheduler SDAT file analysis -- info, dump, and JSON export.

---

## Building and running

Through the apex build, by role:

```bash
make tools-build          # the build role
make tools-ops            # or tools-analysis
make tools                # everything
```

Directly with cargo, from `tools/`:

```bash
cargo build -p apex-build-tools
cargo run -p apex-build-tools --bin cfg2bin -- --help
cargo run -p apex-analysis-tools --bin rdat_tool -- --help
```

## Testing

```bash
make test-rust                        # cargo test --workspace
cd tools && cargo test -p apex-tprm   # one crate
```

Unit tests live inline with the modules; integration tests sit under
each crate's `tests/`.

## Documentation

Each tool has a page under its crate's `docs/` (linked above). Library
APIs are documented with Rustdoc:

```bash
cd tools && cargo doc --workspace --open
```
