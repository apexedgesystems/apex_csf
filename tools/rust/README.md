# apex_csf_tools

A collection of small Rust utilities with a shared core library.
Each utility is focused, self-contained, and can be built and run independently.

---

## Current Utilities

### Configuration and Tunable Parameters

- **[tprm_template](docs/tprm_template.md)**
  Generates TOML templates from C++ headers or JSON struct dictionaries.

- **[cfg2bin](docs/cfg2bin.md)**
  Converts TOML/JSON config files to binary blobs for tunable parameters.

- **[tprm_pack](docs/tprm_pack.md)**
  Pack/unpack/list/diff TPRM archives for multi-component configuration.

- **[apex_data_gen](docs/apex_data_gen.md)**
  Generates JSON struct dictionaries from `apex_data.toml` manifests for
  external C2 systems, fault injection, and debugging.

### System Metadata Analysis

- **[rdat_tool](docs/rdat_tool.md)**
  Registry RDAT file analysis -- info, dump, JSON export, and SQLite export.

- **[sdat_tool](docs/sdat_tool.md)**
  Scheduler SDAT file analysis -- info, dump, and JSON export.

### Hardware and Testing

- **[serial_dev_checker](docs/serial_dev_checker.md)**
  Scans and reports the status of serial devices.

- **[serial_dev_tester](docs/serial_dev_tester.md)**
  Verifies serial loopback and interconnect wiring.

### Binary Management

- **[upx_tool](docs/upx_tool.md)**
  UPX helper to test, compress, decompress, verify, compare `.so <-> .so.upx`
  pairs, and patch binaries safely.

---

## Building

```bash
cargo build
```

## Running

```bash
cargo run --bin <tool> -- --help
```

---

## Testing

```bash
cargo test
```

- Unit tests live inline with modules.
- Integration tests are under `tests/`.

---

## Documentation

- Each utility has its own detailed doc in the [`docs/`](docs) directory.
- Shared library code is documented with Rustdoc:

```bash
cargo doc --open
```
