//! Apex proto profile: ingest and emit for spec-defined structs.
//!
//! The profile is a narrow, standards-conformant proto3 subset
//! (docs/proto_profile.md): one message per struct, declaration order
//! = layout order, field numbers 1..N in order, sizes/counts/defaults
//! carried by the apex custom options (tools/proto/apex/options.proto,
//! standard FieldOptions extensions). Anything that cannot be a fixed
//! memory image -- variable-length data, nested messages, oneof --
//! is inexpressible and fails ingest with a named error.
//!
//! Ingest resolves messages to the same ordered `FieldDef` lists the
//! `[[fields]]` TOML arrays produce, so every downstream surface
//! (headers, dispatch, dictionaries, payloads) is untouched. Emit
//! writes the canonical form; fixpoint tests pin both directions.

use std::collections::BTreeMap;

use super::manifest::{FieldDef, Manifest};
use super::Error;

/// Scalar declarations the profile accepts: proto type -> (logical
/// type, natural size, width-narrowable).
fn scalar(proto_type: &str) -> Option<(&'static str, u32, bool)> {
    match proto_type {
        "float" => Some(("float", 4, false)),
        "double" => Some(("float", 8, false)),
        "bool" => Some(("bool", 1, false)),
        "uint32" => Some(("uint", 4, true)),
        "int32" => Some(("int", 4, true)),
        "uint64" => Some(("uint", 8, false)),
        "int64" => Some(("int", 8, false)),
        _ => None,
    }
}

/// Parse a default literal by shape: bool, float (decimal point or
/// exponent), else integer.
fn parse_default(literal: &str, field: &str) -> Result<toml::Value, Error> {
    if literal == "true" || literal == "false" {
        return Ok(toml::Value::Boolean(literal == "true"));
    }
    if literal.contains('.') || literal.contains('e') || literal.contains('E') {
        return literal
            .parse::<f64>()
            .map(toml::Value::Float)
            .map_err(|_| Error::Parse(format!("field '{field}': bad default '{literal}'")));
    }
    literal
        .parse::<i64>()
        .map(toml::Value::Integer)
        .map_err(|_| Error::Parse(format!("field '{field}': bad default '{literal}'")))
}

/// Format a default as the literal `parse_default` reproduces.
fn format_default(value: &toml::Value) -> Result<String, Error> {
    match value {
        toml::Value::Float(f) => Ok(format!("{f:?}")),
        toml::Value::Integer(i) => Ok(i.to_string()),
        toml::Value::Boolean(b) => Ok(b.to_string()),
        other => Err(Error::Emit(format!(
            "default {other:?} has no proto literal form"
        ))),
    }
}

/// Field options recognized inside `[...]`.
#[derive(Default)]
struct FieldOpts {
    width: Option<u32>,
    count: Option<u32>,
    capacity: Option<u32>,
    default: Option<String>,
}

/// Parse `[(apex.width) = 1, (apex.default) = "5.0"]` (no brackets).
fn parse_options(body: &str, field: &str) -> Result<FieldOpts, Error> {
    let mut opts = FieldOpts::default();
    for part in split_options(body) {
        let (key, value) = part
            .split_once('=')
            .ok_or_else(|| Error::Parse(format!("field '{field}': bad option '{part}'")))?;
        let key = key.trim();
        let value = value.trim();
        match key {
            "(apex.width)" => {
                opts.width =
                    Some(value.parse().map_err(|_| {
                        Error::Parse(format!("field '{field}': bad width '{value}'"))
                    })?)
            }
            "(apex.count)" => {
                opts.count =
                    Some(value.parse().map_err(|_| {
                        Error::Parse(format!("field '{field}': bad count '{value}'"))
                    })?)
            }
            "(apex.capacity)" => {
                opts.capacity = Some(value.parse().map_err(|_| {
                    Error::Parse(format!("field '{field}': bad capacity '{value}'"))
                })?)
            }
            "(apex.default)" => {
                let inner = value
                    .strip_prefix('"')
                    .and_then(|v| v.strip_suffix('"'))
                    .ok_or_else(|| {
                        Error::Parse(format!("field '{field}': default must be a quoted literal"))
                    })?;
                opts.default = Some(inner.to_string());
            }
            other => {
                return Err(Error::Parse(format!(
                    "field '{field}': option '{other}' is outside the apex profile"
                )))
            }
        }
    }
    Ok(opts)
}

