//! Apex analysis tools library
//!
//! Readers for the data files an apex system writes for later inspection:
//!   - registry RDAT parsing (`registry`) behind `rdat_tool`
//!   - scheduler SDAT parsing (`scheduler`) behind `sdat_tool`

pub mod registry;
pub mod scheduler;
