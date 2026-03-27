//! TPRM packing: combine multiple .tprm files into a single archive.
//!
//! Format (version 2):
//!   Header (8 bytes):
//!     magic[4]   = "TPRM"
//!     version[2] = 2 (little-endian)
//!     count[2]   = number of entries (little-endian)
//!
//!   Index (12 bytes x count):
//!     fullUid[4] = component fullUid (32-bit, little-endian)
//!     offset[4]  = byte offset from start of data section (little-endian)
//!     size[4]    = size in bytes (little-endian)
//!
//!   Data:
//!     [entry bytes concatenated]
//!
//! fullUid = (componentId << 8) | instanceIndex
//! This allows per-instance configuration for multi-instance components.

use std::{fs, io, path::Path};

use super::Error;

/* ----------------------------- Constants ----------------------------- */

const MAGIC: &[u8; 4] = b"TPRM";
const VERSION: u16 = 2;
const HEADER_SIZE: usize = 8;
const INDEX_ENTRY_SIZE: usize = 12;

/* ----------------------------- Public Types ----------------------------- */

/// An entry for packing: fullUid + file path.
#[derive(Debug, Clone)]
pub struct PackEntry {
    pub full_uid: u32,
    pub path: std::path::PathBuf,
}

/// An unpacked entry read from archive.
#[derive(Debug, Clone)]
pub struct UnpackedEntry {
    pub full_uid: u32,
    pub offset: u32,
    pub size: u32,
    pub data: Vec<u8>,
}

/// Archive info for listing.
#[derive(Debug, Clone)]
pub struct ArchiveInfo {
    pub version: u16,
    pub entries: Vec<EntryInfo>,
}

/// Entry info for listing.
#[derive(Debug, Clone)]
pub struct EntryInfo {
    pub full_uid: u32,
    pub offset: u32,
    pub size: u32,
}

/* ----------------------------- Public API ----------------------------- */

/// Pack multiple .tprm files into a single archive.
///
/// Entries are sorted by fullUid before packing.
pub fn pack(entries: &[PackEntry], output_path: &Path) -> Result<PackResult, Error> {
    if entries.is_empty() {
        return Err(Error::InvalidArgs("no entries to pack".to_string()));
    }

    // Read all input files
    let mut tprm_entries: Vec<(u32, Vec<u8>)> = Vec::with_capacity(entries.len());
    for entry in entries {
        if !entry.path.exists() {
            return Err(Error::Io(io::Error::new(
                io::ErrorKind::NotFound,
                format!("input file not found: {}", entry.path.display()),
            )));
        }
        let data = fs::read(&entry.path)?;
        tprm_entries.push((entry.full_uid, data));
    }

    // Sort by fullUid
    tprm_entries.sort_by_key(|(uid, _)| *uid);

    // Build header
    let count = tprm_entries.len() as u16;
    let mut output = Vec::new();
    output.extend_from_slice(MAGIC);
    output.extend_from_slice(&VERSION.to_le_bytes());
    output.extend_from_slice(&count.to_le_bytes());

    // Build index and collect data
    let mut data_parts: Vec<&[u8]> = Vec::new();
    let mut offset: u32 = 0;

    for (full_uid, data) in &tprm_entries {
        // Index entry: fullUid(4) + offset(4) + size(4) = 12 bytes
        output.extend_from_slice(&full_uid.to_le_bytes());
        output.extend_from_slice(&offset.to_le_bytes());
        output.extend_from_slice(&(data.len() as u32).to_le_bytes());

        data_parts.push(data);
        offset += data.len() as u32;
    }

    // Append all data
    for data in data_parts {
        output.extend_from_slice(data);
    }

    // Write output
    fs::write(output_path, &output)?;

    Ok(PackResult {
        entry_count: count as usize,
        total_size: output.len(),
    })
}

/// Result of a pack operation.
#[derive(Debug, Clone)]
pub struct PackResult {
    pub entry_count: usize,
    pub total_size: usize,
}

/// Unpack a TPRM archive to individual files.
///
/// Files are written as `{full_uid:06x}.tprm` in the output directory.
pub fn unpack(archive_path: &Path, output_dir: &Path) -> Result<Vec<UnpackedEntry>, Error> {
    let data = fs::read(archive_path)?;
    let entries = parse_archive(&data)?;

    fs::create_dir_all(output_dir)?;

    for entry in &entries {
        let filename = format!("{:06x}.tprm", entry.full_uid);
        let output_path = output_dir.join(&filename);
        fs::write(&output_path, &entry.data)?;
    }

    Ok(entries)
}