/// Split an option body on commas outside quotes.
fn split_options(body: &str) -> Vec<String> {
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    for c in body.chars() {
        match c {
            '"' => {
                in_quotes = !in_quotes;
                current.push(c);
            }
            ',' if !in_quotes => {
                parts.push(current.trim().to_string());
                current.clear();
            }
            _ => current.push(c),
        }
    }
    if !current.trim().is_empty() {
        parts.push(current.trim().to_string());
    }
    parts
}

/// Ingest a profile file: message name -> ordered field list.
pub fn ingest(source: &str) -> Result<BTreeMap<String, Vec<FieldDef>>, Error> {
    let mut messages: BTreeMap<String, Vec<FieldDef>> = BTreeMap::new();
    let mut current: Option<(String, Vec<FieldDef>, u32)> = None;
    let mut pending_doc: Vec<String> = Vec::new();
    let mut saw_syntax = false;
    let mut package: Option<String> = None;

    for (lineno, raw) in source.lines().enumerate() {
        let line = raw.trim();
        let n = lineno + 1;

        if line.is_empty() {
            pending_doc.clear();
            continue;
        }
        if let Some(comment) = line.strip_prefix("//") {
            // Only comments directly above a field accumulate as doc;
            // a blank line (above) resets, so banners never attach.
            if current.is_some() {
                pending_doc.push(comment.trim().to_string());
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("syntax") {
            let value = rest.trim_start_matches(['=', ' ']).trim_end_matches(';');
            if value != "\"proto3\"" {
                return Err(Error::Parse(format!("line {n}: profile requires proto3")));
            }
            saw_syntax = true;
            continue;
        }
        if let Some(rest) = line.strip_prefix("package") {
            package = Some(rest.trim().trim_end_matches(';').trim().to_string());
            continue;
        }
        if let Some(rest) = line.strip_prefix("import") {
            let value = rest.trim().trim_end_matches(';').trim();
            if value != "\"apex/options.proto\"" {
                return Err(Error::Parse(format!(
                    "line {n}: only apex/options.proto may be imported"
                )));
            }
            continue;
        }
        if let Some(rest) = line.strip_prefix("message") {
            if current.is_some() {
                return Err(Error::Parse(format!(
                    "line {n}: nested messages are outside the apex profile"
                )));
            }
            let name = rest.trim().trim_end_matches('{').trim();
            if name.is_empty() || !name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                return Err(Error::Parse(format!("line {n}: bad message name '{name}'")));
            }
            if messages.contains_key(name) {
                return Err(Error::Parse(format!(
                    "line {n}: duplicate message '{name}'"
                )));
            }
            current = Some((name.to_string(), Vec::new(), 0));
            pending_doc.clear();
            continue;
        }
        if line == "}" {
            let (name, fields, _) = current
                .take()
                .ok_or_else(|| Error::Parse(format!("line {n}: unmatched closing brace")))?;
            if fields.is_empty() {
                return Err(Error::Parse(format!("message '{name}' has no fields")));
            }
            messages.insert(name, fields);
            pending_doc.clear();
            continue;
        }

        // Anything else must be a field line inside a message.
        let Some((_, fields, last_number)) = current.as_mut() else {
            return Err(Error::Parse(format!(
                "line {n}: '{line}' is outside the apex profile"
            )));
        };
        let field = parse_field(line, n, &pending_doc)?;
        pending_doc.clear();
        *last_number += 1;
        if field.1 != *last_number {
            return Err(Error::Parse(format!(
                "line {n}: field '{}' numbered {} but declared at position {} \
                 (declaration order is layout order; numbers must be 1..N in order)",
                field.0.name, field.1, last_number
            )));
        }
        fields.push(field.0);
    }

    if current.is_some() {
        return Err(Error::Parse("unterminated message at end of file".into()));
    }
    if !saw_syntax {
        return Err(Error::Parse("missing 'syntax = \"proto3\";'".into()));
    }
    let _ = package; // validated against the manifest namespace by the caller
    Ok(messages)
}

/// The package line's value, for namespace validation by the caller.
pub fn package_of(source: &str) -> Option<String> {
    source.lines().find_map(|l| {
        l.trim()
            .strip_prefix("package")
            .map(|rest| rest.trim().trim_end_matches(';').trim().to_string())
    })
}

/// Join accumulated leading comments into a field doc.
fn join_doc(doc: &[String]) -> Option<String> {
    if doc.is_empty() {
        None
    } else {
        Some(doc.join(" "))
    }
}

/// Bounded text: `string x = i [(apex.capacity) = N]` -> fixed char
/// buffer (null-padded, packing fails on overflow); repeated adds
/// `(apex.count)` for a fixed array of such buffers. Unbounded is the
/// error case, with the fix named.
fn parse_string_field(
    name: &str,
    number: u32,
    n: usize,
    repeated: bool,
    opts: &FieldOpts,
    doc: &[String],
) -> Result<(FieldDef, u32), Error> {
    if opts.width.is_some() {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': width option not allowed on string"
        )));
    }
    if opts.default.is_some() {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': string defaults are outside the profile \
             (author the value in the TPRM TOML)"
        )));
    }
    let Some(capacity) = opts.capacity.filter(|c| *c > 0) else {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': unbounded string -- bound it with \
             (apex.capacity) = <bytes> (fixed char buffer, null-padded)"
        )));
    };
    let count = match (repeated, opts.count) {
        (true, Some(c)) if c > 0 => Some(c),
        (true, _) => {
            return Err(Error::Parse(format!(
                "line {n}: field '{name}': repeated string requires (apex.count) \
                 (the layout has no variable-length members)"
            )))
        }
        (false, Some(_)) => {
            return Err(Error::Parse(format!(
                "line {n}: field '{name}': count option on a non-repeated field"
            )))
        }
        (false, None) => None,
    };
    Ok((
        FieldDef {
            name: name.to_string(),
            r#type: "string".to_string(),
            size: capacity,
            count,
            default: None,
            doc: join_doc(doc),
        },
        number,
    ))
}

