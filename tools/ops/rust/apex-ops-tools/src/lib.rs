//! Apex ops tools library
//!
//! Shared code for the tools that run beside a deployed apex system:
//!   - serial device validation (`serial`): probing, loopback and
//!     interconnect tests behind `serial_dev_checker` and `serial_dev_tester`
//!   - UPX compression helpers (`upx`): single and bulk compress, decompress
//!     and compare behind `upx_tool`
//!
//! `tprm_template` builds on the `apex_tprm` crate and has no module here.

pub mod serial;
pub mod upx;
