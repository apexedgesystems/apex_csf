//! apex_data.toml manifest parsing.
//!
//! Manifest format:
//! ```toml
//! component = "PolynomialModel"
//!
//! [structs]
//! PolynomialTunableParams = { category = "TUNABLE_PARAM" }
//! PolynomialState = { category = "STATE" }
//! PolynomialSetCoeffsCmd = { category = "COMMAND", opcode = "0x0001" }
//! PolynomialStatusTlm = { category = "TELEMETRY", opcode = "0x0001" }
//!
//! [enums]
//! RTMode = {}
//! ComponentType = { header = "inc/ComponentType.hpp" }
//! ```

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use serde::Deserialize;

use super::Error;

/* ----------------------------- Manifest Types ----------------------------- */

/// Data category for struct classification.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DataCategory {
    /// Tunable parameters (TPRM).
    TunableParam,
    /// Runtime state.
    State,
    /// Command payload.
    Command,
    /// Telemetry payload.
    Telemetry,
    /// Wire protocol structures (packet framing).
    Protocol,
}

impl std::fmt::Display for DataCategory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DataCategory::TunableParam => write!(f, "TUNABLE_PARAM"),
            DataCategory::State => write!(f, "STATE"),
            DataCategory::Command => write!(f, "COMMAND"),
            DataCategory::Telemetry => write!(f, "TELEMETRY"),
            DataCategory::Protocol => write!(f, "PROTOCOL"),
        }
    }
}

/// Struct entry in the manifest.
#[derive(Debug, Clone, Deserialize)]
pub struct StructEntry {
    /// Data category.
    pub category: DataCategory,
    /// Opcode for COMMAND/TELEMETRY (hex string like "0x0001").
    pub opcode: Option<String>,
    /// Optional explicit header path (relative to manifest).
    pub header: Option<String>,
}

/// Enum entry in the manifest.
#[derive(Debug, Clone, Default, Deserialize)]
pub struct EnumEntry {
    /// Optional explicit header path (relative to manifest).
    pub header: Option<String>,
}

/// Parsed apex_data.toml manifest.
#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    /// Component name.
    pub component: String,
    /// Struct entries keyed by struct name.
    pub structs: BTreeMap<String, StructEntry>,
    /// Enum entries keyed by enum name (optional section).
    #[serde(default)]
    pub enums: BTreeMap<String, EnumEntry>,
}

/* ----------------------------- Public API --------------------------------- */

/// Parse a manifest file.
pub fn parse_manifest(path: &Path) -> Result<Manifest, Error> {
    let content = fs::read_to_string(path)?;
    parse_manifest_str(&content)
}

/// Parse manifest from string content.
pub fn parse_manifest_str(content: &str) -> Result<Manifest, Error> {
    toml::from_str(content).map_err(|e| Error::Parse(format!("manifest parse error: {}", e)))
}

/* --------------------------------- Tests ---------------------------------- */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_basic_manifest() {
        let content = r#"
            component = "PolynomialModel"

            [structs]
            PolynomialTunableParams = { category = "TUNABLE_PARAM" }
            PolynomialState = { category = "STATE" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        assert_eq!(manifest.component, "PolynomialModel");
        assert_eq!(manifest.structs.len(), 2);

        let tp = &manifest.structs["PolynomialTunableParams"];
        assert_eq!(tp.category, DataCategory::TunableParam);
        assert!(tp.opcode.is_none());

        let state = &manifest.structs["PolynomialState"];
        assert_eq!(state.category, DataCategory::State);
    }

    #[test]
    fn parses_command_telemetry_with_opcode() {
        let content = r#"
            component = "TestModel"

            [structs]
            SetValueCmd = { category = "COMMAND", opcode = "0x0001" }
            StatusTlm = { category = "TELEMETRY", opcode = "0x0002" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        assert_eq!(manifest.component, "TestModel");

        let cmd = &manifest.structs["SetValueCmd"];
        assert_eq!(cmd.category, DataCategory::Command);
        assert_eq!(cmd.opcode.as_deref(), Some("0x0001"));

        let tlm = &manifest.structs["StatusTlm"];
        assert_eq!(tlm.category, DataCategory::Telemetry);
        assert_eq!(tlm.opcode.as_deref(), Some("0x0002"));
    }

    #[test]
    fn parses_explicit_header_path() {
        let content = r#"
            component = "CustomModel"

            [structs]
            CustomParams = { category = "TUNABLE_PARAM", header = "inc/CustomData.hpp" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        let params = &manifest.structs["CustomParams"];
        assert_eq!(params.header.as_deref(), Some("inc/CustomData.hpp"));
    }

    #[test]
    fn rejects_invalid_category() {
        let content = r#"
            component = "BadModel"

            [structs]
            BadStruct = { category = "INVALID" }
        "#;

        let result = parse_manifest_str(content);
        assert!(result.is_err());
    }

    #[test]
    fn parses_protocol_category() {
        let content = r#"
            component = "AprotoProtocol"

            [structs]
            AprotoHeader = { category = "PROTOCOL" }
            AprotoFlags = { category = "PROTOCOL" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        assert_eq!(manifest.component, "AprotoProtocol");

        let header = &manifest.structs["AprotoHeader"];
        assert_eq!(header.category, DataCategory::Protocol);

        let flags = &manifest.structs["AprotoFlags"];
        assert_eq!(flags.category, DataCategory::Protocol);
    }

    #[test]
    fn parses_enums_section() {
        let content = r#"
            component = "SystemComponent"

            [structs]
            RTConfig = { category = "TUNABLE_PARAM" }

            [enums]
            RTMode = {}
            ComponentType = { header = "inc/ComponentType.hpp" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        assert_eq!(manifest.component, "SystemComponent");
        assert_eq!(manifest.structs.len(), 1);
        assert_eq!(manifest.enums.len(), 2);

        let rtmode = &manifest.enums["RTMode"];
        assert!(rtmode.header.is_none());

        let ctype = &manifest.enums["ComponentType"];
        assert_eq!(ctype.header.as_deref(), Some("inc/ComponentType.hpp"));
    }

    #[test]
    fn enums_section_is_optional() {
        let content = r#"
            component = "NoEnums"

            [structs]
            SomeStruct = { category = "STATE" }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        assert_eq!(manifest.component, "NoEnums");
        assert!(manifest.enums.is_empty());
    }
}
