//! RDAT binary format parsing.
//!
//! Format layout (little-endian):
//!
//! ```text
//! +----------------+
//! | Header (16B)   |
//! +----------------+
//! | Components     |  (24B or 32B each depending on version)
//! +----------------+
//! | Tasks (16B)    |
//! +----------------+
//! | Data (24B)     |
//! +----------------+
//! | String Table   |  (null-terminated strings)
//! +----------------+
//! ```

use std::collections::HashMap;
use std::fs::File;
use std::io::{self, Read};
use std::path::Path;

/* ----------------------------- Constants ----------------------------- */

/// RDAT magic bytes.
pub const RDAT_MAGIC: [u8; 4] = *b"RDAT";

/// Original format version (no ComponentType).
pub const RDAT_VERSION_1: u16 = 1;

/// Current format version (with ComponentType).
pub const RDAT_VERSION_2: u16 = 2;

/// Component entry size (24 bytes for both v1 and v2).
/// V2 uses byte 16 for ComponentType instead of reserved.
pub const COMPONENT_ENTRY_SIZE: usize = 24;

/// Task entry size (16 bytes).
pub const TASK_ENTRY_SIZE: usize = 16;

/// Data entry size (24 bytes).
pub const DATA_ENTRY_SIZE: usize = 24;

/// Header size (16 bytes).
pub const HEADER_SIZE: usize = 16;

/* ----------------------------- ComponentType ----------------------------- */

/// Component type classification.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ComponentType {
    /// Root executive component (singleton).
    Executive = 0,
    /// Core infrastructure (scheduler, filesystem).
    Core = 1,
    /// Software/environment simulation models.
    SwModel = 2,
    /// Hardware emulation models.
    HwModel = 3,
    /// Runtime support services.
    Support = 4,
    /// Real hardware interfaces.
    Driver = 5,
    /// Unknown type (for forward compatibility).
    Unknown = 255,
}

impl ComponentType {
    /// Parse from raw byte value.
    pub fn from_u8(value: u8) -> Self {
        match value {
            0 => ComponentType::Executive,
            1 => ComponentType::Core,
            2 => ComponentType::SwModel,
            3 => ComponentType::HwModel,
            4 => ComponentType::Support,
            5 => ComponentType::Driver,
            _ => ComponentType::Unknown,
        }
    }

    /// Human-readable string representation.
    pub fn as_str(&self) -> &'static str {
        match self {
            ComponentType::Executive => "EXECUTIVE",
            ComponentType::Core => "CORE",
            ComponentType::SwModel => "SW_MODEL",
            ComponentType::HwModel => "HW_MODEL",
            ComponentType::Support => "SUPPORT",
            ComponentType::Driver => "DRIVER",
            ComponentType::Unknown => "UNKNOWN",
        }
    }

    /// Check if this is a model type.
    pub fn is_model(&self) -> bool {
        matches!(self, ComponentType::SwModel | ComponentType::HwModel)
    }

    /// Check if this is core infrastructure.
    pub fn is_core_infra(&self) -> bool {
        matches!(self, ComponentType::Executive | ComponentType::Core)
    }

    /// Check if this is schedulable (can have tasks).
    pub fn is_schedulable(&self) -> bool {
        matches!(
            self,
            ComponentType::SwModel
                | ComponentType::HwModel
                | ComponentType::Support
                | ComponentType::Driver
        )
    }
}

impl std::fmt::Display for ComponentType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

/* ----------------------------- DataCategory ----------------------------- */

/// Data category classification.
/// Matches C++ DataCategory enum values.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum DataCategory {
    /// Static parameters (compile-time constant).
    StaticParam = 0,
    /// Tunable parameters (runtime-configurable via TPRM).
    TunableParam = 1,
    /// State data (runtime-mutable).
    State = 2,
    /// Input data.
    Input = 3,
    /// Output data.
    Output = 4,
    /// Unknown category.
    Unknown = 255,
}

impl DataCategory {
    /// Parse from raw byte value.
    pub fn from_u8(value: u8) -> Self {
        match value {
            0 => DataCategory::StaticParam,
            1 => DataCategory::TunableParam,
            2 => DataCategory::State,
            3 => DataCategory::Input,
            4 => DataCategory::Output,
            _ => DataCategory::Unknown,
        }
    }

