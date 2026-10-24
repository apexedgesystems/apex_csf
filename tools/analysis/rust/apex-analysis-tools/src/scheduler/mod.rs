//! Scheduler export format parsing and tools.

pub mod sdat;

pub use sdat::{SdatError, SdatFile, SdatTask, SdatTickEntry, NO_SEQUENCE_GROUP};
