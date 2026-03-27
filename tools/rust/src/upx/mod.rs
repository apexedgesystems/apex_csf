//! UPX helpers: test, compress, decompress, verify, compare, bulk, patch.
//!
//! Public surface mirrors the serial module style.

pub mod bulk;
pub mod compare; // sha256 + streaming compare
pub mod ops; // run upx, single-file ops
pub mod util; // naming, archiving, affinity helpers // bulk traversal & summaries (split into submodules internally)

pub use bulk::{
    run_bulk_compare_pairs, run_bulk_compress, run_bulk_decompress, run_bulk_verify, ArchiveMode,
    BulkOptions, BulkResult,
};
pub use compare::{compare_upx_to_original, files_identical, sha256_file};
pub use ops::{compress, decompress, ensure_upx_available, test};
pub use util::{make_output_name, NameTemplate};
