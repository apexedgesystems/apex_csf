//! SDAT binary format parsing.
//!
//! Format layout (little-endian):
//!
//! ```text
//! +----------------+
//! | Header (16B)   |
//! +----------------+
//! | Tasks (24B)    |  (scheduling configuration)
//! +----------------+
//! | Tick Schedule  |  (variable: tick headers + task indices)
//! +----------------+
//! | String Table   |  (null-terminated task names)
//! +----------------+
//! ```

use std::collections::HashMap;
use std::fs::File;
use std::io::{self, Read};
use std::path::Path;

/* ----------------------------- Constants ----------------------------- */

/// SDAT magic bytes.
pub const SDAT_MAGIC: [u8; 4] = *b"SDAT";

/// Current format version.
pub const SDAT_VERSION: u16 = 1;

/// Header size (16 bytes).
pub const HEADER_SIZE: usize = 16;

/// Task entry size (24 bytes).
pub const TASK_ENTRY_SIZE: usize = 24;

/// Tick entry header size (4 bytes).
pub const TICK_ENTRY_SIZE: usize = 4;

/// No sequence group marker.
pub const NO_SEQUENCE_GROUP: u8 = 0xFF;

/* ----------------------------- Error Type ----------------------------- */

/// SDAT parsing errors.
#[derive(Debug)]
pub enum SdatError {
    /// File I/O error.
    Io(io::Error),
    /// Invalid magic bytes.
    InvalidMagic,
    /// Unsupported version.
    UnsupportedVersion(u16),
    /// File truncated.
    Truncated,
    /// Invalid string reference.
    InvalidStringOffset,
}

impl std::fmt::Display for SdatError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SdatError::Io(e) => write!(f, "I/O error: {}", e),
            SdatError::InvalidMagic => write!(f, "Invalid SDAT magic bytes"),
            SdatError::UnsupportedVersion(v) => write!(f, "Unsupported SDAT version: {}", v),
            SdatError::Truncated => write!(f, "File truncated"),
            SdatError::InvalidStringOffset => write!(f, "Invalid string offset"),
        }
    }
}

impl std::error::Error for SdatError {}

impl From<io::Error> for SdatError {
    fn from(e: io::Error) -> Self {
        SdatError::Io(e)
    }
}

/* ----------------------------- SdatTask ----------------------------- */

/// Parsed task entry.
#[derive(Debug, Clone)]
pub struct SdatTask {
    /// Owner component's full UID.
    pub full_uid: u32,
    /// Task UID within component.
    pub task_uid: u8,
    /// Thread pool index.
    pub pool_index: u8,
    /// Frequency numerator (Hz).
    pub freq_n: u16,
    /// Frequency denominator (>=1).
    pub freq_d: u16,
    /// Tick offset within period.
    pub offset: u16,
    /// Task priority.
    pub priority: i8,
    /// Sequence group (0xFF = none).
    pub sequence_group: u8,
    /// Phase within sequence group.
    pub sequence_phase: u8,
    /// Task name (from string table).
    pub name: String,
}

impl SdatTask {
    /// Calculate effective frequency in Hz.
    pub fn frequency(&self) -> f64 {
        if self.freq_d == 0 {
            return 0.0;
        }
        self.freq_n as f64 / self.freq_d as f64
    }

    /// Check if task is sequenced.
    pub fn is_sequenced(&self) -> bool {
        self.sequence_group != NO_SEQUENCE_GROUP
    }

    /// Extract component ID from full UID.
    pub fn component_id(&self) -> u16 {
        (self.full_uid >> 8) as u16
    }

    /// Extract instance index from full UID.
    pub fn instance_index(&self) -> u8 {
        (self.full_uid & 0xFF) as u8
    }
}

/* ----------------------------- SdatTickEntry ----------------------------- */

/// Parsed tick schedule entry.
#[derive(Debug, Clone)]
pub struct SdatTickEntry {
    /// Tick number.
    pub tick: u16,
    /// Indices into task array for tasks running on this tick.
    pub task_indices: Vec<u16>,
}

/* ----------------------------- SdatFile ----------------------------- */

/// Parsed SDAT file contents.
#[derive(Debug)]
pub struct SdatFile {
    /// Format version.
    pub version: u16,
    /// Flags (reserved).
    pub flags: u16,
    /// Fundamental frequency (ticks/sec).
    pub fundamental_freq: u16,
    /// Total ticks in schedule table.
    pub tick_count: u16,
    /// All task entries.
    pub tasks: Vec<SdatTask>,
    /// Per-tick schedule (tick -> task indices).
    pub tick_schedule: Vec<SdatTickEntry>,
}

impl SdatFile {
    /// Parse SDAT file from path.
    pub fn from_file<P: AsRef<Path>>(path: P) -> Result<Self, SdatError> {
        let mut file = File::open(path)?;
        let mut data = Vec::new();
        file.read_to_end(&mut data)?;
        Self::from_bytes(&data)
    }