/// List entries in a TPRM archive without extracting.
pub fn list(archive_path: &Path) -> Result<ArchiveInfo, Error> {
    let data = fs::read(archive_path)?;

    // Validate header
    if data.len() < HEADER_SIZE {
        return Err(Error::Parse("archive too small for header".to_string()));
    }
    if &data[0..4] != MAGIC {
        return Err(Error::Parse("invalid magic bytes".to_string()));
    }

    let version = u16::from_le_bytes([data[4], data[5]]);
    let count = u16::from_le_bytes([data[6], data[7]]) as usize;

    let expected_index_size = HEADER_SIZE + count * INDEX_ENTRY_SIZE;
    if data.len() < expected_index_size {
        return Err(Error::Parse("archive too small for index".to_string()));
    }

    let mut entries = Vec::with_capacity(count);
    for i in 0..count {
        let idx_start = HEADER_SIZE + i * INDEX_ENTRY_SIZE;
        let full_uid = u32::from_le_bytes([
            data[idx_start],
            data[idx_start + 1],
            data[idx_start + 2],
            data[idx_start + 3],
        ]);
        let offset = u32::from_le_bytes([
            data[idx_start + 4],
            data[idx_start + 5],
            data[idx_start + 6],
            data[idx_start + 7],
        ]);
        let size = u32::from_le_bytes([
            data[idx_start + 8],
            data[idx_start + 9],
            data[idx_start + 10],
            data[idx_start + 11],
        ]);
        entries.push(EntryInfo {
            full_uid,
            offset,
            size,
        });
    }

    Ok(ArchiveInfo { version, entries })
}

/// Compare two archives and return differences.
pub fn diff(old_path: &Path, new_path: &Path) -> Result<DiffResult, Error> {
    let old_data = fs::read(old_path)?;
    let new_data = fs::read(new_path)?;

    let old_entries = parse_archive(&old_data)?;
    let new_entries = parse_archive(&new_data)?;

    // Build maps by fullUid for comparison
    let mut old_map: std::collections::HashMap<u32, &UnpackedEntry> =
        std::collections::HashMap::new();
    for entry in &old_entries {
        old_map.insert(entry.full_uid, entry);
    }

    let mut new_map: std::collections::HashMap<u32, &UnpackedEntry> =
        std::collections::HashMap::new();
    for entry in &new_entries {
        new_map.insert(entry.full_uid, entry);
    }

    let mut added = Vec::new();
    let mut removed = Vec::new();
    let mut modified = Vec::new();
    let mut unchanged = Vec::new();

    // Find removed and modified entries
    for (uid, old_entry) in &old_map {
        match new_map.get(uid) {
            None => {
                removed.push(DiffEntry {
                    full_uid: *uid,
                    old_size: Some(old_entry.size),
                    new_size: None,
                    first_diff_offset: None,
                });
            }
            Some(new_entry) => {
                if old_entry.data == new_entry.data {
                    unchanged.push(DiffEntry {
                        full_uid: *uid,
                        old_size: Some(old_entry.size),
                        new_size: Some(new_entry.size),
                        first_diff_offset: None,
                    });
                } else {
                    // Find first difference
                    let first_diff = old_entry
                        .data
                        .iter()
                        .zip(new_entry.data.iter())
                        .position(|(a, b)| a != b)
                        .or_else(|| {
                            // Different lengths - diff at end of shorter
                            Some(old_entry.data.len().min(new_entry.data.len()))
                        });
                    modified.push(DiffEntry {
                        full_uid: *uid,
                        old_size: Some(old_entry.size),
                        new_size: Some(new_entry.size),
                        first_diff_offset: first_diff.map(|x| x as u32),
                    });
                }
            }
        }
    }

    // Find added entries
    for (uid, new_entry) in &new_map {
        if !old_map.contains_key(uid) {
            added.push(DiffEntry {
                full_uid: *uid,
                old_size: None,
                new_size: Some(new_entry.size),
                first_diff_offset: None,
            });
        }
    }

    // Sort results by fullUid for consistent output
    added.sort_by_key(|e| e.full_uid);
    removed.sort_by_key(|e| e.full_uid);
    modified.sort_by_key(|e| e.full_uid);
    unchanged.sort_by_key(|e| e.full_uid);

    Ok(DiffResult {
        added,
        removed,
        modified,
        unchanged,
    })
}

/// Result of comparing two archives.
#[derive(Debug, Clone)]
pub struct DiffResult {
    pub added: Vec<DiffEntry>,
    pub removed: Vec<DiffEntry>,
    pub modified: Vec<DiffEntry>,
    pub unchanged: Vec<DiffEntry>,
}

