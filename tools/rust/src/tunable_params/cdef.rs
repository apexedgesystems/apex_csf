//! cdef phase 1: generate the TParams header from the component spec.
//!
//! When a struct carries an ordered `[[fields.<Struct>]]` definition in
//! its apex_data.toml, the spec is the source of truth and the C++
//! header derives from it: a generated header cannot drift from the
//! spec, `static_assert(sizeof)` pins the size, and the emitted
//! layout-hash constant is computed with the same canonical field-spec
//! CRC the payload stamper uses -- so the on-board expectation and the
//! stamped prelude agree by construction.
//!
//! Generated files live in the component's `.auto/` directory and are
//! committed; check-cdef regenerates and diffs so hand edits cannot
//! merge. Overrides belong in `.ovr/`, never inside generated files.

use super::manifest::{FieldDef, Manifest};
use super::Error;

/// C++ type for a logical field type/size pair.
fn cpp_type(field_type: &str, size: u32) -> Result<&'static str, Error> {
    Ok(match (field_type, size) {
        ("int", 1) => "std::int8_t",
        ("int", 2) => "std::int16_t",
        ("int", 4) => "std::int32_t",
        ("int", 8) => "std::int64_t",
        ("uint", 1) => "std::uint8_t",
        ("uint", 2) => "std::uint16_t",
        ("uint", 4) => "std::uint32_t",
        ("uint", 8) => "std::uint64_t",
        ("float", 4) => "float",
        ("float", 8) | ("double", 8) => "double",
        ("bool", 1) => "bool",
        ("char", 1) => "char",
        _ => {
            return Err(Error::Emit(format!(
                "unsupported field type/size: {field_type}/{size}"
            )))
        }
    })
}

/// Member initializer text for a field's default.
fn initializer(field: &FieldDef) -> String {
    match &field.default {
        None => "{}".to_string(),
        Some(v) => {
            let lit = match v {
                toml::Value::Float(f) => {
                    if field.r#type == "float" && field.size == 4 {
                        format!("{f:?}F")
                    } else {
                        format!("{f:?}")
                    }
                }
                toml::Value::Integer(i) => format!("{i}"),
                toml::Value::Boolean(b) => format!("{b}"),
                toml::Value::String(s) => format!("'{s}'"),
                other => format!("{other}"),
            };
            format!("{{{lit}}}")
        }
    }
}