/// Bounded byte buffer: `bytes x = i [(apex.count) = N]` -> N-byte
/// array (uint8 elements). Unbounded is the error case.
fn parse_bytes_field(
    name: &str,
    number: u32,
    n: usize,
    repeated: bool,
    opts: &FieldOpts,
    doc: &[String],
) -> Result<(FieldDef, u32), Error> {
    if repeated || opts.width.is_some() || opts.capacity.is_some() || opts.default.is_some() {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': bytes takes only (apex.count) \
             (a fixed byte array; repeated/width/capacity/default do not apply)"
        )));
    }
    let Some(count) = opts.count.filter(|c| *c > 0) else {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': unbounded bytes -- bound it with \
             (apex.count) = <bytes> (fixed byte array)"
        )));
    };
    Ok((
        FieldDef {
            name: name.to_string(),
            r#type: "uint".to_string(),
            size: 1,
            count: Some(count),
            default: None,
            doc: join_doc(doc),
        },
        number,
    ))
}

/// Parse one field line: `[repeated] <scalar> <name> = <n> [opts];`
fn parse_field(line: &str, n: usize, doc: &[String]) -> Result<(FieldDef, u32), Error> {
    let body = line
        .strip_suffix(';')
        .ok_or_else(|| Error::Parse(format!("line {n}: field line must end with ';'")))?;

    // Split off options.
    let (decl, opts_body) = match body.split_once('[') {
        Some((d, rest)) => {
            let inner = rest
                .strip_suffix(']')
                .ok_or_else(|| Error::Parse(format!("line {n}: unterminated options block")))?;
            (d.trim(), Some(inner))
        }
        None => (body.trim(), None),
    };

    let mut tokens = decl.split_whitespace().collect::<Vec<_>>();
    let repeated = tokens.first() == Some(&"repeated");
    if repeated {
        tokens.remove(0);
    }
    let [proto_type, name, eq, number] = tokens[..] else {
        return Err(Error::Parse(format!(
            "line {n}: bad field declaration '{decl}'"
        )));
    };
    if eq != "=" {
        return Err(Error::Parse(format!(
            "line {n}: bad field declaration '{decl}'"
        )));
    }
    let number: u32 = number
        .parse()
        .map_err(|_| Error::Parse(format!("line {n}: bad field number '{number}'")))?;

    let opts = match opts_body {
        Some(b) => parse_options(b, name)?,
        None => FieldOpts::default(),
    };

    // Bounded text and byte buffers: standard proto types whose only
    // legal profile spellings carry an explicit bound (the nanopb
    // static-mode rule -- unbounded is a named error, never a
    // fallback to dynamic memory).
    if proto_type == "string" {
        return parse_string_field(name, number, n, repeated, &opts, doc);
    }
    if proto_type == "bytes" {
        return parse_bytes_field(name, number, n, repeated, &opts, doc);
    }

    let Some((logical, natural, narrowable)) = scalar(proto_type) else {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': type '{proto_type}' is outside the apex profile \
             (fixed-layout scalars only)"
        )));
    };

    if opts.capacity.is_some() {
        return Err(Error::Parse(format!(
            "line {n}: field '{name}': capacity option only applies to string fields"
        )));
    }

    let size = match opts.width {
        None => natural,
        Some(w) => {
            if !narrowable {
                return Err(Error::Parse(format!(
                    "line {n}: field '{name}': width option not allowed on {proto_type}"
                )));
            }
            if !matches!(w, 1 | 2 | 4) {
                return Err(Error::Parse(format!(
                    "line {n}: field '{name}': width must be 1, 2, or 4"
                )));
            }
            w
        }
    };

    let count = match (repeated, opts.count) {
        (true, Some(c)) if c > 0 => Some(c),
        (true, _) => {
            return Err(Error::Parse(format!(
                "line {n}: field '{name}': repeated requires (apex.count) \
                 (the layout has no variable-length members)"
            )))
        }
        (false, Some(_)) => {
            return Err(Error::Parse(format!(
                "line {n}: field '{name}': count option on a non-repeated field"
            )))
        }
        (false, None) => None,
    };

    let default = match &opts.default {
        None => None,
        Some(_) if count.is_some() => {
            return Err(Error::Parse(format!(
                "line {n}: field '{name}': defaults on repeated fields are outside the profile"
            )))
        }
        Some(literal) => {
            let value = parse_default(literal, name)?;
            let matches_type = matches!(
                (&value, logical),
                (toml::Value::Float(_), "float")
                    | (toml::Value::Integer(_), "uint")
                    | (toml::Value::Integer(_), "int")
                    | (toml::Value::Boolean(_), "bool")
            );
            if !matches_type {
                return Err(Error::Parse(format!(
                    "line {n}: field '{name}': default '{literal}' does not match {logical}"
                )));
            }
            Some(value)
        }
    };

    let doc = if doc.is_empty() {
        None
    } else {
        Some(doc.join(" "))
    };

    Ok((
        FieldDef {
            name: name.to_string(),
            r#type: logical.to_string(),
            size,
            count,
            default,
            doc,
        },
        number,
    ))
}

