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
///
/// Matches the C++ DataCategory enum plus manifest-specific categories
/// (COMMAND, TELEMETRY, PROTOCOL) for wire format structs.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DataCategory {
    /// Static parameters (read-only after init).
    StaticParam,
    /// Tunable parameters (TPRM-configurable at runtime).
    TunableParam,
    /// Runtime state.
    State,
    /// External input data.
    Input,
    /// Published output data.
    Output,
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
            DataCategory::StaticParam => write!(f, "STATIC_PARAM"),
            DataCategory::TunableParam => write!(f, "TUNABLE_PARAM"),
            DataCategory::State => write!(f, "STATE"),
            DataCategory::Input => write!(f, "INPUT"),
            DataCategory::Output => write!(f, "OUTPUT"),
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

/// Per-field constraint declaration: the component-level single source
/// the three validation rails consume (cfg2bin refusal, ground UI
/// bounds, on-board load checks). All keys optional; `min`/`max` are
/// inclusive, `allowed` is an explicit legal-value list, `step` is
/// granularity from `min`.
#[derive(Debug, Clone, Default, Deserialize, PartialEq)]
pub struct FieldConstraints {
    pub min: Option<f64>,
    pub max: Option<f64>,
    pub allowed: Option<Vec<f64>>,
    pub step: Option<f64>,
}

/// One field of a spec-defined struct: the ordered building block of
/// the generated header. Array order in the manifest is layout order.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct FieldDef {
    /// Field name (the C++ member identifier).
    pub name: String,
    /// Logical type: int | uint | float | bool | char | string.
    pub r#type: String,
    /// Size in bytes (per element when `count` is set).
    pub size: u32,
    /// Array length: emits `type name[count]` (absent = scalar).
    pub count: Option<u32>,
    /// Default value (becomes the member initializer and the template
    /// default).
    pub default: Option<toml::Value>,
    /// One-line field documentation (becomes the member's doc comment).
    pub doc: Option<String>,
}

/// One command in the component's spec: opcode, payload structs
/// (whose layouts are ordinary `[[fields.<Struct>]]` specs), and the
/// doc line the generated dispatch and dictionaries carry.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct CommandDef {
    /// Handler name (the generated hook is `on<Name>`).
    pub name: String,
    /// Command opcode (hex string, e.g. "0x0200").
    pub opcode: String,
    /// Request payload struct (spec-defined); absent = no payload.
    pub request: Option<String>,
    /// Response payload struct (spec-defined); absent = status-only.
    pub response: Option<String>,
    /// One-line command documentation.
    pub doc: Option<String>,
}

/// One telemetry stream in the component's spec.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct TelemetryDef {
    /// Stream name.
    pub name: String,
    /// Telemetry opcode (hex string).
    pub opcode: String,
    /// Payload struct (spec-defined).
    pub r#struct: String,
    /// One-line emission documentation.
    pub doc: Option<String>,
}

/// One schedulable task in the component's spec: identity only --
/// name (which is also the derived class's method name), the task
/// uid, and the doc line. Timing (frequency, priority, offset) is
/// deliberately NOT spec data: it stays in the scheduler TOML so the
/// vehicle can be retimed without regeneration.
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct TaskDef {
    /// Task name; the generated doInit binds the derived method of
    /// the same name (e.g. "step" -> &TDerived::step).
    pub name: String,
    /// Task uid within the component (scheduler TOML addresses it).
    pub uid: u8,
    /// One-line task documentation.
    pub doc: Option<String>,
}

/// Parsed apex_data.toml manifest.
#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    /// Component name.
    pub component: String,
    /// C++ namespace generated headers open (e.g. "appsim::wave").
    #[serde(default)]
    pub namespace: Option<String>,
    /// Component id (identity constant in generated stubs).
    #[serde(default)]
    pub component_id: Option<u16>,
    /// Tier base for generated stubs: SW_MODEL | SUPPORT | DRIVER.
    #[serde(default)]
    pub component_type: Option<String>,
    /// Short log label for generated stubs (e.g. "SPEC_SNS").
    #[serde(default)]
    pub label: Option<String>,
    /// Proto-authored spec source: a profile .proto (relative to the
    /// manifest) whose messages populate `fields`. One definition
    /// source per component -- mutually exclusive with `[[fields]]`.
    #[serde(default)]
    pub proto_spec: Option<String>,
    /// Struct entries keyed by struct name.
    pub structs: BTreeMap<String, StructEntry>,
    /// Enum entries keyed by enum name (optional section).
    #[serde(default)]
    pub enums: BTreeMap<String, EnumEntry>,
    /// Constraint declarations: struct name -> field name -> legal
    /// range (optional section, `[constraints.<StructName>]`).
    #[serde(default)]
    pub constraints: BTreeMap<String, BTreeMap<String, FieldConstraints>>,
    /// Spec-defined struct layouts: struct name -> ordered field list
    /// (optional section, `[[fields.<StructName>]]` array-of-tables).
    /// Presence makes the spec the source of truth for that struct:
    /// the header generates from it (cdef phase 1).
    #[serde(default)]
    pub fields: BTreeMap<String, Vec<FieldDef>>,
    /// Spec-defined command set (`[[commands]]`): the generated
    /// dispatch and the dictionaries derive from it (cdef phase 2).
    #[serde(default)]
    pub commands: Vec<CommandDef>,
    /// Spec-defined telemetry streams (`[[telemetry]]`).
    #[serde(default)]
    pub telemetry: Vec<TelemetryDef>,
    /// Spec-defined schedulable tasks (`[[tasks]]`): the generated
    /// base registers them in its doInit.
    #[serde(default)]
    pub tasks: Vec<TaskDef>,
    /// Declared target capabilities (`capabilities = [...]`), exported
    /// verbatim into the dictionary so ground feature-detects from an
    /// explicit flag, never from version strings. The executive's
    /// manifest is the per-target surface (one executive per target).
    #[serde(default)]
    pub capabilities: Vec<String>,
}

