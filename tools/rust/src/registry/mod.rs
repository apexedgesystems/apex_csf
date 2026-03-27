//! Registry RDAT file parsing and analysis.
//!
//! This module provides types and functions for parsing the RDAT (Registry Data)
//! binary format exported by ApexRegistry. The format contains:
//!
//! - Component entries (fullUid, name, type, task/data indices)
//! - Task entries (owning component, taskUid, name)
//! - Data entries (owning component, category, name, size)
//! - String table (null-terminated component/task/data names)
//!
//! # Format Versions
//!
//! - Version 1: Original format without ComponentType
//! - Version 2: Added ComponentType field to component entries
//!
//! # Example
//!
//! ```ignore
//! use apex_rust_tools::registry::{RdatFile, ComponentType};
//!
//! let rdat = RdatFile::from_file("registry.rdat").unwrap();
//! for comp in rdat.components() {
//!     println!("{}: {} (type={:?})", comp.full_uid, comp.name, comp.component_type);
//! }
//! ```

mod rdat;

pub use rdat::*;
