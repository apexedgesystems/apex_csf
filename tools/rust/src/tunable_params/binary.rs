//! Binary serialization: convert TOML/JSON config to raw bytes.
//!
//! Serializes field values to little-endian binary format for embedding
//! in firmware or loading at runtime.
//!
//! Uses `toml_edit` for TOML parsing to preserve field declaration order.
//!
//! Supports:
//! - Hex literals: `value = "0x6500"` or `value = "0xFF"`
//! - Enum strings: `value = "HARD_PERIOD_COMPLETE"` (looked up in __enums__)

use serde_json::Value as Json;
use std::{collections::HashMap, fs, io::Write, path::Path};
use toml_edit::DocumentMut;

use super::Error;

/// Built-in enum mappings for common Apex types.
/// Users can override these via __enums__ section in their TOML.
fn builtin_enums() -> HashMap<&'static str, i64> {
    let mut m = HashMap::new();
    // RT modes (from ExecutiveTunableParams)
    m.insert("HARD_TICK_COMPLETE", 0);
    m.insert("HARD_PERIOD_COMPLETE", 1);
    m.insert("SOFT_SKIP_ON_BUSY", 2);
    m.insert("SOFT_LAG_TOLERANT", 3);
    m.insert("SOFT_LOG_ONLY", 4);
    // Startup modes
    m.insert("AUTO", 0);
    m.insert("MANUAL", 1);
    // Shutdown modes
    m.insert("NONE", 0);
    m.insert("ABSOLUTE_TIME", 1);
    m.insert("RELATIVE_TIME", 2);
    m.insert("CYCLE_COUNT", 3);
    // Thread policies
    m.insert("SCHED_OTHER", 0);
    m.insert("SCHED_FIFO", 1);
    m.insert("SCHED_RR", 2);
    // Framing protocols
    m.insert("SLIP", 0);
    m.insert("COBS", 1);
    m.insert("RAW", 2);
    m
}

/* ----------------------------- Public API ----------------------------- */

/// Load a TOML or JSON config file into the JSON value tree the
/// serializer walks (TOML via toml_edit to preserve field order).
pub fn load_config(config_path: &Path) -> Result<Json, Error> {
    let content = fs::read_to_string(config_path)?;
    if config_path.extension().is_some_and(|e| e == "toml") {
        let doc: DocumentMut = content
            .parse()
            .map_err(|e| Error::Parse(format!("TOML parse error: {e}")))?;
        Ok(toml_edit_to_json(doc.as_item()))
    } else {
        serde_json::from_str(&content).map_err(|e| Error::Parse(format!("JSON parse error: {e}")))
    }
}

/// Load a TOML or JSON file and serialize to raw bytes (no prelude):
/// sequences and generic blobs.
pub fn config_to_binary(config_path: &Path, output_path: &Path) -> Result<(), Error> {
    let binary = serialize_value(&load_config(config_path)?)?;
    let mut file = fs::File::create(output_path)?;
    file.write_all(&binary)?;
    Ok(())
}

/// Load a config, serialize, and stamp the v3 payload prelude for the
/// component the packing manifest targets.
pub fn config_to_binary_v3(
    config_path: &Path,
    output_path: &Path,
    full_uid: u32,
) -> Result<(), Error> {
    let (payload, layout_hash) = serialize_value_with_layout(&load_config(config_path)?)?;
    let stamped = super::payload::stamp(full_uid, layout_hash, &payload)?;
    let mut file = fs::File::create(output_path)?;
    file.write_all(&stamped)?;
    Ok(())
}

/// Serialize a JSON value tree to binary bytes.
pub fn serialize_value(value: &Json) -> Result<Vec<u8>, Error> {
    Ok(serialize_value_with_layout(value)?.0)
}

/// Serialize a JSON value tree to binary bytes plus the layout hash:
/// the CRC-32 of the canonical field spec (`name:type:size;` per leaf
/// in emission order) that the v3 payload prelude carries.
pub fn serialize_value_with_layout(value: &Json) -> Result<(Vec<u8>, u32), Error> {
    let (bytes, hash, _rows) = serialize_value_with_layout_and_rows(value)?;
    Ok((bytes, hash))
}

