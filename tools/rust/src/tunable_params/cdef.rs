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
        let ty = cpp_type(&f.r#type, f.size)?;
        let decl = match f.count {
            None => format!("{ty} {}{}", f.name, initializer(f)),
            Some(n) => format!("{ty} {}[{n}]{{}}", f.name),
        };
        match &f.doc {
            Some(doc) => body.push_str(&format!("  {decl}; ///< {doc}\n")),
            None => body.push_str(&format!("  {decl};\n")),
        }
    }

    let ns_open;
    let ns_close;
    match &manifest.namespace {
        Some(ns) => {
            let parts: Vec<&str> = ns.split("::").collect();
            ns_open = parts
                .iter()
                .map(|p| format!("namespace {p} {{\n"))
                .collect::<String>();
            ns_close = parts
                .iter()
                .rev()
                .map(|p| format!("}} // namespace {p}\n"))
                .collect::<String>();
        }
        None => {
            ns_open = String::new();
            ns_close = String::new();
        }
    }

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
    fn unknown_type_is_an_error() {
        let mut m = pilot_manifest();
        m.fields.get_mut("WaveGenTunableParams").unwrap()[0].r#type = "quaternion".into();
        assert!(generate_header(&m, "WaveGenTunableParams").is_err());
    }
}