/// Emit the canonical profile file for a manifest's spec-defined
/// structs (its committed `.auto/<Component>.proto` interface).
pub fn emit(manifest: &Manifest) -> Result<String, Error> {
    let mut out = String::new();
    out.push_str(&format!(
        "// Generated by cdef_gen from the {} spec -- DO NOT EDIT.\n\
         // Regenerate: make cdef (check-cdef diffs this file against the spec).\n\
         // Layout metadata rides apex/options.proto (standard FieldOptions\n\
         // extensions); declaration order is layout order.\n\
         syntax = \"proto3\";\n\n",
        manifest.component
    ));
    if let Some(ns) = &manifest.namespace {
        out.push_str(&format!("package {};\n\n", ns.replace("::", ".")));
    }
    out.push_str("import \"apex/options.proto\";\n");

    for (struct_name, fields) in &manifest.fields {
        out.push_str(&format!("\nmessage {struct_name} {{\n"));
        for (i, f) in fields.iter().enumerate() {
            if let Some(doc) = &f.doc {
                out.push_str(&format!("  // {doc}\n"));
            }
            let (proto_type, width_opt) = proto_type_of(f)?;
            let mut opts: Vec<String> = Vec::new();
            if let Some(w) = width_opt {
                opts.push(format!("(apex.width) = {w}"));
            }
            if proto_type == "string" {
                opts.push(format!("(apex.capacity) = {}", f.size));
            }
            if let Some(c) = f.count {
                opts.push(format!("(apex.count) = {c}"));
            }
            if let Some(d) = &f.default {
                opts.push(format!("(apex.default) = \"{}\"", format_default(d)?));
            }
            let repeated = if f.count.is_some() { "repeated " } else { "" };
            let opts_text = if opts.is_empty() {
                String::new()
            } else {
                format!(" [{}]", opts.join(", "))
            };
            out.push_str(&format!(
                "  {repeated}{proto_type} {} = {}{opts_text};\n",
                f.name,
                i + 1
            ));
        }
        out.push_str("}\n");
    }
    Ok(out)
}