    /// Human-readable string representation.
    pub fn as_str(&self) -> &'static str {
        match self {
            DataCategory::State => "STATE",
            DataCategory::StaticParam => "STATIC_PARAM",
            DataCategory::TunableParam => "TUNABLE_PARAM",
            DataCategory::Input => "INPUT",
            DataCategory::Output => "OUTPUT",
            DataCategory::Unknown => "UNKNOWN",
        }
    }
}

impl std::fmt::Display for DataCategory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

/* ----------------------------- Parsed Types ----------------------------- */

/// Parsed component entry.
#[derive(Debug, Clone)]
pub struct Component {
    /// Full UID: (componentId << 8) | instanceIndex.
    pub full_uid: u32,
    /// Component ID (extracted from fullUid).
    pub component_id: u16,
    /// Instance index (extracted from fullUid).
    pub instance_index: u8,
    /// Component name.
    pub name: String,
    /// Component type classification.
    pub component_type: ComponentType,
    /// Index of first task in task table.
    pub task_start: u16,
    /// Number of tasks for this component.
    pub task_count: u16,
    /// Index of first data entry in data table.
    pub data_start: u16,
    /// Number of data entries for this component.
    pub data_count: u16,
}

impl Component {
    /// Format fullUid as hex string.
    pub fn full_uid_hex(&self) -> String {
        format!("0x{:06X}", self.full_uid)
    }
}

/// Parsed task entry.
#[derive(Debug, Clone)]
pub struct Task {
    /// Owning component's fullUid.
    pub full_uid: u32,
    /// Task UID within component.
    pub task_uid: u8,
    /// Task name.
    pub name: String,
}

/// Parsed data entry.
#[derive(Debug, Clone)]
pub struct DataEntry {
    /// Owning component's fullUid.
    pub full_uid: u32,
    /// Data category.
    pub category: DataCategory,
    /// Data name.
    pub name: String,
    /// Size in bytes.
    pub size: u32,
}

/* ----------------------------- RDAT File ----------------------------- */

/// Parsed RDAT file.
#[derive(Debug)]
pub struct RdatFile {
    /// Format version.
    pub version: u16,
    /// Format flags.
    pub flags: u16,
    /// All components.
    components: Vec<Component>,
    /// All tasks.
    tasks: Vec<Task>,
    /// All data entries.
    data_entries: Vec<DataEntry>,
    /// Component lookup by fullUid.
    component_map: HashMap<u32, usize>,
}

/// Error type for RDAT parsing.
#[derive(Debug)]
pub enum RdatError {
    /// I/O error.
    Io(io::Error),
    /// Invalid magic bytes.
    InvalidMagic([u8; 4]),
    /// Unsupported version.
    UnsupportedVersion(u16),
    /// File too small.
    FileTooSmall { expected: usize, actual: usize },
    /// Invalid string offset.
    InvalidStringOffset(u32),
    /// String not null-terminated.
    UnterminatedString,
}

impl std::fmt::Display for RdatError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RdatError::Io(e) => write!(f, "I/O error: {}", e),
            RdatError::InvalidMagic(m) => write!(f, "Invalid magic: {:?}", m),
            RdatError::UnsupportedVersion(v) => write!(f, "Unsupported version: {}", v),
            RdatError::FileTooSmall { expected, actual } => {
                write!(
                    f,
                    "File too small: expected {} bytes, got {}",
                    expected, actual
                )
            }
            RdatError::InvalidStringOffset(o) => write!(f, "Invalid string offset: {}", o),
            RdatError::UnterminatedString => write!(f, "Unterminated string"),
        }
    }
}

impl std::error::Error for RdatError {}

impl From<io::Error> for RdatError {
    fn from(e: io::Error) -> Self {
        RdatError::Io(e)
    }
}

impl RdatFile {
    /// Parse RDAT file from path.
    pub fn from_file<P: AsRef<Path>>(path: P) -> Result<Self, RdatError> {
        let mut file = File::open(path)?;
        let mut data = Vec::new();
        file.read_to_end(&mut data)?;
        Self::from_bytes(&data)
    }

