//! Apex Rust Tools Library
//!
//! This library hosts reusable components for the apex-rust-tools executables.
//! Right now that includes:
//!   - serial device validation (`serial`)
//!   - tunable parameter tooling (`tunable_params`)
//!   - UPX compression helpers (`upx`)
//!   - registry RDAT parsing (`registry`)
//!   - scheduler SDAT parsing (`scheduler`)
//!
//! Executables:
//!   - `serial_dev_checker`: probes ports and prints status
//!   - `serial_dev_tester`: runs loopback & interconnect tests
//!   - `tprm_template`: generates TOML templates from C++ headers or JSON dictionaries
//!   - `cfg2bin`: converts TOML/JSON config to binary (single or batch)
//!   - `tprm_pack`: packs/unpacks/lists TPRM archives
//!   - `upx_tool`: UPX binary compression helper
//!   - `rdat_tool`: registry RDAT file analysis
//!   - `sdat_tool`: scheduler SDAT file analysis
//!
//! Benchmarking tools have moved to Vernier:
//!   https://github.com/apexedgesystems/vernier
//!
//! Shared code lives under `apex_rust_tools::<module>`.

pub mod registry;
pub mod scheduler;
pub mod serial;
pub mod tunable_params;
pub mod upx;