    /// Parse SDAT from byte slice.
    pub fn from_bytes(data: &[u8]) -> Result<Self, SdatError> {
        if data.len() < HEADER_SIZE {
            return Err(SdatError::Truncated);
        }

        // Parse header
        let magic = &data[0..4];
        if magic != SDAT_MAGIC {
            return Err(SdatError::InvalidMagic);
        }

        let version = u16::from_le_bytes([data[4], data[5]]);
        if version != SDAT_VERSION {
            return Err(SdatError::UnsupportedVersion(version));
        }

        let flags = u16::from_le_bytes([data[6], data[7]]);
        let fundamental_freq = u16::from_le_bytes([data[8], data[9]]);
        let task_count = u16::from_le_bytes([data[10], data[11]]);
        let tick_count = u16::from_le_bytes([data[12], data[13]]);

        // Calculate table offsets
        let task_table_offset = HEADER_SIZE;
        let task_table_size = task_count as usize * TASK_ENTRY_SIZE;
        let tick_schedule_offset = task_table_offset + task_table_size;

        if data.len() < tick_schedule_offset {
            return Err(SdatError::Truncated);
        }

        // Parse tasks (without names yet - need string table)
        let mut tasks: Vec<SdatTask> = Vec::with_capacity(task_count as usize);
        let mut name_offsets: Vec<u32> = Vec::with_capacity(task_count as usize);

        for i in 0..task_count as usize {
            let offset = task_table_offset + i * TASK_ENTRY_SIZE;
            let entry = &data[offset..offset + TASK_ENTRY_SIZE];

            let full_uid = u32::from_le_bytes([entry[0], entry[1], entry[2], entry[3]]);
            let task_uid = entry[4];
            let pool_index = entry[5];
            let freq_n = u16::from_le_bytes([entry[6], entry[7]]);
            let freq_d = u16::from_le_bytes([entry[8], entry[9]]);
            let task_offset = u16::from_le_bytes([entry[10], entry[11]]);
            let priority = entry[12] as i8;
            let sequence_group = entry[13];
            let sequence_phase = entry[14];
            // entry[15] is reserved
            let name_offset = u32::from_le_bytes([entry[16], entry[17], entry[18], entry[19]]);
            // entry[20..24] is reserved

            tasks.push(SdatTask {
                full_uid,
                task_uid,
                pool_index,
                freq_n,
                freq_d,
                offset: task_offset,
                priority,
                sequence_group,
                sequence_phase,
                name: String::new(), // Will be filled from string table
            });
            name_offsets.push(name_offset);
        }

        // Parse tick schedule
        let mut tick_schedule: Vec<SdatTickEntry> = Vec::new();
        let mut pos = tick_schedule_offset;

        while pos + TICK_ENTRY_SIZE <= data.len() {
            // Check if this looks like a tick entry (tick number should be < tick_count)
            let tick = u16::from_le_bytes([data[pos], data[pos + 1]]);
            let tasks_on_tick = u16::from_le_bytes([data[pos + 2], data[pos + 3]]);

            // If tick >= tick_count, we've likely reached the string table
            if tick >= tick_count {
                break;
            }

            pos += TICK_ENTRY_SIZE;

            // Read task indices
            let indices_size = tasks_on_tick as usize * 2;
            if pos + indices_size > data.len() {
                break;
            }

            let mut task_indices = Vec::with_capacity(tasks_on_tick as usize);
            for _ in 0..tasks_on_tick {
                let idx = u16::from_le_bytes([data[pos], data[pos + 1]]);
                task_indices.push(idx);
                pos += 2;
            }

            tick_schedule.push(SdatTickEntry { tick, task_indices });
        }

        // String table starts at pos
        let string_table_offset = pos;
        let string_table = &data[string_table_offset..];

        // Resolve task names from string table
        for (i, task) in tasks.iter_mut().enumerate() {
            let name_off = name_offsets[i] as usize;
            if name_off < string_table.len() {
                // Find null terminator
                let end = string_table[name_off..]
                    .iter()
                    .position(|&b| b == 0)
                    .unwrap_or(string_table.len() - name_off);
                task.name =
                    String::from_utf8_lossy(&string_table[name_off..name_off + end]).to_string();
            }
        }

        Ok(SdatFile {
            version,
            flags,
            fundamental_freq,
            tick_count,
            tasks,
            tick_schedule,
        })
    }

    /// Get number of tasks.
    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }

    /// Get number of active ticks (ticks with at least one task).
    pub fn active_tick_count(&self) -> usize {
        self.tick_schedule.len()
    }

    /// Get tasks grouped by frequency.
    pub fn tasks_by_frequency(&self) -> HashMap<String, Vec<&SdatTask>> {
        let mut map: HashMap<String, Vec<&SdatTask>> = HashMap::new();
        for task in &self.tasks {
            let key = format!("{:.1}Hz", task.frequency());
            map.entry(key).or_default().push(task);
        }
        map
    }

    /// Get tasks grouped by component.
    pub fn tasks_by_component(&self) -> HashMap<u32, Vec<&SdatTask>> {
        let mut map: HashMap<u32, Vec<&SdatTask>> = HashMap::new();
        for task in &self.tasks {
            map.entry(task.full_uid).or_default().push(task);
        }
        map
    }

    /// Get maximum tasks on any single tick.
    pub fn max_tasks_per_tick(&self) -> usize {
        self.tick_schedule
            .iter()
            .map(|t| t.task_indices.len())
            .max()
            .unwrap_or(0)
    }

    /// Get average tasks per active tick.
    pub fn avg_tasks_per_tick(&self) -> f64 {
        if self.tick_schedule.is_empty() {
            return 0.0;
        }
        let total: usize = self
            .tick_schedule
            .iter()
            .map(|t| t.task_indices.len())
            .sum();
        total as f64 / self.tick_schedule.len() as f64
    }
}