/// Serialize plus the constraint rows: one JSON row per constrained
/// leaf ({offset, size, kind, min?, max?, step?, allowed?}) in body
/// order -- the source the build compiles into the on-board table.
/// Kind encoding matches the C++ TprmFieldKind (0=int, 1=uint,
/// 2=float).
pub fn serialize_value_with_layout_and_rows(
    value: &Json,
) -> Result<(Vec<u8>, u32, Vec<Json>), Error> {
    let mut out = Vec::new();
    let mut spec = String::new();
    let mut rows = Vec::new();
    // Build enum map: start with builtins, add user-defined from __enums__
    let mut enums: HashMap<String, i64> = builtin_enums()
        .into_iter()
        .map(|(k, v)| (k.to_string(), v))
        .collect();
    if let Some(user_enums) = value.get("__enums__").and_then(|v| v.as_object()) {
        for (name, val) in user_enums {
            if let Some(n) = val.as_i64() {
                enums.insert(name.clone(), n);
            }
        }
    }
    traverse_and_serialize(value, &mut out, &mut spec, &mut rows, &enums)?;
    Ok((out, super::payload::crc32(spec.as_bytes()), rows))
}

/// Record a constraint row for a leaf about to serialize at `offset`.
fn record_constraint_row(
    rows: &mut Vec<Json>,
    field_type: &str,
    field: &Json,
    offset: usize,
    size: usize,
) {
    let has_constraint = ["min", "max", "allowed", "step"]
        .iter()
        .any(|k| field.get(*k).is_some());
    if !has_constraint {
        return;
    }
    let kind = match field_type {
        "int" => 0,
        "uint" => 1,
        "float" | "double" => 2,
        _ => return,
    };
    let mut row = serde_json::Map::new();
    row.insert("offset".into(), serde_json::json!(offset));
    row.insert("size".into(), serde_json::json!(size));
    row.insert("kind".into(), serde_json::json!(kind));
    for k in ["min", "max", "step", "allowed"] {
        if let Some(v) = field.get(k) {
            row.insert(k.to_string(), v.clone());
        }
    }
    rows.push(Json::Object(row));
}

/* ----------------------------- Constraints ----------------------------- */

/// Enforce the field's declared constraints (`min`, `max`, `allowed`,
/// `step`): rail 1 of the shared constraint declaration -- a value
/// outside its legal range never becomes bytes. Constraint keys are
/// authoring metadata only; they contribute nothing to the serialized
/// payload or the layout hash.
fn validate_constraints(name: &str, field: &Json, value: f64) -> Result<(), Error> {
    let min = field.get("min").and_then(|v| v.as_f64());
    if let Some(m) = min {
        if value < m {
            return Err(Error::Emit(format!("{name}: value {value} below min {m}")));
        }
    }
    if let Some(m) = field.get("max").and_then(|v| v.as_f64()) {
        if value > m {
            return Err(Error::Emit(format!("{name}: value {value} above max {m}")));
        }
    }
    if let Some(allowed) = field.get("allowed").and_then(|v| v.as_array()) {
        let ok = allowed
            .iter()
            .filter_map(|a| a.as_f64())
            .any(|a| a == value);
        if !ok {
            return Err(Error::Emit(format!(
                "{name}: value {value} not in the allowed list"
            )));
        }
    }
    if let Some(step) = field.get("step").and_then(|v| v.as_f64()) {
        if step > 0.0 {
            let base = min.unwrap_or(0.0);
            let steps = ((value - base) / step).round();
            if (base + steps * step - value).abs() > step * 1e-9 {
                return Err(Error::Emit(format!(
                    "{name}: value {value} is not min + N*step (base {base}, step {step})"
                )));
            }
        }
    }
    Ok(())
}

/// Validate numeric value against type/size constraints.
fn validate_numeric(field_type: &str, size: usize, value: f64) -> Result<(), Error> {
    let (min, max) = match (field_type, size) {
        // Signed integers
        ("int", 1) => (i8::MIN as f64, i8::MAX as f64),
        ("int", 2) => (i16::MIN as f64, i16::MAX as f64),
        ("int", 4) => (i32::MIN as f64, i32::MAX as f64),
        ("int", 8) => (i64::MIN as f64, i64::MAX as f64),
        // Unsigned integers
        ("uint", 1) => (0.0, u8::MAX as f64),
        ("uint", 2) => (0.0, u16::MAX as f64),
        ("uint", 4) => (0.0, u32::MAX as f64),
        ("uint", 8) => (0.0, u64::MAX as f64),
        // Floats
        ("float", 4) => (f32::MIN as f64, f32::MAX as f64),
        ("float", 8) | ("double", 8) => (f64::MIN, f64::MAX),
        // Bool (treated as 0 or 1)
        ("bool", 1) => (0.0, 1.0),
        // Char (ASCII range)
        ("char", 1) => (0.0, 127.0),
        // Unknown - skip validation
        _ => return Ok(()),
    };

    if value < min || value > max {
        return Err(Error::Emit(format!(
            "value {} exceeds limits for {} of size {} bytes (min: {}, max: {})",
            value, field_type, size, min, max
        )));
    }

    Ok(())
}