    /// Parse RDAT file from bytes.
    pub fn from_bytes(data: &[u8]) -> Result<Self, RdatError> {
        if data.len() < HEADER_SIZE {
            return Err(RdatError::FileTooSmall {
                expected: HEADER_SIZE,
                actual: data.len(),
            });
        }

        // Parse header
        let magic: [u8; 4] = data[0..4].try_into().unwrap();
        if magic != RDAT_MAGIC {
            return Err(RdatError::InvalidMagic(magic));
        }

        let version = u16::from_le_bytes([data[4], data[5]]);
        let flags = u16::from_le_bytes([data[6], data[7]]);
        let component_count = u16::from_le_bytes([data[8], data[9]]) as usize;
        let task_count = u16::from_le_bytes([data[10], data[11]]) as usize;
        let data_count = u16::from_le_bytes([data[12], data[13]]) as usize;

        // Validate version (v1 and v2 both use 24-byte component entries)
        if version != RDAT_VERSION_1 && version != RDAT_VERSION_2 {
            return Err(RdatError::UnsupportedVersion(version));
        }

        // Calculate expected file size and string table offset
        let component_table_size = component_count * COMPONENT_ENTRY_SIZE;
        let task_table_size = task_count * TASK_ENTRY_SIZE;
        let data_table_size = data_count * DATA_ENTRY_SIZE;
        let string_table_offset =
            HEADER_SIZE + component_table_size + task_table_size + data_table_size;

        if data.len() < string_table_offset {
            return Err(RdatError::FileTooSmall {
                expected: string_table_offset,
                actual: data.len(),
            });
        }

        let string_table = &data[string_table_offset..];

        // Parse components
        let mut components = Vec::with_capacity(component_count);
        let mut component_map = HashMap::with_capacity(component_count);
        let mut offset = HEADER_SIZE;

        for i in 0..component_count {
            let full_uid = u32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]);
            let name_offset = u32::from_le_bytes([
                data[offset + 4],
                data[offset + 5],
                data[offset + 6],
                data[offset + 7],
            ]);
            let task_start = u16::from_le_bytes([data[offset + 8], data[offset + 9]]);
            let task_count = u16::from_le_bytes([data[offset + 10], data[offset + 11]]);
            let data_start = u16::from_le_bytes([data[offset + 12], data[offset + 13]]);
            let data_count = u16::from_le_bytes([data[offset + 14], data[offset + 15]]);

            // ComponentType is only present in version 2
            let component_type = if version >= RDAT_VERSION_2 {
                ComponentType::from_u8(data[offset + 16])
            } else {
                // Version 1: infer from componentId range
                let component_id = ((full_uid >> 8) & 0xFFFF) as u16;
                if component_id == 0 {
                    ComponentType::Executive
                } else if component_id <= 100 {
                    ComponentType::Core
                } else {
                    ComponentType::SwModel // Assume SW_MODEL for models
                }
            };

            let name = read_string(string_table, name_offset)?;

            let component_id = ((full_uid >> 8) & 0xFFFF) as u16;
            let instance_index = (full_uid & 0xFF) as u8;

            component_map.insert(full_uid, i);
            components.push(Component {
                full_uid,
                component_id,
                instance_index,
                name,
                component_type,
                task_start,
                task_count,
                data_start,
                data_count,
            });

            offset += COMPONENT_ENTRY_SIZE;
        }

        // Parse tasks
        let mut tasks = Vec::with_capacity(task_count);
        for _ in 0..task_count {
            let full_uid = u32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]);
            let task_uid = data[offset + 4];
            let name_offset = u32::from_le_bytes([
                data[offset + 8],
                data[offset + 9],
                data[offset + 10],
                data[offset + 11],
            ]);

            let name = read_string(string_table, name_offset)?;

            tasks.push(Task {
                full_uid,
                task_uid,
                name,
            });

            offset += TASK_ENTRY_SIZE;
        }

        // Parse data entries
        let mut data_entries = Vec::with_capacity(data_count);
        for _ in 0..data_count {
            let full_uid = u32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]);
            let category = DataCategory::from_u8(data[offset + 4]);
            let name_offset = u32::from_le_bytes([
                data[offset + 8],
                data[offset + 9],
                data[offset + 10],
                data[offset + 11],
            ]);
            let size = u32::from_le_bytes([
                data[offset + 12],
                data[offset + 13],
                data[offset + 14],
                data[offset + 15],
            ]);

            let name = read_string(string_table, name_offset)?;

            data_entries.push(DataEntry {
                full_uid,
                category,
                name,
                size,
            });

            offset += DATA_ENTRY_SIZE;
        }

        Ok(RdatFile {
            version,
            flags,
            components,
            tasks,
            data_entries,
            component_map,
        })
    }

    /// Get all components.
    pub fn components(&self) -> &[Component] {
        &self.components
    }

    /// Get all tasks.
    pub fn tasks(&self) -> &[Task] {
        &self.tasks
    }

    /// Get all data entries.
    pub fn data_entries(&self) -> &[DataEntry] {
        &self.data_entries
    }

    /// Get component by fullUid.
    pub fn get_component(&self, full_uid: u32) -> Option<&Component> {
        self.component_map
            .get(&full_uid)
            .map(|&i| &self.components[i])
    }

    /// Get tasks for a component.
    pub fn get_tasks_for_component(&self, comp: &Component) -> &[Task] {
        let start = comp.task_start as usize;
        let end = start + comp.task_count as usize;
        if end <= self.tasks.len() {
            &self.tasks[start..end]
        } else {
            &[]
        }
    }

    /// Get data entries for a component.
    pub fn get_data_for_component(&self, comp: &Component) -> &[DataEntry] {
        let start = comp.data_start as usize;
        let end = start + comp.data_count as usize;
        if end <= self.data_entries.len() {
            &self.data_entries[start..end]
        } else {
            &[]
        }
    }

    /// Total number of components.
    pub fn component_count(&self) -> usize {
        self.components.len()
    }

    /// Total number of tasks.
    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }

    /// Total number of data entries.
    pub fn data_count(&self) -> usize {
        self.data_entries.len()
    }

    /// Total data size across all entries.
    pub fn total_data_size(&self) -> u64 {
        self.data_entries.iter().map(|d| d.size as u64).sum()
    }

    /// Get components by type.
    pub fn components_by_type(&self, ct: ComponentType) -> Vec<&Component> {
        self.components
            .iter()
            .filter(|c| c.component_type == ct)
            .collect()
    }
}