/// A single entry in the diff result.
#[derive(Debug, Clone)]
pub struct DiffEntry {
    pub full_uid: u32,
    pub old_size: Option<u32>,
    pub new_size: Option<u32>,
    pub first_diff_offset: Option<u32>,
}

/* ----------------------------- Internal Helpers ----------------------------- */

fn parse_archive(data: &[u8]) -> Result<Vec<UnpackedEntry>, Error> {
    // Validate header
    if data.len() < HEADER_SIZE {
        return Err(Error::Parse("archive too small for header".to_string()));
    }
    if &data[0..4] != MAGIC {
        return Err(Error::Parse("invalid magic bytes".to_string()));
    }

    let version = u16::from_le_bytes([data[4], data[5]]);
    if version != VERSION {
        return Err(Error::Parse(format!(
            "unsupported version: {} (expected {})",
            version, VERSION
        )));
    }

    let count = u16::from_le_bytes([data[6], data[7]]) as usize;

    let expected_index_size = HEADER_SIZE + count * INDEX_ENTRY_SIZE;
    if data.len() < expected_index_size {
        return Err(Error::Parse("archive too small for index".to_string()));
    }

    let data_start = expected_index_size;

    let mut entries = Vec::with_capacity(count);
    for i in 0..count {
        let idx_start = HEADER_SIZE + i * INDEX_ENTRY_SIZE;
        let full_uid = u32::from_le_bytes([
            data[idx_start],
            data[idx_start + 1],
            data[idx_start + 2],
            data[idx_start + 3],
        ]);
        let offset = u32::from_le_bytes([
            data[idx_start + 4],
            data[idx_start + 5],
            data[idx_start + 6],
            data[idx_start + 7],
        ]);
        let size = u32::from_le_bytes([
            data[idx_start + 8],
            data[idx_start + 9],
            data[idx_start + 10],
            data[idx_start + 11],
        ]);

        let abs_start = data_start + offset as usize;
        let abs_end = abs_start + size as usize;

        if abs_end > data.len() {
            return Err(Error::Parse(format!(
                "entry {} (fullUid 0x{:06x}) extends beyond archive",
                i, full_uid
            )));
        }

        entries.push(UnpackedEntry {
            full_uid,
            offset,
            size,
            data: data[abs_start..abs_end].to_vec(),
        });
    }

    Ok(entries)
}