/* ----------------------------- Traversal ----------------------------- */

fn traverse_and_serialize(
    value: &Json,
    out: &mut Vec<u8>,
    spec: &mut String,
    rows: &mut Vec<Json>,
    enums: &HashMap<String, i64>,
) -> Result<(), Error> {
    match value {
        Json::Object(map) => {
            for (key, val) in map {
                // Skip metadata keys
                if key.starts_with("__") {
                    continue;
                }
                // If this is a field with type/size/value, serialize it
                if let Some(field_type) = val.get("type").and_then(|v| v.as_str()) {
                    let size = val.get("size").and_then(|v| v.as_u64()).unwrap_or(0);
                    spec.push_str(&format!("{key}:{field_type}:{size};"));
                    serialize_field(key, field_type, val, out, spec, rows, enums)?;
                } else {
                    // Recurse into nested structures
                    traverse_and_serialize(val, out, spec, rows, enums)?;
                }
            }
        }
        Json::Array(arr) => {
            for item in arr {
                traverse_and_serialize(item, out, spec, rows, enums)?;
            }
        }
        _ => {}
    }
    Ok(())
}

/* ----------------------------- Field Serialization ----------------------------- */

fn serialize_field(
    name: &str,
    field_type: &str,
    field: &Json,
    out: &mut Vec<u8>,
    spec: &mut String,
    rows: &mut Vec<Json>,
    enums: &HashMap<String, i64>,
) -> Result<(), Error> {
    let size = field.get("size").and_then(|v| v.as_u64()).unwrap_or(0) as usize;
    let value = field.get("value");
    record_constraint_row(rows, field_type, field, out.len(), size);

    match field_type {
        "int" => {
            let v = parse_int_value(value, enums)?;
            validate_numeric(field_type, size, v as f64)?;
            validate_constraints(name, field, v as f64)?;
            out.extend(&int_to_bytes(v, size)?);
        }
        "uint" => {
            let v = parse_uint_value(value, enums)?;
            validate_numeric(field_type, size, v as f64)?;
            validate_constraints(name, field, v as f64)?;
            out.extend(&uint_to_bytes(v, size)?);
        }
        "float" => {
            let v = value.and_then(|v| v.as_f64()).unwrap_or(0.0);
            validate_numeric(field_type, size, v)?;
            validate_constraints(name, field, v)?;
            if size == 4 {
                out.extend(&(v as f32).to_le_bytes());
            } else if size == 8 {
                out.extend(&v.to_le_bytes());
            } else {
                return Err(Error::Emit(format!("Invalid float size: {size}")));
            }
        }
        "double" => {
            let v = value.and_then(|v| v.as_f64()).unwrap_or(0.0);
            validate_numeric(field_type, size, v)?;
            validate_constraints(name, field, v)?;
            if size != 8 {
                return Err(Error::Emit(format!("double must be 8 bytes, got {size}")));
            }
            out.extend(&v.to_le_bytes());
        }
        "bool" => {
            let v = value.and_then(|v| v.as_bool()).unwrap_or(false);
            out.push(if v { 1 } else { 0 });
        }
        "char" => {
            let v = value
                .and_then(|v| v.as_str())
                .and_then(|s| s.chars().next())
                .unwrap_or('\0');
            let byte = v as u32;
            if byte > 127 {
                return Err(Error::Emit(format!("char '{}' is not ASCII", v)));
            }
            out.push(byte as u8);
        }
        "string" => {
            let v = value.and_then(|v| v.as_str()).unwrap_or("");
            if v.len() > size {
                return Err(Error::Emit(format!(
                    "string '{}' exceeds size {} bytes",
                    v, size
                )));
            }
            let mut bytes = v.as_bytes().to_vec();
            bytes.resize(size, 0); // Pad with null bytes
            out.extend(&bytes);
        }
        "array" => {
            if let Some(Json::Array(elements)) = value {
                let elem_type = field
                    .get("element_type")
                    .and_then(|v| v.as_str())
                    .unwrap_or("uint");
                let elem_size = size / elements.len().max(1);
                // Element shape is layout: equal-size arrays of different
                // element types must hash apart.
                spec.push_str(&format!("[{elem_type}:{elem_size}x{}]", elements.len()));
                for (i, elem) in elements.iter().enumerate() {
                    let mut elem_field = serde_json::json!({
                        "type": elem_type,
                        "size": elem_size,
                        "value": elem
                    });
                    for k in ["min", "max", "allowed", "step"] {
                        if let Some(c) = field.get(k) {
                            elem_field[k] = c.clone();
                        }
                    }
                    let elem_name = format!("{name}[{i}]");
                    serialize_field(&elem_name, elem_type, &elem_field, out, spec, rows, enums)?;
                }
            }
        }
        "struct" => {
            if let Some(fields) = field.get("fields") {
                traverse_and_serialize(fields, out, spec, rows, enums)?;
            }
        }
        _ => {
            // Unknown type - emit zeros
            out.extend(vec![0u8; size]);
        }
    }

    Ok(())
}