/* ----------------------------- Helpers ----------------------------- */

/// Read null-terminated string from string table.
fn read_string(string_table: &[u8], offset: u32) -> Result<String, RdatError> {
    let offset = offset as usize;
    if offset >= string_table.len() {
        return Err(RdatError::InvalidStringOffset(offset as u32));
    }

    let slice = &string_table[offset..];
    let end = slice
        .iter()
        .position(|&b| b == 0)
        .ok_or(RdatError::UnterminatedString)?;

    String::from_utf8_lossy(&slice[..end]).into_owned().pipe(Ok)
}

/// Extension trait for pipe operator.
trait Pipe: Sized {
    fn pipe<T>(self, f: impl FnOnce(Self) -> T) -> T {
        f(self)
    }
}

impl<T> Pipe for T {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_component_type_from_u8() {
        assert_eq!(ComponentType::from_u8(0), ComponentType::Executive);
        assert_eq!(ComponentType::from_u8(1), ComponentType::Core);
        assert_eq!(ComponentType::from_u8(2), ComponentType::SwModel);
        assert_eq!(ComponentType::from_u8(3), ComponentType::HwModel);
        assert_eq!(ComponentType::from_u8(4), ComponentType::Support);
        assert_eq!(ComponentType::from_u8(5), ComponentType::Driver);
        assert_eq!(ComponentType::from_u8(99), ComponentType::Unknown);
    }

    #[test]
    fn test_component_type_is_model() {
        assert!(!ComponentType::Executive.is_model());
        assert!(!ComponentType::Core.is_model());
        assert!(ComponentType::SwModel.is_model());
        assert!(ComponentType::HwModel.is_model());
        assert!(!ComponentType::Support.is_model());
        assert!(!ComponentType::Driver.is_model());
    }

    #[test]
    fn test_component_type_is_schedulable() {
        assert!(!ComponentType::Executive.is_schedulable());
        assert!(!ComponentType::Core.is_schedulable());
        assert!(ComponentType::SwModel.is_schedulable());
        assert!(ComponentType::HwModel.is_schedulable());
        assert!(ComponentType::Support.is_schedulable());
        assert!(ComponentType::Driver.is_schedulable());
    }

    #[test]
    fn test_data_category_from_u8() {
        // Matches C++ enum: STATIC_PARAM=0, TUNABLE_PARAM=1, STATE=2, INPUT=3, OUTPUT=4
        assert_eq!(DataCategory::from_u8(0), DataCategory::StaticParam);
        assert_eq!(DataCategory::from_u8(1), DataCategory::TunableParam);
        assert_eq!(DataCategory::from_u8(2), DataCategory::State);
        assert_eq!(DataCategory::from_u8(3), DataCategory::Input);
        assert_eq!(DataCategory::from_u8(4), DataCategory::Output);
        assert_eq!(DataCategory::from_u8(99), DataCategory::Unknown);
    }
}