/// Proto declaration for a field: (scalar, width option when the
/// natural width narrows).
fn proto_type_of(f: &FieldDef) -> Result<(&'static str, Option<u32>), Error> {
    Ok(match (f.r#type.as_str(), f.size) {
        ("float", 4) => ("float", None),
        ("float", 8) | ("double", 8) => ("double", None),
        ("bool", 1) => ("bool", None),
        ("uint", 4) => ("uint32", None),
        ("uint", 8) => ("uint64", None),
        ("uint", w @ (1 | 2)) => ("uint32", Some(w)),
        ("int", 4) => ("int32", None),
        ("int", 8) => ("int64", None),
        ("int", w @ (1 | 2)) => ("int32", Some(w)),
        ("string", _) => ("string", None),
        (other, size) => {
            return Err(Error::Emit(format!(
                "field '{}': {other}/{size} has no proto profile form",
                f.name
            )))
        }
    })
}

/* --------------------------------- Tests ---------------------------------- */

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tunable_params::cdef::canonical_spec;
    use crate::tunable_params::manifest::parse_manifest_str;

    const AUTHORED: &str = r#"
syntax = "proto3";

package appsim.spec;

import "apex/options.proto";

message SpecActuatorTunableParams {
  // Slew rate limit [units/s].
  float rateLimit = 1 [(apex.default) = "5.0"];
  // Hold band around the commanded position.
  float holdBand = 2 [(apex.default) = "0.1"];
  uint32 mode = 3 [(apex.width) = 1];
  repeated uint32 reserved = 4 [(apex.width) = 1, (apex.count) = 3];
}