/// Parse an integer value, supporting:
/// - Direct i64 numbers
/// - Hex string literals ("0x6500")
/// - Enum string names ("HARD_PERIOD_COMPLETE")
fn parse_int_value(value: Option<&Json>, enums: &HashMap<String, i64>) -> Result<i64, Error> {
    match value {
        Some(Json::Number(n)) => Ok(n.as_i64().unwrap_or(0)),
        Some(Json::String(s)) => {
            // Try hex literal first
            if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
                i64::from_str_radix(hex, 16)
                    .map_err(|_| Error::Emit(format!("invalid hex literal: {}", s)))
            } else if let Some(&val) = enums.get(s.as_str()) {
                // Try enum lookup
                Ok(val)
            } else {
                Err(Error::Emit(format!(
                    "unknown enum or invalid value: '{}'. Use a number, hex (0x...), or enum name.",
                    s
                )))
            }
        }
        _ => Ok(0),
    }
}

/// Parse an unsigned integer value, supporting hex literals and enums.
fn parse_uint_value(value: Option<&Json>, enums: &HashMap<String, i64>) -> Result<u64, Error> {
    match value {
        Some(Json::Number(n)) => Ok(n.as_u64().unwrap_or(0)),
        Some(Json::String(s)) => {
            // Try hex literal first
            if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
                u64::from_str_radix(hex, 16)
                    .map_err(|_| Error::Emit(format!("invalid hex literal: {}", s)))
            } else if let Some(&val) = enums.get(s.as_str()) {
                // Try enum lookup (cast to u64)
                if val < 0 {
                    Err(Error::Emit(format!(
                        "enum '{}' has negative value {} but uint expected",
                        s, val
                    )))
                } else {
                    Ok(val as u64)
                }
            } else {
                Err(Error::Emit(format!(
                    "unknown enum or invalid value: '{}'. Use a number, hex (0x...), or enum name.",
                    s
                )))
            }
        }
        _ => Ok(0),
    }
}

/* ----------------------------- Helpers ----------------------------- */

fn int_to_bytes(v: i64, size: usize) -> Result<Vec<u8>, Error> {
    match size {
        1 => Ok((v as i8).to_le_bytes().to_vec()),
        2 => Ok((v as i16).to_le_bytes().to_vec()),
        4 => Ok((v as i32).to_le_bytes().to_vec()),
        8 => Ok(v.to_le_bytes().to_vec()),
        _ => Err(Error::Emit(format!("Invalid int size: {size}"))),
    }
}

fn uint_to_bytes(v: u64, size: usize) -> Result<Vec<u8>, Error> {
    match size {
        1 => Ok((v as u8).to_le_bytes().to_vec()),
        2 => Ok((v as u16).to_le_bytes().to_vec()),
        4 => Ok((v as u32).to_le_bytes().to_vec()),
        8 => Ok(v.to_le_bytes().to_vec()),
        _ => Err(Error::Emit(format!("Invalid uint size: {size}"))),
    }
}

/// Convert toml_edit Item to serde_json Value, preserving field order.
fn toml_edit_to_json(item: &toml_edit::Item) -> Json {
    use toml_edit::Item;

    match item {
        Item::None => Json::Null,
        Item::Value(v) => toml_edit_value_to_json(v),
        Item::Table(t) => {
            // Iterate in declaration order (toml_edit preserves order)
            let mut map = serde_json::Map::new();
            for (k, v) in t.iter() {
                map.insert(k.to_string(), toml_edit_to_json(v));
            }
            Json::Object(map)
        }
        Item::ArrayOfTables(aot) => Json::Array(
            aot.iter()
                .map(|t| {
                    let mut map = serde_json::Map::new();
                    for (k, v) in t.iter() {
                        map.insert(k.to_string(), toml_edit_to_json(v));
                    }
                    Json::Object(map)
                })
                .collect(),
        ),
    }
}

