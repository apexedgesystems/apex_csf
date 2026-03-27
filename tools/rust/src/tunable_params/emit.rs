//! Emit helpers for writing metadata as JSON or TOML.
//!
//! The input `Metadata` is a `serde_json::Value` map produced by the parser.
//! We pretty-print JSON, and for TOML we convert JSON values recursively into
//! `toml_edit` items (arrays, tables, arrays-of-tables) with a best-effort
//! mapping suitable for hand-editing.

use std::fs;
use std::io::{BufWriter, Write};
use std::path::Path;

use toml_edit::{Array, ArrayOfTables, DocumentMut, Item, Table, Value as TomlValue};

use super::{Error, Metadata};

/// Write `metadata` to `out_path` in JSON format.
pub(crate) fn write_json(metadata: &Metadata, out_path: &Path) -> Result<(), Error> {
    let file = fs::File::create(out_path)?;
    let mut w = BufWriter::new(file);
    serde_json::to_writer_pretty(&mut w, metadata).map_err(|e| Error::Emit(e.to_string()))?;
    w.flush().map_err(Error::Io)
}

/// Write `metadata` to `out_path` in TOML format.
///
/// Best-effort conversion from JSON:
/// - JSON objects -> TOML tables
/// - JSON arrays of objects -> TOML arrays of tables
/// - JSON arrays of primitives -> TOML arrays
/// - JSON null -> empty string (placeholder)
pub(crate) fn write_toml(metadata: &Metadata, out_path: &Path) -> Result<(), Error> {
    let mut doc = DocumentMut::new();
    let item = json_to_toml_item(metadata).map_err(Error::Emit)?;

    // We expect root to be a table; if not, wrap under "root".
    match item {
        Item::Table(t) => {
            for (k, v) in t.iter() {
                doc[k] = v.clone();
            }
        }
        Item::ArrayOfTables(aot) => {
            doc["root"] = Item::ArrayOfTables(aot);
        }
        other => {
            doc["root"] = other;
        }
    }

    let file = fs::File::create(out_path)?;
    let mut w = BufWriter::new(file);
    w.write_all(doc.to_string().as_bytes()).map_err(Error::Io)?;
    w.flush().map_err(Error::Io)
}

// --- JSON -> TOML conversion -------------------------------------------------

fn json_to_toml_item(v: &Metadata) -> Result<Item, String> {
    Ok(match v {
        serde_json::Value::Null => Item::Value(TomlValue::from("")),
        serde_json::Value::Bool(b) => Item::Value(TomlValue::from(*b)),
        serde_json::Value::Number(n) => num_to_toml(n)?,
        serde_json::Value::String(s) => Item::Value(TomlValue::from(s.clone())),
        serde_json::Value::Array(arr) => array_to_toml(arr)?,
        serde_json::Value::Object(_) => object_to_table(v)?,
    })
}

fn num_to_toml(n: &serde_json::Number) -> Result<Item, String> {
    if let Some(i) = n.as_i64() {
        return Ok(Item::Value(TomlValue::from(i)));
    }
    if let Some(u) = n.as_u64() {
        // toml_edit stores integers as i64; preserve values > i64::MAX as strings
        if u <= i64::MAX as u64 {
            return Ok(Item::Value(TomlValue::from(u as i64)));
        }
        return Ok(Item::Value(TomlValue::from(u.to_string())));
    }
    if let Some(f) = n.as_f64() {
        return Ok(Item::Value(TomlValue::from(f)));
    }
    Err("unsupported number representation".into())
}

fn array_to_toml(arr: &[serde_json::Value]) -> Result<Item, String> {
    // Empty JSON array -> TOML empty array
    if arr.is_empty() {
        return Ok(Item::Value(TomlValue::Array(Array::default())));
    }

    if arr.iter().all(|e| e.is_object()) {
        // Collapse parser-style struct fields only if non-empty and shaped as {"name": {...}}
        if is_field_array(arr) {
            let mut tbl = Table::new();
            for obj in arr {
                let map = obj.as_object().unwrap();
                let (k, v) = map.iter().next().unwrap();
                tbl[k] = json_to_toml_item(v)?;
            }
            return Ok(Item::Table(tbl));
        }

        // Generic array of objects -> array of tables
        let mut aot = ArrayOfTables::new();
        for obj in arr {
            let Item::Table(t) = object_to_table(obj)? else {
                return Err("expected object to convert to table".into());
            };
            aot.push(t);
        }
        return Ok(Item::ArrayOfTables(aot));
    }

    // Primitive / mixed arrays -> plain TOML array (best effort)
    let mut a = Array::default();
    for e in arr {
        match json_to_toml_item(e)? {
            Item::Value(val) => a.push(val),
            other => a.push(TomlValue::from(other.to_string())),
        }
    }
    Ok(Item::Value(TomlValue::Array(a)))
}