/* ----------------------------- Tests ----------------------------- */

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn pack_and_unpack_roundtrip() {
        let temp = TempDir::new().unwrap();

        // Create test input files
        let file1 = temp.path().join("exec.tprm");
        let file2 = temp.path().join("model.tprm");
        fs::write(&file1, b"executive_data").unwrap();
        fs::write(&file2, b"model_data_longer").unwrap();

        // fullUid = (componentId << 8) | instanceIndex
        // Executive: componentId=0, instance=0 -> fullUid=0x000000
        // Model: componentId=101=0x65, instance=0 -> fullUid=0x006500
        let entries = vec![
            PackEntry {
                full_uid: 0x000000,
                path: file1,
            },
            PackEntry {
                full_uid: 0x006500, // componentId=101, instance=0
                path: file2,
            },
        ];

        // Pack
        let archive = temp.path().join("master.tprm");
        let result = pack(&entries, &archive).unwrap();
        assert_eq!(result.entry_count, 2);

        // Verify archive structure
        let data = fs::read(&archive).unwrap();
        assert_eq!(&data[0..4], b"TPRM");
        assert_eq!(u16::from_le_bytes([data[4], data[5]]), 2); // version
        assert_eq!(u16::from_le_bytes([data[6], data[7]]), 2); // count

        // Unpack
        let unpack_dir = temp.path().join("unpacked");
        let unpacked = unpack(&archive, &unpack_dir).unwrap();
        assert_eq!(unpacked.len(), 2);
        assert_eq!(unpacked[0].full_uid, 0x000000);
        assert_eq!(unpacked[0].data, b"executive_data");
        assert_eq!(unpacked[1].full_uid, 0x006500);
        assert_eq!(unpacked[1].data, b"model_data_longer");

        // Verify files written
        assert!(unpack_dir.join("000000.tprm").exists());
        assert!(unpack_dir.join("006500.tprm").exists());
    }

    #[test]
    fn list_archive() {
        let temp = TempDir::new().unwrap();

        // Create test input
        let file1 = temp.path().join("a.tprm");
        let file2 = temp.path().join("b.tprm");
        let file3 = temp.path().join("c.tprm");
        fs::write(&file1, b"aaa").unwrap();
        fs::write(&file2, b"bbbbb").unwrap();
        fs::write(&file3, b"c").unwrap();

        // Scheduler (componentId=1), two models (componentId=102,103)
        let entries = vec![
            PackEntry {
                full_uid: 0x000100, // componentId=1, instance=0
                path: file1,
            },
            PackEntry {
                full_uid: 0x006600, // componentId=102, instance=0
                path: file2,
            },
            PackEntry {
                full_uid: 0x006700, // componentId=103, instance=0
                path: file3,
            },
        ];

        let archive = temp.path().join("test.tprm");
        pack(&entries, &archive).unwrap();

        let info = list(&archive).unwrap();
        assert_eq!(info.version, 2);
        assert_eq!(info.entries.len(), 3);

        // Entries should be sorted by fullUid
        assert_eq!(info.entries[0].full_uid, 0x000100);
        assert_eq!(info.entries[0].size, 3);
        assert_eq!(info.entries[1].full_uid, 0x006600);
        assert_eq!(info.entries[1].size, 5);
        assert_eq!(info.entries[2].full_uid, 0x006700);
        assert_eq!(info.entries[2].size, 1);
    }

    #[test]
    fn pack_sorts_by_full_uid() {
        let temp = TempDir::new().unwrap();

        // Create files with IDs out of order
        let file1 = temp.path().join("z.tprm");
        let file2 = temp.path().join("a.tprm");
        fs::write(&file1, b"zzz").unwrap();
        fs::write(&file2, b"aaa").unwrap();

        let entries = vec![
            PackEntry {
                full_uid: 0x006400, // Higher fullUid first (componentId=100)
                path: file1,
            },
            PackEntry {
                full_uid: 0x000100, // Lower fullUid second (componentId=1)
                path: file2,
            },
        ];

        let archive = temp.path().join("sorted.tprm");
        pack(&entries, &archive).unwrap();

        let info = list(&archive).unwrap();
        // Should be sorted: 0x000100 before 0x006400
        assert_eq!(info.entries[0].full_uid, 0x000100);
        assert_eq!(info.entries[1].full_uid, 0x006400);
    }

    #[test]
    fn rejects_empty_entries() {
        let temp = TempDir::new().unwrap();
        let archive = temp.path().join("empty.tprm");
        let result = pack(&[], &archive);
        assert!(result.is_err());
    }

    #[test]
    fn rejects_missing_input_file() {
        let temp = TempDir::new().unwrap();
        let entries = vec![PackEntry {
            full_uid: 0,
            path: temp.path().join("nonexistent.tprm"),
        }];
        let archive = temp.path().join("out.tprm");
        let result = pack(&entries, &archive);
        assert!(result.is_err());
    }

    #[test]
    fn rejects_invalid_magic() {
        let temp = TempDir::new().unwrap();
        let bad_archive = temp.path().join("bad.tprm");
        fs::write(&bad_archive, b"NOPE\x02\x00\x00\x00").unwrap();

        let result = list(&bad_archive);
        assert!(result.is_err());
    }

    #[test]
    fn rejects_truncated_archive() {
        let temp = TempDir::new().unwrap();
        let bad_archive = temp.path().join("trunc.tprm");
        // Header says 1 entry but no index data
        fs::write(&bad_archive, b"TPRM\x02\x00\x01\x00").unwrap();

        let result = list(&bad_archive);
        assert!(result.is_err());
    }

    #[test]
    fn handles_multi_instance_components() {
        // Multi-instance support: same componentId, different instanceIndex
        let temp = TempDir::new().unwrap();

        let file1 = temp.path().join("poly0.tprm");
        let file2 = temp.path().join("poly1.tprm");
        fs::write(&file1, b"instance0").unwrap();
        fs::write(&file2, b"instance1").unwrap();

        // PolynomialModel (componentId=102=0x66) with two instances
        let entries = vec![
            PackEntry {
                full_uid: 0x006600, // componentId=102, instance=0
                path: file1,
            },
            PackEntry {
                full_uid: 0x006601, // componentId=102, instance=1
                path: file2,
            },
        ];

        let archive = temp.path().join("multi.tprm");
        pack(&entries, &archive).unwrap();

        let info = list(&archive).unwrap();
        assert_eq!(info.entries.len(), 2);
        assert_eq!(info.entries[0].full_uid, 0x006600);
        assert_eq!(info.entries[1].full_uid, 0x006601);

        // Unpack and verify separate files
        let unpack_dir = temp.path().join("unpacked");
        unpack(&archive, &unpack_dir).unwrap();
        assert!(unpack_dir.join("006600.tprm").exists());
        assert!(unpack_dir.join("006601.tprm").exists());
    }
}