fn toml_edit_value_to_json(v: &toml_edit::Value) -> Json {
    use toml_edit::Value;

    match v {
        Value::String(s) => Json::String(s.value().clone()),
        Value::Integer(i) => Json::Number((*i.value()).into()),
        Value::Float(f) => serde_json::Number::from_f64(*f.value())
            .map(Json::Number)
            .unwrap_or(Json::Null),
        Value::Boolean(b) => Json::Bool(*b.value()),
        Value::Datetime(dt) => Json::String(dt.value().to_string()),
        Value::Array(arr) => Json::Array(arr.iter().map(toml_edit_value_to_json).collect()),
        Value::InlineTable(t) => {
            let mut map = serde_json::Map::new();
            for (k, v) in t.iter() {
                map.insert(k.to_string(), toml_edit_value_to_json(v));
            }
            Json::Object(map)
        }
    }
}

/* ----------------------------- Tests ----------------------------- */

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn serializes_uint_fields() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 4, "value": 42 },
                "b": { "type": "uint", "size": 2, "value": 0x1234 }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![42, 0, 0, 0, 0x34, 0x12]);
    }

    #[test]
    fn serializes_int_fields() {
        let data = json!({
            "Test": {
                "a": { "type": "int", "size": 4, "value": -1 }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![0xFF, 0xFF, 0xFF, 0xFF]);
    }

    #[test]
    fn serializes_string_fields() {
        let data = json!({
            "Test": {
                "name": { "type": "string", "size": 8, "value": "hello" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, b"hello\0\0\0");
    }

    #[test]
    fn serializes_bool_fields() {
        let data = json!({
            "Test": {
                "flag": { "type": "bool", "size": 1, "value": true }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![1]);
    }

    #[test]
    fn serializes_double_fields() {
        let data = json!({
            "Test": {
                "d": { "type": "double", "size": 8, "value": 1.25 }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes.len(), 8);
        // Verify it's a valid double
        let val = f64::from_le_bytes(bytes.try_into().unwrap());
        assert!((val - 1.25).abs() < 1e-10);
    }

    #[test]
    fn skips_metadata_keys() {
        let data = json!({
            "__note__": "ignored",
            "__defines__": {},
            "Test": {
                "a": { "type": "uint", "size": 1, "value": 7 }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![7]);
    }

    #[test]
    fn validates_uint_overflow() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 1, "value": 256 }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("exceeds limits"));
    }

    #[test]
    fn validates_int_underflow() {
        let data = json!({
            "Test": {
                "a": { "type": "int", "size": 1, "value": -129 }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("exceeds limits"));
    }

    #[test]
    fn validates_string_overflow() {
        let data = json!({
            "Test": {
                "name": { "type": "string", "size": 4, "value": "hello" }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("exceeds size"));
    }

    #[test]
    fn validates_non_ascii_char() {
        let data = json!({
            "Test": {
                "c": { "type": "char", "size": 1, "value": "\u{00E9}" }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("not ASCII"));
    }

    #[test]
    fn rejects_double_wrong_size() {
        let data = json!({
            "Test": {
                "d": { "type": "double", "size": 4, "value": 1.0 }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("must be 8 bytes"));
    }

    #[test]
    fn serializes_arrays() {
        let data = json!({
            "Test": {
                "arr": {
                    "type": "array",
                    "size": 4,
                    "element_type": "uint",
                    "value": [1, 2, 3, 4]
                }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![1, 2, 3, 4]);
    }

    #[test]
    fn serializes_nested_struct() {
        let data = json!({
            "Outer": {
                "inner": {
                    "type": "struct",
                    "size": 3,
                    "fields": {
                        "a": { "type": "uint", "size": 1, "value": 10 },
                        "b": { "type": "uint", "size": 2, "value": 0x1234 }
                    }
                }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![10, 0x34, 0x12]);
    }

    #[test]
    fn parses_hex_literals() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 4, "value": "0x6500" },
                "b": { "type": "uint", "size": 2, "value": "0xFF" },
                "c": { "type": "uint", "size": 1, "value": "0x0A" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        // 0x6500 = 25856 in little-endian = [0x00, 0x65, 0x00, 0x00]
        // 0xFF = 255 in little-endian = [0xFF, 0x00]
        // 0x0A = 10 = [0x0A]
        assert_eq!(bytes, vec![0x00, 0x65, 0x00, 0x00, 0xFF, 0x00, 0x0A]);
    }

    #[test]
    fn parses_builtin_enums() {
        let data = json!({
            "Test": {
                "rtMode": { "type": "uint", "size": 1, "value": "HARD_PERIOD_COMPLETE" },
                "policy": { "type": "int", "size": 1, "value": "SCHED_FIFO" },
                "shutdown": { "type": "uint", "size": 1, "value": "RELATIVE_TIME" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        // HARD_PERIOD_COMPLETE = 1, SCHED_FIFO = 1, RELATIVE_TIME = 2
        assert_eq!(bytes, vec![1, 1, 2]);
    }

    #[test]
    fn parses_user_defined_enums() {
        let data = json!({
            "__enums__": {
                "MY_CUSTOM_VALUE": 42,
                "ANOTHER_VALUE": 100
            },
            "Test": {
                "a": { "type": "uint", "size": 1, "value": "MY_CUSTOM_VALUE" },
                "b": { "type": "uint", "size": 1, "value": "ANOTHER_VALUE" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![42, 100]);
    }

    #[test]
    fn user_enums_override_builtins() {
        let data = json!({
            "__enums__": {
                "SCHED_FIFO": 99  // Override builtin value of 1
            },
            "Test": {
                "policy": { "type": "uint", "size": 1, "value": "SCHED_FIFO" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![99]);
    }

    #[test]
    fn rejects_unknown_enum() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 1, "value": "NOT_A_REAL_ENUM" }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("unknown enum"));
    }

    #[test]
    fn rejects_invalid_hex() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 1, "value": "0xGGGG" }
            }
        });
        let result = serialize_value(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("invalid hex"));
    }

    #[test]
    fn parses_uppercase_hex() {
        let data = json!({
            "Test": {
                "a": { "type": "uint", "size": 2, "value": "0XABCD" }
            }
        });
        let bytes = serialize_value(&data).unwrap();
        assert_eq!(bytes, vec![0xCD, 0xAB]); // Little-endian
    }

    #[test]
    fn constraints_reject_each_violation_by_field_name() {
        let below = json!({"rate": {"type": "uint", "size": 2, "value": 5, "min": 10}});
        assert!(format!("{}", serialize_value(&below).unwrap_err()).contains("rate"));

        let above = json!({"gain": {"type": "float", "size": 4, "value": 11.0, "max": 10.0}});
        assert!(format!("{}", serialize_value(&above).unwrap_err()).contains("above max"));

        let outside =
            json!({"mode": {"type": "uint", "size": 1, "value": 3, "allowed": [0, 1, 2]}});
        assert!(format!("{}", serialize_value(&outside).unwrap_err()).contains("allowed"));

        let off_grid = json!({"freq": {
            "type": "float", "size": 4, "value": 1.25, "min": 0.0, "step": 0.5
        }});
        assert!(format!("{}", serialize_value(&off_grid).unwrap_err()).contains("step"));
    }

    #[test]
    fn constraints_accept_legal_values() {
        let data = json!({
            "rate": {"type": "uint", "size": 2, "value": 100, "min": 10, "max": 1000},
            "mode": {"type": "uint", "size": 1, "value": 2, "allowed": [0, 1, 2]},
            "freq": {"type": "float", "size": 4, "value": 1.5, "min": 0.0, "step": 0.5}
        });
        assert!(serialize_value(&data).is_ok());
    }

    #[test]
    fn constraints_apply_per_array_element() {
        let bad = json!({"gains": {
            "type": "array", "size": 8, "element_type": "uint",
            "value": [1, 99], "max": 10
        }});
        assert!(format!("{}", serialize_value(&bad).unwrap_err()).contains("gains[1]"));
    }

    #[test]
    fn constraint_keys_change_no_bytes_and_no_layout_hash() {
        let plain = json!({"rate": {"type": "uint", "size": 2, "value": 100}});
        let constrained =
            json!({"rate": {"type": "uint", "size": 2, "value": 100, "min": 10, "max": 1000}});
        let (b1, h1) = serialize_value_with_layout(&plain).unwrap();
        let (b2, h2) = serialize_value_with_layout(&constrained).unwrap();
        assert_eq!(b1, b2);
        assert_eq!(h1, h2);
    }
}