fn is_field_array(arr: &[serde_json::Value]) -> bool {
    // Must be non-empty and each element must be {"fieldName": { ...object... }}
    arr.iter().all(|elem| {
        elem.as_object()
            .and_then(|o| {
                if o.len() != 1 {
                    return None;
                }
                o.values().next()
            })
            .map(|v| v.is_object())
            .unwrap_or(false)
    })
}

fn object_to_table(v: &Metadata) -> Result<Item, String> {
    let mut table = Table::new();
    let obj = v.as_object().ok_or("expected object")?;
    // Preserve insertion order as provided by upstream serializer where possible.
    for (k, val) in obj {
        table[k] = json_to_toml_item(val)?;
    }
    Ok(Item::Table(table))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::fs;
    use tempfile::tempdir;

    // ---------- JSON tests ----------

    #[test]
    fn write_json_valid_data() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("out.json");
        let data: Metadata = json!({
            "key1": "value1",
            "key2": 123,
            "key3": ["a","b","c"],
            "key4": { "nested_key": "nested_value" }
        });
        write_json(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let roundtrip: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(roundtrip, data);
    }

    #[test]
    fn write_json_empty_data() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("empty.json");
        let data: Metadata = json!({});
        write_json(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let v: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(v, data);
    }

    #[test]
    fn write_json_overwrite_existing() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("overwrite.json");

        write_json(&json!({"key":"v1"}), &path).unwrap();
        write_json(&json!({"key":"v2"}), &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let v: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(v, json!({"key":"v2"}));
    }

    #[test]
    fn write_json_invalid_path() {
        // Very likely invalid on Linux; if this flakes on some systems,
        // feel free to guard with cfg or adjust the path.
        let path = Path::new("/invalid_path/output.json");
        let err = write_json(&json!({"key":"value"}), path).unwrap_err();
        match err {
            Error::Io(_) => {} // expected
            other => panic!("expected Error::Io, got {:?}", other),
        }
    }

    // ---------- TOML tests ----------

    #[test]
    fn write_toml_basic_flat() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("basic.toml");

        let data: Metadata = json!({
            "key1": "value1",
            "key2": 42,
            "key3": true
        });

        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        assert_eq!(parsed.get("key1").unwrap().as_str().unwrap(), "value1");
        assert_eq!(parsed.get("key2").unwrap().as_integer().unwrap(), 42);
        assert!(parsed.get("key3").unwrap().as_bool().unwrap());
    }

    #[test]
    fn write_toml_nested_structures() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("nested.toml");

        let data: Metadata = json!({
            "struct": {
                "field1": { "type": "int", "size": 4, "value": 123 },
                "field2": { "type": "string", "size": 10, "value": "example" }
            }
        });

        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        let st = parsed.get("struct").unwrap();
        assert_eq!(
            st.get("field1")
                .unwrap()
                .get("type")
                .unwrap()
                .as_str()
                .unwrap(),
            "int"
        );
        assert_eq!(
            st.get("field1")
                .unwrap()
                .get("size")
                .unwrap()
                .as_integer()
                .unwrap(),
            4
        );
        assert_eq!(
            st.get("field1")
                .unwrap()
                .get("value")
                .unwrap()
                .as_integer()
                .unwrap(),
            123
        );

        assert_eq!(
            st.get("field2")
                .unwrap()
                .get("type")
                .unwrap()
                .as_str()
                .unwrap(),
            "string"
        );
        assert_eq!(
            st.get("field2")
                .unwrap()
                .get("size")
                .unwrap()
                .as_integer()
                .unwrap(),
            10
        );
        assert_eq!(
            st.get("field2")
                .unwrap()
                .get("value")
                .unwrap()
                .as_str()
                .unwrap(),
            "example"
        );
    }

    #[test]
    fn write_toml_arrays_and_metadata() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("array.toml");

        let data: Metadata = json!({
            "array_field": {
                "type": "array",
                "element_type": "int",
                "dims": [2, 3],
                "size": 24,
                "value": [[1,2,3],[4,5,6]]
            }
        });

        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        let af = parsed.get("array_field").unwrap();
        assert_eq!(af.get("type").unwrap().as_str().unwrap(), "array");
        assert_eq!(af.get("element_type").unwrap().as_str().unwrap(), "int");
        assert_eq!(af.get("dims").unwrap().as_array().unwrap().len(), 2);
        assert_eq!(af.get("size").unwrap().as_integer().unwrap(), 24);

        let vv = af.get("value").unwrap().as_array().unwrap();
        assert_eq!(vv.len(), 2);
        assert_eq!(
            vv[0].as_array().unwrap(),
            &vec![
                toml::Value::from(1),
                toml::Value::from(2),
                toml::Value::from(3)
            ]
        );
        assert_eq!(
            vv[1].as_array().unwrap(),
            &vec![
                toml::Value::from(4),
                toml::Value::from(5),
                toml::Value::from(6)
            ]
        );
    }

    #[test]
    fn write_toml_array_of_tables() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("aot.toml");

        let data: Metadata = json!({
            "items": [
                {"a": 1, "b": "x"},
                {"a": 2, "b": "y"}
            ]
        });

        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();
        let items = parsed.get("items").unwrap().as_array().unwrap();

        // Each element should be a table (array of tables)
        assert_eq!(items.len(), 2);
        assert_eq!(items[0].get("a").unwrap().as_integer().unwrap(), 1);
        assert_eq!(items[0].get("b").unwrap().as_str().unwrap(), "x");
        assert_eq!(items[1].get("a").unwrap().as_integer().unwrap(), 2);
        assert_eq!(items[1].get("b").unwrap().as_str().unwrap(), "y");
    }

    #[test]
    fn write_toml_null_placeholder() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("null.toml");

        let data: Metadata = json!({ "k": null });
        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        // We convert null -> empty string placeholder
        assert_eq!(parsed.get("k").unwrap().as_str().unwrap(), "");
    }

    #[test]
    fn write_toml_large_u64_becomes_string() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("big.toml");

        let big = (i64::MAX as u64) + 1;
        let data: Metadata = json!({ "big": big });

        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        // Stored as string to avoid i64 overflow inside toml_edit
        assert_eq!(
            parsed.get("big").unwrap().as_str().unwrap(),
            big.to_string()
        );
    }

    #[test]
    fn write_toml_root_wrapped_for_non_table() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("rootwrap.toml");

        // Root is an array -> should be placed under "root"
        let data: Metadata = json!(["a", "b"]);
        write_toml(&data, &path).unwrap();

        let s = fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        let root = parsed.get("root").unwrap().as_array().unwrap();
        assert_eq!(root.len(), 2);
        assert_eq!(root[0].as_str().unwrap(), "a");
        assert_eq!(root[1].as_str().unwrap(), "b");
    }

    #[test]
    fn write_toml_invalid_path() {
        let path = Path::new("/invalid_path/output.toml");
        let err = write_toml(&json!({"key":"value"}), path).unwrap_err();
        match err {
            Error::Io(_) => {} // expected
            other => panic!("expected Error::Io, got {:?}", other),
        }
    }

    #[test]
    fn write_toml_preserves_empty_arrays() {
        use serde_json::json;
        let dir = tempdir().unwrap();
        let path = dir.path().join("empty_arrays.toml");

        // Mimic parser output where an array field has an empty value
        let data: Metadata = json!({
            "S": [{
                "boolField": {
                    "type": "array",
                    "element_type": "bool",
                    "dims": [2],
                    "size": 2,
                    "value": []  // <-- the important part
                }
            }]
        });

        write_toml(&data, &path).unwrap();
        let s = std::fs::read_to_string(&path).unwrap();
        let parsed: toml::Value = toml::from_str(&s).unwrap();

        // S is a table of fields (collapsed from array-of-one)
        let s_tbl = parsed.get("S").unwrap().as_table().unwrap();
        let bf = s_tbl.get("boolField").unwrap().as_table().unwrap();

        assert_eq!(bf.get("type").unwrap().as_str().unwrap(), "array");
        assert_eq!(bf.get("element_type").unwrap().as_str().unwrap(), "bool");
        assert_eq!(
            bf.get("dims").unwrap().as_array().unwrap()[0]
                .as_integer()
                .unwrap(),
            2
        );
        assert_eq!(bf.get("size").unwrap().as_integer().unwrap(), 2);

        // And crucially: value is an empty TOML array, not an empty table
        let v = bf.get("value").unwrap().as_array().unwrap();
        assert!(v.is_empty());
    }
}