message MoveRequest {
  // Commanded position.
  float position = 1;
}
"#;

    #[test]
    fn ingest_resolves_ordered_fields_with_options() {
        let messages = ingest(AUTHORED).unwrap();
        let t = &messages["SpecActuatorTunableParams"];
        assert_eq!(t.len(), 4);
        assert_eq!(t[0].name, "rateLimit");
        assert_eq!(t[0].r#type, "float");
        assert_eq!(t[0].size, 4);
        assert_eq!(t[0].default, Some(toml::Value::Float(5.0)));
        assert_eq!(t[0].doc.as_deref(), Some("Slew rate limit [units/s]."));
        assert_eq!(t[2].size, 1);
        assert_eq!(t[3].count, Some(3));
        assert_eq!(t[3].size, 1);
        assert_eq!(messages["MoveRequest"].len(), 1);
    }

    #[test]
    fn declaration_order_must_match_numbering() {
        let bad = AUTHORED.replace("uint32 mode = 3", "uint32 mode = 7");
        let err = ingest(&bad).unwrap_err().to_string();
        assert!(err.contains("declaration order is layout order"));
    }

    #[test]
    fn dynamic_data_is_inexpressible() {
        for (decl, needle) in [
            ("string name = 1;", "unbounded string"),
            ("bytes blob = 1;", "unbounded bytes"),
            ("repeated float xs = 1;", "repeated requires (apex.count)"),
        ] {
            let src = format!("syntax = \"proto3\";\nmessage M {{\n  {decl}\n}}\n");
            let err = ingest(&src).unwrap_err().to_string();
            assert!(err.contains(needle), "{decl}: {err}");
        }
    }

    #[test]
    fn bounded_strings_and_bytes_resolve_to_fixed_buffers() {
        let src = r#"
syntax = "proto3";
message M {
  // Axis label.
  string axisLabel = 1 [(apex.capacity) = 8];
  repeated string tags = 2 [(apex.capacity) = 4, (apex.count) = 2];
  bytes blob = 3 [(apex.count) = 6];
}
"#;
        let m = &ingest(src).unwrap()["M"];
        assert_eq!(
            (m[0].r#type.as_str(), m[0].size, m[0].count),
            ("string", 8, None)
        );
        assert_eq!(
            (m[1].r#type.as_str(), m[1].size, m[1].count),
            ("string", 4, Some(2))
        );
        assert_eq!(
            (m[2].r#type.as_str(), m[2].size, m[2].count),
            ("uint", 1, Some(6))
        );
        // Layout-hash agreement with the value-TOML serializer walk:
        // scalar strings hash as name:string:N; arrays as the element
        // descriptor form.
        assert_eq!(
            canonical_spec(m),
            "axisLabel:string:8:0;tags:array:8:8;[string:4x2]blob:array:6:16;[uint:1x6]|size:22"
        );
    }

    #[test]
    fn unbounded_string_and_bytes_yell_with_the_fix() {
        for (decl, needle) in [
            ("string name = 1;", "bound it with (apex.capacity)"),
            (
                "repeated string tags = 1 [(apex.capacity) = 4];",
                "repeated string requires (apex.count)",
            ),
            ("bytes blob = 1;", "bound it with (apex.count)"),
            (
                "string name = 1 [(apex.capacity) = 8, (apex.width) = 1];",
                "width option not allowed on string",
            ),
            (
                "float x = 1 [(apex.capacity) = 8];",
                "capacity option only applies to string",
            ),
        ] {
            let src = format!("syntax = \"proto3\";\nmessage M {{\n  {decl}\n}}\n");
            let err = ingest(&src).unwrap_err().to_string();
            assert!(err.contains(needle), "{decl}: {err}");
        }
    }

    #[test]
    fn string_fields_round_trip_through_emit() {
        let src = r#"
syntax = "proto3";
message M {
  string axisLabel = 1 [(apex.capacity) = 8];
  repeated string tags = 2 [(apex.capacity) = 4, (apex.count) = 2];
}
"#;
        let mut m = parse_manifest_str(
            "component = \"X\"\n[structs]\nM = { category = \"TUNABLE_PARAM\" }\n",
        )
        .unwrap();
        m.fields = ingest(src).unwrap();
        let first = emit(&m).unwrap();
        assert!(first.contains("string axisLabel = 1 [(apex.capacity) = 8];"));
        assert!(first.contains("repeated string tags = 2 [(apex.capacity) = 4, (apex.count) = 2];"));
        let hash_before: Vec<String> = m.fields.values().map(|f| canonical_spec(f)).collect();
        m.fields = ingest(&first).unwrap();
        let hash_after: Vec<String> = m.fields.values().map(|f| canonical_spec(f)).collect();
        assert_eq!(first, emit(&m).unwrap());
        assert_eq!(hash_before, hash_after);
    }

    #[test]
    fn width_rules_enforced() {
        let src = "syntax = \"proto3\";\nmessage M {\n  uint64 x = 1 [(apex.width) = 2];\n}\n";
        assert!(ingest(src).unwrap_err().to_string().contains("not allowed"));
        let src = "syntax = \"proto3\";\nmessage M {\n  uint32 x = 1 [(apex.width) = 3];\n}\n";
        assert!(ingest(src).unwrap_err().to_string().contains("1, 2, or 4"));
    }

    fn spec_manifest() -> Manifest {
        parse_manifest_str(
            r#"
            component = "SpecActuator"
            namespace = "appsim::spec"

            [structs]
            SpecActuatorTunableParams = { category = "TUNABLE_PARAM" }
            MoveRequest = { category = "COMMAND", opcode = "0x0210" }

            [[fields.SpecActuatorTunableParams]]
            name = "rateLimit"
            type = "float"
            size = 4
            default = 5.0
            doc = "Slew rate limit [units/s]."

            [[fields.SpecActuatorTunableParams]]
            name = "mode"
            type = "uint"
            size = 1

            [[fields.SpecActuatorTunableParams]]
            name = "reserved"
            type = "uint"
            size = 1
            count = 3

            [[fields.MoveRequest]]
            name = "position"
            type = "float"
            size = 4
            doc = "Commanded position."
        "#,
        )
        .unwrap()
    }

    #[test]
    fn fixpoint_emit_ingest_emit_is_stable() {
        // Round 1: emit from a TOML-authored manifest.
        let mut m = spec_manifest();
        let first = emit(&m).unwrap();

        // Round 2: ingest the emission and emit again.
        let ingested = ingest(&first).unwrap();
        let hash_before: Vec<String> = m.fields.values().map(|f| canonical_spec(f)).collect();
        m.fields = ingested;
        let hash_after: Vec<String> = m.fields.values().map(|f| canonical_spec(f)).collect();
        let second = emit(&m).unwrap();

        assert_eq!(first, second, "emission is not a fixpoint");
        assert_eq!(
            hash_before, hash_after,
            "layout hash moved through the round-trip"
        );
    }

    #[test]
    fn package_mismatch_is_caller_visible() {
        assert_eq!(package_of(AUTHORED).as_deref(), Some("appsim.spec"));
    }
}