/* ----------------------------- Public API --------------------------------- */

/// Parse a manifest file.
pub fn parse_manifest(path: &Path) -> Result<Manifest, Error> {
    let content = fs::read_to_string(path)?;
    let mut manifest = parse_toml(&content)?;
    resolve_proto_spec(&mut manifest, path)?;
    Ok(manifest)
}

/// Raw TOML deserialization shared by both entry points.
fn parse_toml(content: &str) -> Result<Manifest, Error> {
    toml::from_str(content).map_err(|e| Error::Parse(format!("manifest parse error: {}", e)))
}

/// Populate `fields` from a referenced profile .proto. One definition
/// source per component: `proto_spec` beside any `[[fields]]` array is
/// an error, never a merge. Every message must name a registered
/// struct (typo guard); the file's package, when present, must match
/// the manifest namespace.
fn resolve_proto_spec(manifest: &mut Manifest, manifest_path: &Path) -> Result<(), Error> {
    let Some(proto_rel) = &manifest.proto_spec else {
        return Ok(());
    };
    if !manifest.fields.is_empty() {
        return Err(Error::Parse(format!(
            "component '{}' declares both proto_spec and [[fields]] -- one definition \
             source per component",
            manifest.component
        )));
    }
    let proto_path = manifest_path
        .parent()
        .unwrap_or_else(|| Path::new("."))
        .join(proto_rel);
    let source = fs::read_to_string(&proto_path)
        .map_err(|e| Error::Parse(format!("proto_spec '{}': {e}", proto_path.display())))?;

    if let (Some(package), Some(namespace)) =
        (super::proto::package_of(&source), &manifest.namespace)
    {
        let expected = namespace.replace("::", ".");
        if package != expected {
            return Err(Error::Parse(format!(
                "proto_spec package '{package}' does not match namespace '{namespace}'"
            )));
        }
    }

    let messages = super::proto::ingest(&source)?;
    for name in messages.keys() {
        if !manifest.structs.contains_key(name) {
            return Err(Error::Parse(format!(
                "proto_spec message '{name}' is not a registered struct"
            )));
        }
    }
    manifest.fields = messages;
    Ok(())
}

/// Parse manifest from string content. A `proto_spec` reference needs
/// a manifest location to resolve against, so it is rejected here --
/// use `parse_manifest` for file-based manifests.
pub fn parse_manifest_str(content: &str) -> Result<Manifest, Error> {
    let manifest = parse_toml(content)?;
    if manifest.proto_spec.is_some() {
        return Err(Error::Parse(
            "proto_spec requires a file-based manifest (nothing to resolve the path against)"
                .into(),
        ));
    }
    Ok(manifest)
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
        assert!(manifest.constraints.is_empty());

        let tp = &manifest.structs["PolynomialTunableParams"];
        assert_eq!(tp.category, DataCategory::TunableParam);
        assert!(tp.opcode.is_none());

        let state = &manifest.structs["PolynomialState"];
        assert_eq!(state.category, DataCategory::State);
    }

    #[test]
    fn parses_field_definitions_in_order() {
        let content = r#"
            component = "WaveGenerator"

            [structs]
            WaveGenTunableParams = { category = "TUNABLE_PARAM" }

            [[fields.WaveGenTunableParams]]
            name = "frequency"
            type = "float"
            size = 4
            default = 1.0
            doc = "Primary frequency [Hz]"

            [[fields.WaveGenTunableParams]]
            name = "reserved"
            type = "uint"
            size = 1
            count = 3
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        let f = &manifest.fields["WaveGenTunableParams"];
        assert_eq!(f.len(), 2);
        assert_eq!(f[0].name, "frequency");
        assert_eq!(f[0].r#type, "float");
        assert_eq!(f[0].doc.as_deref(), Some("Primary frequency [Hz]"));
        assert_eq!(f[1].count, Some(3));
    }

    #[test]
    fn parses_constraint_declarations() {
        let content = r#"
            component = "WaveGenerator"

            [structs]
            WaveGenTunableParams = { category = "TUNABLE_PARAM" }

            [constraints.WaveGenTunableParams]
            frequency = { min = 0.0, max = 50.0 }
            waveType = { allowed = [0, 1, 2, 3, 4] }
        "#;

        let manifest = parse_manifest_str(content).unwrap();
        let c = &manifest.constraints["WaveGenTunableParams"];
        assert_eq!(c["frequency"].min, Some(0.0));
        assert_eq!(c["frequency"].max, Some(50.0));
        assert_eq!(c["waveType"].allowed.as_ref().unwrap().len(), 5);
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

#[cfg(test)]
mod capability_tests {
    use super::*;

    #[test]
    fn capabilities_parse_and_default_empty() {
        let m = parse_manifest_str("component = \"X\"\ncapabilities = [\"readback\"]\n[structs]\n")
            .unwrap();
        assert_eq!(m.capabilities, vec!["readback".to_string()]);

        let none = parse_manifest_str("component = \"Y\"\n[structs]\n").unwrap();
        assert!(none.capabilities.is_empty());
    }
}