/// The canonical field-spec string for the layout hash: identical to
/// the serializer's emission-order walk (`name:type:size;` per leaf,
/// array element shape appended) over the template this spec
/// generates.
pub fn canonical_spec(fields: &[FieldDef]) -> String {
    let mut spec = String::new();
    for f in fields {
        match f.count {
            None => spec.push_str(&format!("{}:{}:{};", f.name, f.r#type, f.size)),
            Some(n) => {
                let total = f.size * n;
                spec.push_str(&format!("{}:array:{};", f.name, total));
                spec.push_str(&format!("[{}:{}x{}]", f.r#type, f.size, n));
            }
        }
    }
    spec
}

/// Total serialized size of the spec's layout in bytes.
pub fn layout_size(fields: &[FieldDef]) -> u32 {
    fields.iter().map(|f| f.size * f.count.unwrap_or(1)).sum()
}

/// Generate the `.auto` header for one spec-defined struct.
pub fn generate_header(manifest: &Manifest, struct_name: &str) -> Result<String, Error> {
    let fields = manifest
        .fields
        .get(struct_name)
        .ok_or_else(|| Error::Parse(format!("no [[fields.{struct_name}]] spec in the manifest")))?;
    if fields.is_empty() {
        return Err(Error::Parse(format!("[[fields.{struct_name}]] is empty")));
    }

    let hash = super::payload::crc32(canonical_spec(fields).as_bytes());
    let size = layout_size(fields);

    let mut shout = String::new();
    for c in struct_name.chars() {
        if c.is_uppercase() && !shout.is_empty() && !shout.ends_with('_') {
            shout.push('_');
        }
        shout.push(c.to_ascii_uppercase());
    }

    let mut body = String::new();
    for f in fields {
        // Fixed text: size is the byte capacity of a null-padded char
        // buffer; a count makes it a fixed array of such buffers.
        let decl = if f.r#type == "string" {
            match f.count {
                None => format!("char {}[{}]{{}}", f.name, f.size),
                Some(n) => format!("char {}[{n}][{}]{{}}", f.name, f.size),
            }
        } else {
            let ty = cpp_type(&f.r#type, f.size)?;
            match f.count {
                None => format!("{ty} {}{}", f.name, initializer(f)),
                Some(n) => format!("{ty} {}[{n}]{{}}", f.name),
            }
        };
        match &f.doc {
            Some(doc) => body.push_str(&format!("  {decl}; ///< {doc}\n")),
            None => body.push_str(&format!("  {decl};\n")),
        }
    }

    let (ns_open, ns_close) = match &manifest.namespace {
        Some(ns) => {
            let parts: Vec<&str> = ns.split("::").collect();
            (
                parts
                    .iter()
                    .map(|p| format!("namespace {p} {{\n"))
                    .collect::<String>(),
                parts
                    .iter()
                    .rev()
                    .map(|p| format!("}} // namespace {p}\n"))
                    .collect::<String>(),
            )
        }
        None => (String::new(), String::new()),
    };

    Ok(format!(
        "// Generated by cdef_gen from the {component} spec -- DO NOT EDIT.\n\
         // Regenerate: make cdef (check-cdef diffs this file against the spec).\n\
         // Overrides belong in the component's .ovr/ directory.\n\
         #ifndef APEX_CDEF_AUTO_{shout}_HPP\n\
         #define APEX_CDEF_AUTO_{shout}_HPP\n\
         \n\
         #include <cstdint>\n\
         \n\
         {ns_open}\n\
         /// Spec-defined tunable parameters ({size} bytes, packed by\n\
         /// construction: field order and sizes come from the spec).\n\
         struct {struct_name} {{\n\
         {body}}};\n\
         static_assert(sizeof({struct_name}) == {size}, \"layout diverged from the spec\");\n\
         \n\
         /// Layout hash the v3 payload prelude must carry for this struct\n\
         /// (canonical field-spec CRC-32; stamped by cfg2bin from the same\n\
         /// spec-generated template).\n\
         inline constexpr std::uint32_t {shout}_LAYOUT_HASH = 0x{hash:08X}U;\n\
         \n\
         {ns_close}\
         #endif // APEX_CDEF_AUTO_{shout}_HPP\n",
        component = manifest.component,
    ))
}

/// Generate the `.auto` command-dispatch base for the component: a
/// tier-agnostic mixin whose handleCommand verifies each spec-declared
/// command's payload size, decodes the request struct, invokes the
/// pure-virtual hook, and encodes the response. Malformed payloads and
/// unknown opcodes never reach user logic (unknowns fall through to
/// the tier base).
pub fn generate_cmd_base(manifest: &Manifest) -> Result<String, Error> {
    if manifest.commands.is_empty() {
        return Err(Error::Parse("no [[commands]] in the manifest".to_string()));
    }
    for c in &manifest.commands {
        for s in [&c.request, &c.response].into_iter().flatten() {
            if !manifest.fields.contains_key(s) {
                return Err(Error::Parse(format!(
                    "command {}: payload struct '{s}' has no [[fields.{s}]] spec",
                    c.name
                )));
            }
        }
    }

    let component = &manifest.component;
    let class = format!("{component}CmdBase");
    let mut shout = String::new();
    for ch in component.chars() {
        if ch.is_uppercase() && !shout.is_empty() && !shout.ends_with('_') {
            shout.push('_');
        }
        shout.push(ch.to_ascii_uppercase());
    }

    let mut includes = String::new();
    let mut seen: Vec<&String> = Vec::new();
    for c in &manifest.commands {
        for s in [&c.request, &c.response].into_iter().flatten() {
            if !seen.contains(&s) {
                seen.push(s);
                includes.push_str(&format!("#include \"{s}_auto.hpp\"\n"));
            }
        }
    }

    let mut hooks = String::new();
    let mut cases = String::new();
    for c in &manifest.commands {
        let doc = c.doc.as_deref().unwrap_or("Spec-declared command.");
        let hook = format!("on{}", c.name);
        let opcode = &c.opcode;
        match (&c.request, &c.response) {
            (Some(req), Some(resp)) => {
                hooks.push_str(&format!(
                    "  /// {doc}\n  [[nodiscard]] virtual std::uint8_t {hook}(const {req}& request, {resp}& response) noexcept = 0;\n"
                ));
                cases.push_str(&format!(
                    "    case {opcode}U: {{\n      if (payload.size() != sizeof({req})) {{\n        return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);\n      }}\n      {req} request{{}};\n      std::memcpy(&request, payload.data(), sizeof(request));\n      {resp} reply{{}};\n      const std::uint8_t RC = {hook}(request, reply);\n      response.resize(sizeof(reply));\n      std::memcpy(response.data(), &reply, sizeof(reply));\n      return RC;\n    }}\n"
                ));
            }
            (Some(req), None) => {
                hooks.push_str(&format!(
                    "  /// {doc}\n  [[nodiscard]] virtual std::uint8_t {hook}(const {req}& request) noexcept = 0;\n"
                ));
                cases.push_str(&format!(
                    "    case {opcode}U: {{\n      if (payload.size() != sizeof({req})) {{\n        return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);\n      }}\n      {req} request{{}};\n      std::memcpy(&request, payload.data(), sizeof(request));\n      return {hook}(request);\n    }}\n"
                ));
            }
            (None, Some(resp)) => {
                hooks.push_str(&format!(
                    "  /// {doc}\n  [[nodiscard]] virtual std::uint8_t {hook}({resp}& response) noexcept = 0;\n"
                ));
                cases.push_str(&format!(
                    "    case {opcode}U: {{\n      if (!payload.empty()) {{\n        return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);\n      }}\n      {resp} reply{{}};\n      const std::uint8_t RC = {hook}(reply);\n      response.resize(sizeof(reply));\n      std::memcpy(response.data(), &reply, sizeof(reply));\n      return RC;\n    }}\n"
                ));
            }
            (None, None) => {
                hooks.push_str(&format!(
                    "  /// {doc}\n  [[nodiscard]] virtual std::uint8_t {hook}() noexcept = 0;\n"
                ));
                cases.push_str(&format!(
                    "    case {opcode}U: {{\n      if (!payload.empty()) {{\n        return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);\n      }}\n      return {hook}();\n    }}\n"
                ));
            }
        }
    }

    let (ns_open, ns_close) = match &manifest.namespace {
        Some(ns) => {
            let parts: Vec<&str> = ns.split("::").collect();
            (
                parts
                    .iter()
                    .map(|p| format!("namespace {p} {{\n"))
                    .collect::<String>(),
                parts
                    .iter()
                    .rev()
                    .map(|p| format!("}} // namespace {p}\n"))
                    .collect::<String>(),
            )
        }
        None => (String::new(), String::new()),
    };

    Ok(format!(
        "// Generated by cdef_gen from the {component} spec -- DO NOT EDIT.\n\
         // Regenerate: make cdef (check-cdef diffs this file against the spec).\n\
         // Implement the on<Command> hooks in the component (stub-generated).\n\
         #ifndef APEX_CDEF_AUTO_{shout}_CMD_BASE_HPP\n\
         #define APEX_CDEF_AUTO_{shout}_CMD_BASE_HPP\n\
         \n\
         #include \"src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp\"\n\
         #include \"src/utilities/compatibility/inc/compat_span.hpp\"\n\
         {includes}\n\
         #include <cstdint>\n\
         #include <cstring>\n\
         #include <vector>\n\
         \n\
         {ns_open}\n\
         /// Spec-generated command dispatch: size-verified decode, hook\n\
         /// invocation, response encode. Unknown opcodes fall through to\n\
         /// the tier base.\n\
         template <typename TBase> class {class} : public TBase {{\n\
         public:\n\
           using TBase::TBase;\n\
         \n\
         protected:\n\
         {hooks}\n\
           [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,\n\
                                                    apex::compat::rospan<std::uint8_t> payload,\n\
                                                    std::vector<std::uint8_t>& response) noexcept override {{\n\
             switch (opcode) {{\n\
         {cases}    default:\n\
               return TBase::handleCommand(opcode, payload, response);\n\
             }}\n\
           }}\n\
         }};\n\
         \n\
         {ns_close}\
         #endif // APEX_CDEF_AUTO_{shout}_CMD_BASE_HPP\n"
    ))
}

/// Generate the once-only component stub: a compilable skeleton the
/// user owns after generation (never regenerated over). It inherits
/// the generated command base over the spec's tier base, carries the
/// spec identity, a ParamBank for the spec's TUNABLE_PARAM struct
/// (enforcing the generated layout hash), a registered step task, and
/// minimal honest hook bodies awaiting the component's real logic.
pub fn generate_stub(manifest: &Manifest) -> Result<String, Error> {
    let component = &manifest.component;
    let id = manifest.component_id.ok_or_else(|| {
        Error::Parse("stub generation needs component_id in the manifest".to_string())
    })?;
    let tier = manifest.component_type.as_deref().unwrap_or("SW_MODEL");
    let (base, base_include) = match tier {
        "SW_MODEL" => (
            "system_core::system_component::SwModelBase",
            "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp",
        ),
        "SUPPORT" => (
            "system_core::system_component::SupportComponentBase",
            "src/system/core/infrastructure/system_component/posix/inc/SupportComponentBase.hpp",
        ),
        "DRIVER" => (
            "system_core::system_component::DriverBase",
            "src/system/core/infrastructure/system_component/posix/inc/DriverBase.hpp",
        ),
        other => {
            return Err(Error::Parse(format!(
                "unsupported component_type '{other}' (SW_MODEL | SUPPORT | DRIVER)"
            )))
        }
    };
    let label = manifest.label.clone().unwrap_or_else(|| {
        let mut s = String::new();
        for c in component.chars() {
            if c.is_uppercase() && !s.is_empty() {
                s.push('_');
            }
            s.push(c.to_ascii_uppercase());
        }
        s
    });

    // The spec's tunable struct (first TUNABLE_PARAM with a field spec).
    let tunable = manifest
        .structs
        .iter()
        .find(|(name, e)| {
            matches!(e.category, super::manifest::DataCategory::TunableParam)
                && manifest.fields.contains_key(*name)
        })
        .map(|(name, _)| name.clone());

    let mut shout_struct = String::new();
    let mut bank_decl = String::new();
    let mut load_tprm = String::new();
    let mut tunable_include = String::new();
    if let Some(ts) = &tunable {
        tunable_include = format!("#include \"{ts}_auto.hpp\" // via the .auto include path\n");
        for c in ts.chars() {
            if c.is_uppercase() && !shout_struct.is_empty() && !shout_struct.ends_with('_') {
                shout_struct.push('_');
            }
            shout_struct.push(c.to_ascii_uppercase());
        }
        bank_decl = format!("  system_core::system_component::ParamBank<{ts}> paramBank_{{}};\n");
        load_tprm = format!(
            "  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {{\n    if (!isRegistered()) {{\n      return false;\n    }}\n    const std::filesystem::path PATH = tprmDir / tprmFilename(fullUid());\n    bool loaded = false;\n    if (std::filesystem::exists(PATH)) {{\n      loaded = paramBank_.load(PATH, fullUid(),\n                               [](const {ts}&) noexcept {{ return true; }},\n                               &{shout_struct}_LAYOUT_HASH) ==\n               system_core::system_component::Status::SUCCESS;\n    }}\n    if (!loaded) {{\n      (void)paramBank_.load({ts}{{}});\n    }}\n    (void)paramBank_.publishInitial();\n    setConfigured(true);\n    return loaded;\n  }}\n\n"
        );
    }

    let cmd_base = if manifest.commands.is_empty() {
        base.to_string()
    } else {
        format!("{component}CmdBase<{base}>")
    };
    let cmd_include = if manifest.commands.is_empty() {
        String::new()
    } else {
        format!("#include \"{component}CmdBase_auto.hpp\" // via the .auto include path\n")
    };

    let mut hooks = String::new();
    for c in &manifest.commands {
        let hook = format!("on{}", c.name);
        let sig = match (&c.request, &c.response) {
            (Some(rq), Some(rs)) => format!("{hook}(const {rq}& request, {rs}& response)"),
            (Some(rq), None) => format!("{hook}(const {rq}& request)"),
            (None, Some(rs)) => format!("{hook}({rs}& response)"),
            (None, None) => format!("{hook}()"),
        };
        hooks.push_str(&format!(
            "  [[nodiscard]] std::uint8_t {sig} noexcept override {{\n    // Component logic for {name} belongs here.\n    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);\n  }}\n\n",
            name = c.name
        ));
    }

    let (ns_open, ns_close) = match &manifest.namespace {
        Some(ns) => {
            let parts: Vec<&str> = ns.split("::").collect();
            (
                parts
                    .iter()
                    .map(|p| format!("namespace {p} {{\n"))
                    .collect::<String>(),
                parts
                    .iter()
                    .rev()
                    .map(|p| format!("}} // namespace {p}\n"))
                    .collect::<String>(),
            )
        }
        None => (String::new(), String::new()),
    };

    Ok(format!(
        "// Generated once by cdef_gen --stub from the {component} spec.\n\
         // USER-OWNED after generation: fill in the component logic; the\n\
         // .auto headers (structs, dispatch) keep regenerating separately.\n\
         #ifndef APEX_SPEC_STUB_{label}_HPP\n\
         #define APEX_SPEC_STUB_{label}_HPP\n\
         \n\
         #include \"{base_include}\"\n\
         {cmd_include}\
         {tunable_include}\
         \n\
         #include <cstdint>\n\
         #include <filesystem>\n\
         \n\
         {ns_open}\n\
         class {component} final : public {cmd_base} {{\n\
         public:\n\
           static constexpr std::uint16_t COMPONENT_ID = {id};\n\
           static constexpr const char* COMPONENT_NAME = \"{component}\";\n\
         \n\
           [[nodiscard]] std::uint16_t componentId() const noexcept override {{ return COMPONENT_ID; }}\n\
           [[nodiscard]] const char* componentName() const noexcept override {{ return COMPONENT_NAME; }}\n\
           [[nodiscard]] const char* label() const noexcept override {{ return \"{label}\"; }}\n\
         \n\
           {component}() noexcept = default;\n\
           ~{component}() override = default;\n\
         \n\
           enum class TaskUid : std::uint8_t {{\n\
             STEP = 1, ///< Periodic model step.\n\
           }};\n\
         \n\
           std::uint8_t step() noexcept {{\n\
             // Component periodic logic belongs here.\n\
             return 0;\n\
           }}\n\
         \n\
         {load_tprm}\
         protected:\n\
         {hooks}\
           [[nodiscard]] std::uint8_t doInit() noexcept override {{\n\
             registerTask<{component}, &{component}::step>(\n\
                 static_cast<std::uint8_t>(TaskUid::STEP), this, \"step\");\n\
             return 0;\n\
           }}\n\
         \n\
         private:\n\
         {bank_decl}\
         }};\n\
         \n\
         {ns_close}\
         #endif // APEX_SPEC_STUB_{label}_HPP\n"
    ))
}

/* ----------------------------- Tests ----------------------------- */

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tunable_params::manifest::parse_manifest_str;

    fn pilot_manifest() -> Manifest {
        parse_manifest_str(
            r#"
            component = "WaveGenerator"
            namespace = "appsim::wave"

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
        "#,
        )
        .unwrap()
    }

    #[test]
    fn header_carries_struct_assert_and_hash() {
        let h = generate_header(&pilot_manifest(), "WaveGenTunableParams").unwrap();
        assert!(h.contains("struct WaveGenTunableParams {"));
        assert!(h.contains("float frequency{1.0F}; ///< Primary frequency [Hz]"));
        assert!(h.contains("std::uint8_t reserved[3]{};"));
        assert!(h.contains("static_assert(sizeof(WaveGenTunableParams) == 7"));
        assert!(h.contains("WAVE_GEN_TUNABLE_PARAMS_LAYOUT_HASH"));
        assert!(h.contains("namespace appsim {"));
        assert!(h.contains("} // namespace wave"));
    }

    #[test]
    fn layout_hash_matches_serializer_over_generated_template_shape() {
        // The serializer's spec walk over the template this spec
        // generates must produce the same canonical string.
        let m = pilot_manifest();
        let fields = &m.fields["WaveGenTunableParams"];
        let spec = canonical_spec(fields);
        assert_eq!(spec, "frequency:float:4;reserved:array:3;[uint:1x3]");
    }

    #[test]
    fn cmd_base_dispatches_each_shape() {
        let m = parse_manifest_str(
            r#"
            component = "SpecSensor"
            namespace = "appsim::spec"

            [structs]
            SetModeRequest = { category = "COMMAND", opcode = "0x0200" }
            StatsResponse = { category = "TELEMETRY", opcode = "0x0201" }

            [[fields.SetModeRequest]]
            name = "mode"
            type = "uint"
            size = 1

            [[fields.StatsResponse]]
            name = "samples"
            type = "uint"
            size = 4

            [[commands]]
            name = "SetMode"
            opcode = "0x0200"
            request = "SetModeRequest"
            doc = "Select the sensor mode."

            [[commands]]
            name = "GetStats"
            opcode = "0x0201"
            response = "StatsResponse"

            [[commands]]
            name = "Recalibrate"
            opcode = "0x0202"
        "#,
        )
        .unwrap();
        let h = generate_cmd_base(&m).unwrap();
        assert!(h.contains("template <typename TBase> class SpecSensorCmdBase"));
        assert!(h.contains("onSetMode(const SetModeRequest& request)"));
        assert!(h.contains("onGetStats(StatsResponse& response)"));
        assert!(h.contains("onRecalibrate() noexcept = 0"));
        assert!(h.contains("case 0x0200U:"));
        assert!(h.contains("payload.size() != sizeof(SetModeRequest)"));
        assert!(h.contains("return TBase::handleCommand(opcode, payload, response);"));
        assert!(h.contains("#include \"SetModeRequest_auto.hpp\""));
    }

    #[test]
    fn stub_carries_identity_bank_and_generated_includes() {
        let m = parse_manifest_str(
            r#"
            component = "SpecSensor"
            namespace = "appsim::spec"
            component_id = 212
            component_type = "SW_MODEL"
            label = "SPEC_SNS"

            [structs]
            SpecSensorTunableParams = { category = "TUNABLE_PARAM" }
            SetModeRequest = { category = "COMMAND", opcode = "0x0200" }

            [[fields.SpecSensorTunableParams]]
            name = "sampleRateHz"
            type = "float"
            size = 4
            default = 10.0

            [[fields.SetModeRequest]]
            name = "mode"
            type = "uint"
            size = 1

            [[commands]]
            name = "SetMode"
            opcode = "0x0200"
            request = "SetModeRequest"
        "#,
        )
        .unwrap();
        let s = generate_stub(&m).unwrap();
        assert!(s.contains("class SpecSensor final"));
        assert!(s.contains("SpecSensorCmdBase<system_core::system_component::SwModelBase>"));
        assert!(s.contains("COMPONENT_ID = 212"));
        assert!(s.contains("return \"SPEC_SNS\";"));
        // Everything the stub references must be reachable through its own
        // includes: the dispatch base and the tunable struct + layout hash.
        assert!(s.contains("#include \"SpecSensorCmdBase_auto.hpp\""));
        assert!(s.contains("#include \"SpecSensorTunableParams_auto.hpp\""));
        assert!(s.contains("ParamBank<SpecSensorTunableParams> paramBank_{};"));
        assert!(s.contains("&SPEC_SENSOR_TUNABLE_PARAMS_LAYOUT_HASH"));
        assert!(s.contains("onSetMode(const SetModeRequest& request)"));
    }

    #[test]
    fn stub_without_component_id_is_an_error() {
        let m = parse_manifest_str("component = \"X\"\n[structs]\n").unwrap();
        assert!(generate_stub(&m).is_err());
    }

    #[test]
    fn cmd_base_rejects_unspecced_payload_struct() {
        let m = parse_manifest_str(
            r#"
            component = "SpecSensor"
            [structs]
            X = { category = "COMMAND" }
            [[commands]]
            name = "Bad"
            opcode = "0x0300"
            request = "MissingStruct"
        "#,
        )
        .unwrap();
        assert!(generate_cmd_base(&m).is_err());
    }

    #[test]
    fn unknown_type_is_an_error() {
        let mut m = pilot_manifest();
        m.fields.get_mut("WaveGenTunableParams").unwrap()[0].r#type = "quaternion".into();
        assert!(generate_header(&m, "WaveGenTunableParams").is_err());
    }
}
