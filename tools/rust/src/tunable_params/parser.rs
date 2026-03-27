//! Header parser: read a C++ header and build a best-effort metadata map.

use once_cell::sync::Lazy;
use regex::Regex;
use serde_json::{json, Map as JsonMap, Value as Json};
use std::{
    collections::{BTreeMap, HashSet},
    fs,
    path::Path,
};

use super::{Error, TemplateOptions, AUTOGEN_NOTE};

/* --------------------------- precompiled regexes -------------------------- */

static RE_BLOCK_COMMENTS: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?s)/\*.*?\*/").expect("block comment regex"));

static RE_LINE_COMMENTS: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?m)//.*$").expect("line comment regex"));

static RE_DEFINE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"(?m)^\s*#define\s+(\w+)\s+([\w\.]+)\s*$"#).expect("define regex"));

// Match inline constexpr constants: inline constexpr TYPE NAME = VALUE;
static RE_CONSTEXPR: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r#"(?m)^\s*(?:inline\s+)?constexpr\s+[\w:]+\s+(\w+)\s*=\s*([\w\.x]+)\s*;"#)
        .expect("constexpr regex")
});

// Match struct body, allowing nested braces (for brace initializers like {0})
// Also handles __attribute__((packed)) and similar attributes between } and ;
static RE_STRUCT: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(?s)\bstruct\s+(\w+)\s*\{((?:[^{}]|\{[^}]*\})*)\}(?:\s*__attribute__\s*\(\([^)]*\)\))?\s*;")
        .expect("struct regex")
});

// Match enum class with optional underlying type: enum class Name : Type { ... };
// Captures: 1=name, 2=underlying_type (optional), 3=body
static RE_ENUM_CLASS: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(?s)\benum\s+class\s+(\w+)(?:\s*:\s*([\w:]+))?\s*\{([^}]*)\}\s*;")
        .expect("enum class regex")
});

static RE_FIELD: Lazy<Regex> = Lazy::new(|| {
    Regex::new(
        r#"(?mx)
        ^\s*
        (?:
           (?:const|volatile|mutable|constexpr)\s+ | alignas\s*\([^)]*\)\s+
        )*
        (?:
            std::array\s*<\s*([^,>]+?)\s*,\s*([^>]+?)\s*>\s*([\w\d_]+)   # 1:elem 2:N 3:name
            (?:\s*\{([^}]*)\})?                                          # 4:brace init (captured)
          | ([\w\d_:]+(?:\s+[\w\d_:]+)?(?:\s*\*+|\s*&+)?)\s+([\w\d_]+)   # 5:type 6:name
            (?:\s*\[\s*([\w\d_]+)\s*\])?                                 # 7:N (array dim)
            (?:\s*\{([^}]*)\})?                                          # 8:brace init (captured)
        )
        \s*;\s*$
    "#,
    )
    .expect("field regex")
});

static RE_WS_COALESCE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"\s+"#).expect("whitespace coalesce regex"));

/* --------------------------- public API ----------------------------------- */

/// Read the entire header file into a string.
pub fn read_file(header_path: &Path) -> Result<String, Error> {
    Ok(fs::read_to_string(header_path)?)
}

/// Parse the header content into a metadata JSON value.
pub fn parse_header(content: &str, opts: &TemplateOptions) -> Result<Json, Error> {
    let content = strip_comments(content);

    let mut root = JsonMap::new();
    root.insert("__note__".into(), Json::String(AUTOGEN_NOTE.to_string()));

    let defines = parse_defines(&content)?;
    root.insert("__defines__".into(), defines.clone());

    let raw = collect_structs(&content)?;
    let names: HashSet<String> = raw.keys().cloned().collect();

    for (sname, fields) in &raw {
        // Apply struct filter if specified
        if let Some(ref filter) = opts.struct_filter {
            if sname != filter {
                continue;
            }
        }

        let mut out_fields = Vec::with_capacity(fields.len());
        for rf in fields {
            let meta = field_to_json(rf, &raw, &names, &defines);
            let mut obj = JsonMap::new();
            obj.insert(rf.name.clone(), Json::Object(meta));
            out_fields.push(Json::Object(obj));
        }
        root.insert(sname.clone(), Json::Array(out_fields));
    }

    Ok(Json::Object(root))
}

/* --------------------------- utilities ----------------------------------- */

fn strip_comments(src: &str) -> String {
    // /* ... */ → keep newlines, blank out others
    let tmp = RE_BLOCK_COMMENTS.replace_all(src, |m: &regex::Captures| {
        m[0].chars()
            .map(|c| if c == '\n' { '\n' } else { ' ' })
            .collect::<String>()
    });
    // // ... EOL
    RE_LINE_COMMENTS.replace_all(&tmp, "").to_string()
}

/* --------------------------- #define parsing ------------------------------ */

fn parse_defines(content: &str) -> Result<Json, Error> {
    let mut map = JsonMap::new();

    // Parse #define macros
    for c in RE_DEFINE.captures_iter(content) {
        let name = c[1].to_string();
        let raw = &c[2];
        if let Ok(v) = raw.parse::<i64>() {
            map.insert(name, json!(v));
            continue;
        }
        if let Ok(v) = raw.parse::<u64>() {
            map.insert(name, json!(v));
            continue;
        }
        if let Ok(v) = raw.parse::<f64>() {
            map.insert(name, json!(v));
            continue;
        }
        map.insert(name, json!(raw));
    }

    // Parse inline constexpr constants
    for c in RE_CONSTEXPR.captures_iter(content) {
        let name = c[1].to_string();
        let raw = &c[2];
        // Handle hex values (0x...)
        if raw.starts_with("0x") || raw.starts_with("0X") {
            if let Ok(v) = u64::from_str_radix(&raw[2..], 16) {
                map.insert(name, json!(v));
                continue;
            }
        }
        if let Ok(v) = raw.parse::<i64>() {
            map.insert(name, json!(v));
            continue;
        }
        if let Ok(v) = raw.parse::<u64>() {
            map.insert(name, json!(v));
            continue;
        }
        if let Ok(v) = raw.parse::<f64>() {
            map.insert(name, json!(v));
            continue;
        }
        map.insert(name, json!(raw));
    }

    Ok(Json::Object(map))
}

/* --------------------------- enum parsing --------------------------------- */

/// Parsed enum class information.
#[derive(Debug, Clone)]
pub struct ParsedEnum {
    /// Underlying type if specified (e.g., "std::uint8_t" -> "uint8_t").
    pub underlying_type: Option<String>,
    /// Value mappings: name -> numeric value.
    pub values: BTreeMap<String, i64>,
}

/// Collect all enum class definitions from header content.
pub fn collect_enums(content: &str) -> Result<BTreeMap<String, ParsedEnum>, Error> {
    let content = strip_comments(content);
    let mut out = BTreeMap::new();

    for cap in RE_ENUM_CLASS.captures_iter(&content) {
        let name = cap[1].to_string();
        let underlying = cap.get(2).map(|m| normalize_type(m.as_str()));
        let body = &cap[3];

        out.insert(
            name,
            ParsedEnum {
                underlying_type: underlying,
                values: parse_enum_values(body),
            },
        );
    }

    Ok(out)
}

/// Parse enum value declarations from body text.
/// Uses simple string splitting instead of regex for better performance.
fn parse_enum_values(body: &str) -> BTreeMap<String, i64> {
    let mut values = BTreeMap::new();
    let mut next_value: i64 = 0;

    for part in body.split(',') {
        let part = part.trim();
        if part.is_empty() {
            continue;
        }

        // Parse "NAME = VALUE" or just "NAME"
        let (name, value) = if let Some(eq_pos) = part.find('=') {
            let name = part[..eq_pos].trim();
            let raw_val = part[eq_pos + 1..].trim();
            let val = parse_enum_numeric(raw_val).unwrap_or(next_value);
            (name, val)
        } else {
            // Just a name, no explicit value
            (part.trim(), next_value)
        };

        // Skip if name is empty or not a valid identifier start
        if name.is_empty()
            || !name
                .chars()
                .next()
                .map_or(false, |c| c.is_alphabetic() || c == '_')
        {
            continue;
        }

        values.insert(name.to_string(), value);
        next_value = value + 1;
    }

    values
}

/// Parse numeric value from enum (supports decimal and hex).
#[inline]
fn parse_enum_numeric(raw: &str) -> Option<i64> {
    let raw = raw.trim();
    if let Some(hex) = raw.strip_prefix("0x").or_else(|| raw.strip_prefix("0X")) {
        i64::from_str_radix(hex, 16).ok()
    } else {
        raw.parse().ok()
    }
}

/* --------------------------- struct parsing ------------------------------- */

#[derive(Debug)]
struct RawField {
    ty: String,
    name: String,
    // optional [N] after name
    bracket_n: Option<String>,
    // for std::array<Elem,N>
    std_elem: Option<String>,
    std_n: Option<String>,
    // brace initializer value (e.g., "1.0" from {1.0})
    default_value: Option<String>,
}

fn collect_structs(content: &str) -> Result<BTreeMap<String, Vec<RawField>>, Error> {
    let mut out: BTreeMap<String, Vec<RawField>> = BTreeMap::new();

    for s in RE_STRUCT.captures_iter(content) {
        let name = s[1].to_string();
        let body = s[2].replace(';', ";\n"); // normalize semicolon-packed lines
        let mut fields = Vec::new();

        for line in body.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            if let Some(c) = RE_FIELD.captures(line) {
                if c.get(1).is_some() {
                    // std::array<Elem,N> name {init};
                    // Groups: 1=elem, 2=N, 3=name, 4=init
                    fields.push(RawField {
                        ty: "std::array".into(),
                        name: c.get(3).unwrap().as_str().into(),
                        bracket_n: None,
                        std_elem: Some(c.get(1).unwrap().as_str().trim().into()),
                        std_n: Some(c.get(2).unwrap().as_str().trim().into()),
                        default_value: c.get(4).map(|m| m.as_str().trim().into()),
                    });
                } else {
                    // Plain/ptr/ref type
                    // Groups: 5=type, 6=name, 7=N (array dim), 8=init
                    fields.push(RawField {
                        ty: c.get(5).unwrap().as_str().trim().into(),
                        name: c.get(6).unwrap().as_str().into(),
                        bracket_n: c.get(7).map(|m| m.as_str().into()),
                        std_elem: None,
                        std_n: None,
                        default_value: c.get(8).map(|m| m.as_str().trim().into()),
                    });
                }
                continue;
            }
            // ultra-simple fallback: "type name" or "type name[N]"
            if let Some((ty, nm, n, init)) = simple_field_fallback(line) {
                fields.push(RawField {
                    ty,
                    name: nm,
                    bracket_n: n,
                    std_elem: None,
                    std_n: None,
                    default_value: init,
                });
            }
        }
        out.insert(name, fields);
    }
    Ok(out)
}

fn simple_field_fallback(line: &str) -> Option<(String, String, Option<String>, Option<String>)> {
    if line.contains("std::array") {
        return None;
    }
    let mut parts = line.split_whitespace();
    let ty = parts.next()?.trim();
    let rest = parts.next()?.trim();
    if rest.is_empty() {
        return None;
    }
    let (name, n) = if let (Some(lb), Some(rb)) = (rest.find('['), rest.rfind(']')) {
        (
            rest[..lb].to_string(),
            Some(rest[lb + 1..rb].trim().to_string()),
        )
    } else {
        (rest.to_string(), None)
    };
    // Extract brace initializer {value} from name
    let (name, init) = if let (Some(lb), Some(rb)) = (name.find('{'), name.rfind('}')) {
        let init_str = name[lb + 1..rb].trim().to_string();
        let init = if init_str.is_empty() {
            None
        } else {
            Some(init_str)
        };
        (name[..lb].trim().to_string(), init)
    } else {
        (name, None)
    };
    Some((ty.to_string(), name, n, init))
}

/* --------------------------- resolution (single path) ---------------------- */

/// Parse a C++ brace initializer value into a JSON value.
fn parse_default_value(init: Option<&str>, logical_type: &str) -> Json {
    let Some(raw) = init else { return Json::Null };
    let raw = raw.trim();
    if raw.is_empty() {
        return Json::Null;
    }

    // Handle string literals (strip quotes)
    if raw.starts_with('"') && raw.ends_with('"') && raw.len() >= 2 {
        return json!(raw[1..raw.len() - 1].to_string());
    }

    // Handle char literals
    if raw.starts_with('\'') && raw.ends_with('\'') && raw.len() >= 2 {
        return json!(raw[1..raw.len() - 1].to_string());
    }

    // Handle bool
    if raw == "true" {
        return json!(true);
    }
    if raw == "false" {
        return json!(false);
    }

    // Handle numeric types
    match logical_type {
        "float" => {
            // Handle negative numbers
            if let Ok(v) = raw.parse::<f64>() {
                return json!(v);
            }
        }
        "int" => {
            // Handle hex (0x...) and decimal
            if raw.starts_with("0x") || raw.starts_with("0X") {
                if let Ok(v) = i64::from_str_radix(&raw[2..], 16) {
                    return json!(v);
                }
            } else if let Ok(v) = raw.parse::<i64>() {
                return json!(v);
            }
        }
        "uint" => {
            // Handle hex (0x...) and decimal
            if raw.starts_with("0x") || raw.starts_with("0X") {
                if let Ok(v) = u64::from_str_radix(&raw[2..], 16) {
                    return json!(v);
                }
            } else if let Ok(v) = raw.parse::<u64>() {
                return json!(v);
            }
        }
        "bool" => {
            if raw == "1" {
                return json!(true);
            }
            if raw == "0" {
                return json!(false);
            }
        }
        _ => {}
    }

    // Fallback: try numeric parsing
    if let Ok(v) = raw.parse::<i64>() {
        return json!(v);
    }
    if let Ok(v) = raw.parse::<f64>() {
        return json!(v);
    }

    // Last resort: return as string
    json!(raw.to_string())
}

fn field_to_json(
    rf: &RawField,
    all: &BTreeMap<String, Vec<RawField>>,
    names: &HashSet<String>,
    defines: &Json,
) -> JsonMap<String, Json> {
    let mut m = JsonMap::new();

    // std::array<T,N>
    if rf.ty.starts_with("std::array") {
        if let (Some(elem), Some(n_tok)) = (&rf.std_elem, &rf.std_n) {
            let (logical, sz) = determine_type(elem.trim());
            let n = resolve_dim(defines, n_tok);
            m.insert("type".into(), json!("array"));
            m.insert("element_type".into(), json!(logical));
            m.insert("dims".into(), json!([n]));
            m.insert("size".into(), json!(sz.unwrap_or(0) * (n as usize)));
            // Arrays: use empty array as default if no init, otherwise try to preserve init
            let default_val =
                if rf.default_value.is_some() && !rf.default_value.as_ref().unwrap().is_empty() {
                    json!(rf.default_value.as_ref().unwrap())
                } else {
                    json!([])
                };
            m.insert("value".into(), default_val);
            return m;
        }
    }

    // char[N] → string
    if is_char(&rf.ty) && rf.bracket_n.is_some() {
        let n = resolve_dim(defines, rf.bracket_n.as_ref().unwrap());
        m.insert("type".into(), json!("string"));
        m.insert("size".into(), json!(n));
        // Use parsed default or empty string
        let default_val = parse_default_value(rf.default_value.as_deref(), "string");
        m.insert(
            "value".into(),
            if default_val.is_null() {
                json!("")
            } else {
                default_val
            },
        );
        return m;
    }

    // T[N]
    if let Some(n_tok) = &rf.bracket_n {
        let n = resolve_dim(defines, n_tok);
        let (logical, sz) = determine_type(&rf.ty);
        m.insert("type".into(), json!("array"));
        m.insert("element_type".into(), json!(logical));
        m.insert("dims".into(), json!([n]));
        m.insert("size".into(), json!(sz.unwrap_or(0) * (n as usize)));
        // Arrays: use empty array as default
        let default_val =
            if rf.default_value.is_some() && !rf.default_value.as_ref().unwrap().is_empty() {
                json!(rf.default_value.as_ref().unwrap())
            } else {
                json!([])
            };
        m.insert("value".into(), default_val);
        return m;
    }

    // Nested struct by name
    if names.contains(&rf.ty) {
        if let Some(nested) = all.get(&rf.ty) {
            let mut nested_json = Vec::with_capacity(nested.len());
            let mut total = 0usize;
            for nf in nested {
                let meta = field_to_json(nf, all, names, defines);
                let mut obj = JsonMap::new();
                obj.insert(nf.name.clone(), Json::Object(meta.clone()));
                total += meta.get("size").and_then(as_usize).unwrap_or(0);
                nested_json.push(Json::Object(obj));
            }
            m.insert("type".into(), json!("struct"));
            m.insert("fields".into(), Json::Array(nested_json));
            m.insert("size".into(), json!(total));
            return m;
        }
    }

    // Pointers/refs → unknown
    if is_ptr_ref(&rf.ty) {
        m.insert("type".into(), json!("unknown"));
        m.insert("size".into(), json!(0));
        m.insert("value".into(), Json::Null);
        return m;
    }

    // Plain primitive/alias or unknown
    let (logical, sz) = determine_type(&rf.ty);
    m.insert("type".into(), json!(logical));
    m.insert("size".into(), json!(sz.unwrap_or(0)));
    m.insert(
        "value".into(),
        parse_default_value(rf.default_value.as_deref(), &logical),
    );
    m
}

/* --------------------------- helpers -------------------------------------- */

fn is_char(ty: &str) -> bool {
    ty.trim() == "char"
}
fn is_ptr_ref(ty: &str) -> bool {
    ty.contains('*') || ty.contains('&')
}

fn as_usize(v: &Json) -> Option<usize> {
    match v {
        Json::Number(n) => n
            .as_u64()
            .map(|u| u as usize)
            .or_else(|| n.as_i64().map(|i| i.max(0) as usize)),
        _ => None,
    }
}

fn resolve_dim(defines: &Json, token: &str) -> u64 {
    if let Ok(v) = token.parse::<u64>() {
        return v;
    }
    if let Json::Object(map) = defines {
        if let Some(v) = map.get(token) {
            if let Some(u) = v.as_u64() {
                return u;
            }
            if let Some(i) = v.as_i64() {
                return i.max(0) as u64;
            }
        }
    }
    0
}

// Map C++-ish field types to (logical_name, size_bytes_if_known).
fn determine_type(field_type: &str) -> (String, Option<usize>) {
    let t = normalize_type(field_type);
    use std::borrow::Cow;
    fn o(n: usize) -> Option<usize> {
        Some(n)
    }
    let (logical, sz): (Cow<'_, str>, Option<usize>) = match t.as_str() {
        // fixed-width
        "uint8_t" => ("uint".into(), o(1)),
        "uint16_t" => ("uint".into(), o(2)),
        "uint32_t" => ("uint".into(), o(4)),
        "uint64_t" => ("uint".into(), o(8)),
        "int8_t" => ("int".into(), o(1)),
        "int16_t" => ("int".into(), o(2)),
        "int32_t" => ("int".into(), o(4)),
        "int64_t" => ("int".into(), o(8)),
        // common aliases
        "unsigned" | "unsigned int" | "uint" => ("uint".into(), o(4)),
        "int" | "signed" | "signed int" => ("int".into(), o(4)),
        "float" => ("float".into(), o(4)),
        "double" => ("float".into(), o(8)),
        "bool" => ("bool".into(), o(1)),
        "char" => ("char".into(), o(1)),
        "std::string" | "string" => ("string".into(), None), // dynamic
        // best-effort
        "size_t" => ("uint".into(), o(8)),
        "ptrdiff_t" => ("int".into(), o(8)),
        other => (Cow::from(other), None),
    };
    (logical.into_owned(), sz)
}

fn normalize_type(ty: &str) -> String {
    let mut t = ty
        .trim()
        .trim_end_matches('*')
        .trim_end_matches('&')
        .trim()
        .to_string();
    t = RE_WS_COALESCE.replace_all(&t, " ").to_string();
    // Handle std:: prefix for common types
    if t == "std :: string" || t == "std:: string" {
        return "std::string".into();
    }
    // Strip std:: prefix for fixed-width types (std::uint32_t -> uint32_t)
    if t.starts_with("std::") {
        t = t.strip_prefix("std::").unwrap().to_string();
    }
    t
}

/* -------------------------------- tests ----------------------------------- */

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn parses_defines_only() {
        let src = r#"
            // comment
            #define FOO 1
            #define BAR 42
            /* block
               comment */
            #define NAME HelloWorld
            #define PI 3.14
        "#;

        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let defs = &meta["__defines__"];

        assert_eq!(defs["FOO"], json!(1));
        assert_eq!(defs["BAR"], json!(42));
        assert_eq!(defs["NAME"], json!("HelloWorld"));
        let expected = 314.0_f64 / 100.0;
        assert_eq!(defs["PI"], json!(expected));
        assert_eq!(meta["__note__"], json!(AUTOGEN_NOTE));
    }

    #[test]
    fn parses_empty_file() {
        let src = "";
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        assert!(meta.get("__defines__").is_some());
        // Only __note__ and __defines__ at root
        assert_eq!(meta.as_object().unwrap().len(), 2);
    }

    #[test]
    fn parses_structs_with_macros_and_arrays() {
        let src = r#"
            #define N 16
            struct FixedArrayStruct {
                char description[N];
                std::array<int32_t, 5> fixedArray;
                uint8_t flags[4];
            };
        "#;

        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let s = meta.get("FixedArrayStruct").unwrap().as_array().unwrap();

        // description → string of size N
        let desc = &s[0]["description"];
        assert_eq!(desc["type"], json!("string"));
        assert_eq!(desc["size"], json!(16));

        // fixedArray → array<int, 5> (int32_t → 4 bytes)
        let fa = &s[1]["fixedArray"];
        assert_eq!(fa["type"], json!("array"));
        assert_eq!(fa["element_type"], json!("int"));
        assert_eq!(fa["dims"], json!([5]));
        assert_eq!(fa["size"], json!(20));

        // flags → uint8_t[4]
        let flags = &s[2]["flags"];
        assert_eq!(flags["type"], json!("array"));
        assert_eq!(flags["element_type"], json!("uint"));
        assert_eq!(flags["dims"], json!([4]));
        assert_eq!(flags["size"], json!(4));
    }

    #[test]
    fn parses_nested_structs_and_qualifiers() {
        let src = r#"
            #define LEN 8
            struct Inner {
                uint8_t x;
                char label[LEN];
            };

            struct Outer {
                const uint32_t a;
                Inner nested;
                volatile int32_t c[2];
                std::array<uint8_t, 4> b;
                float* ignored_ptr;
            };
        "#;

        let meta = parse_header(src, &TemplateOptions::default()).unwrap();

        // Inner checks
        let inner = meta.get("Inner").unwrap().as_array().unwrap();
        assert_eq!(inner[0]["x"]["type"], json!("uint"));
        assert_eq!(inner[0]["x"]["size"], json!(1));
        assert_eq!(inner[1]["label"]["type"], json!("string"));
        assert_eq!(inner[1]["label"]["size"], json!(8));

        // Outer checks (ordered)
        let outer = meta.get("Outer").unwrap().as_array().unwrap();

        let a = &outer[0]["a"];
        assert_eq!(a["type"], json!("uint"));
        assert_eq!(a["size"], json!(4));

        let nested = &outer[1]["nested"];
        assert_eq!(nested["type"], json!("struct"));
        assert!(nested["size"].as_u64().unwrap() >= 1);

        let c = &outer[2]["c"];
        assert_eq!(c["type"], json!("array"));
        assert_eq!(c["element_type"], json!("int"));
        assert_eq!(c["dims"], json!([2]));
        assert_eq!(c["size"], json!(8));

        let b = &outer[3]["b"];
        assert_eq!(b["type"], json!("array"));
        assert_eq!(b["element_type"], json!("uint"));
        assert_eq!(b["dims"], json!([4]));
        assert_eq!(b["size"], json!(4));

        let ignored = &outer[4]["ignored_ptr"];
        assert_eq!(ignored["type"], json!("unknown"));
    }

    #[test]
    fn recognizes_common_aliases() {
        let src = r#"
            struct Aliases {
                unsigned int u;
                int s;
                double d;
                bool ok;
                char ch;
            };
        "#;

        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let a = meta.get("Aliases").unwrap().as_array().unwrap();

        assert_eq!(a[0]["u"]["type"], json!("uint"));
        assert_eq!(a[0]["u"]["size"], json!(4));

        assert_eq!(a[1]["s"]["type"], json!("int"));
        assert_eq!(a[1]["s"]["size"], json!(4));

        assert_eq!(a[2]["d"]["type"], json!("float"));
        assert_eq!(a[2]["d"]["size"], json!(8));

        assert_eq!(a[3]["ok"]["type"], json!("bool"));
        assert_eq!(a[3]["ok"]["size"], json!(1));

        assert_eq!(a[4]["ch"]["type"], json!("char"));
        assert_eq!(a[4]["ch"]["size"], json!(1));
    }

    #[test]
    fn pointers_and_references_are_unknown() {
        let src = r#"
            struct P {
                int* p;
                double& r;
                const char* s;
            };
        "#;
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let p = meta.get("P").unwrap().as_array().unwrap();

        assert_eq!(p[0]["p"]["type"], json!("unknown"));
        assert_eq!(p[1]["r"]["type"], json!("unknown"));
        assert_eq!(p[2]["s"]["type"], json!("unknown"));
    }

    #[test]
    fn std_string_and_namespaced_types() {
        let src = r#"
            namespace ns { struct Dummy { int z; }; }
            struct T {
                std::string s;
                ns::Dummy d;
            };
        "#;
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let t = meta.get("T").unwrap().as_array().unwrap();

        // std::string recognized as string (size unknown => 0)
        assert_eq!(t[0]["s"]["type"], json!("string"));

        // namespaced type that isn't collected as a struct stays as passthrough logical type
        assert_eq!(t[1]["d"]["type"], json!("ns::Dummy"));
        assert_eq!(t[1]["d"]["size"], json!(0));
    }

    #[test]
    fn std_array_with_macro_dimension() {
        let src = r#"
            #define CNT 3
            struct A {
                std::array<uint16_t, CNT> v;
            };
        "#;
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let a = meta.get("A").unwrap().as_array().unwrap();
        let v = &a[0]["v"];
        assert_eq!(v["type"], json!("array"));
        assert_eq!(v["element_type"], json!("uint"));
        assert_eq!(v["dims"], json!([3]));
        assert_eq!(v["size"], json!(2 * 3));
    }

    #[test]
    fn undefined_macro_dimension_resolves_to_zero() {
        let src = r#"
            struct B {
                int arr[NOT_DEFINED];
            };
        "#;
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();
        let b = meta.get("B").unwrap().as_array().unwrap();
        let arr = &b[0]["arr"];
        assert_eq!(arr["type"], json!("array"));
        assert_eq!(arr["dims"], json!([0])); // unresolved macro -> 0
        assert_eq!(arr["size"], json!(0)); // 0 * elem_size
    }

    #[test]
    fn nested_struct_size_is_sum_of_fields() {
        let src = r#"
            struct Inner { uint8_t a; int32_t b; };
            struct Outer { Inner x; uint16_t y; };
        "#;
        let meta = parse_header(src, &TemplateOptions::default()).unwrap();

        let inner = meta.get("Inner").unwrap().as_array().unwrap();
        let inner_a = inner[0]["a"]["size"].as_u64().unwrap();
        let inner_b = inner[1]["b"]["size"].as_u64().unwrap();
        let expected_inner = inner_a + inner_b;

        let outer = meta.get("Outer").unwrap().as_array().unwrap();
        let x = &outer[0]["x"];
        assert_eq!(x["type"], json!("struct"));
        assert_eq!(x["size"], json!(expected_inner as usize));

        let y = &outer[1]["y"];
        assert_eq!(y["type"], json!("uint"));
        assert_eq!(y["size"], json!(2));
    }

    /* --------------------------- enum parsing tests ---------------------------- */

    #[test]
    fn parses_enum_class_with_underlying_type() {
        let src = r#"
            enum class RTMode : std::uint8_t {
                HARD_TICK_COMPLETE = 0,
                HARD_PERIOD_COMPLETE = 1,
                SOFT_SKIP_ON_BUSY = 2,
                SOFT_LAG_TOLERANT = 3,
                SOFT_LOG_ONLY = 4,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        assert_eq!(enums.len(), 1);
        assert!(enums.contains_key("RTMode"));

        let rtmode = &enums["RTMode"];
        assert_eq!(rtmode.underlying_type, Some("uint8_t".to_string()));
        assert_eq!(rtmode.values.len(), 5);
        assert_eq!(rtmode.values["HARD_TICK_COMPLETE"], 0);
        assert_eq!(rtmode.values["HARD_PERIOD_COMPLETE"], 1);
        assert_eq!(rtmode.values["SOFT_LOG_ONLY"], 4);
    }

    #[test]
    fn parses_enum_class_without_underlying_type() {
        let src = r#"
            enum class ComponentType {
                EXECUTIVE = 0,
                CORE = 1,
                SW_MODEL = 2,
                HW_MODEL = 3,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        assert_eq!(enums.len(), 1);
        assert!(enums.contains_key("ComponentType"));

        let ctype = &enums["ComponentType"];
        assert!(ctype.underlying_type.is_none());
        assert_eq!(ctype.values.len(), 4);
        assert_eq!(ctype.values["EXECUTIVE"], 0);
        assert_eq!(ctype.values["HW_MODEL"], 3);
    }

    #[test]
    fn parses_enum_class_with_hex_values() {
        let src = r#"
            enum class Opcode : uint16_t {
                GET_HEALTH = 0x0100,
                EXEC_NOOP = 0x0101,
                GET_STATUS = 0x0102,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        let opcode = &enums["Opcode"];
        assert_eq!(opcode.values["GET_HEALTH"], 0x0100);
        assert_eq!(opcode.values["EXEC_NOOP"], 0x0101);
        assert_eq!(opcode.values["GET_STATUS"], 0x0102);
    }

    #[test]
    fn parses_enum_class_auto_increment() {
        let src = r#"
            enum class State {
                INIT,
                RUNNING,
                PAUSED,
                STOPPED,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        let state = &enums["State"];
        assert_eq!(state.values["INIT"], 0);
        assert_eq!(state.values["RUNNING"], 1);
        assert_eq!(state.values["PAUSED"], 2);
        assert_eq!(state.values["STOPPED"], 3);
    }

    #[test]
    fn parses_enum_class_mixed_auto_and_explicit() {
        let src = r#"
            enum class Mixed {
                A,
                B = 10,
                C,
                D = 20,
                E,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        let mixed = &enums["Mixed"];
        assert_eq!(mixed.values["A"], 0);
        assert_eq!(mixed.values["B"], 10);
        assert_eq!(mixed.values["C"], 11);
        assert_eq!(mixed.values["D"], 20);
        assert_eq!(mixed.values["E"], 21);
    }

    #[test]
    fn parses_multiple_enums() {
        let src = r#"
            enum class First { X = 1, Y = 2 };
            struct Interleaved { int x; };
            enum class Second { A, B, C };
        "#;

        let enums = collect_enums(src).unwrap();
        assert_eq!(enums.len(), 2);
        assert!(enums.contains_key("First"));
        assert!(enums.contains_key("Second"));
        assert_eq!(enums["First"].values["X"], 1);
        assert_eq!(enums["Second"].values["C"], 2);
    }

    #[test]
    fn ignores_comments_in_enums() {
        let src = r#"
            enum class WithComments {
                // This is a comment
                VALUE_A = 1,
                /* Block comment */
                VALUE_B = 2,
            };
        "#;

        let enums = collect_enums(src).unwrap();
        let e = &enums["WithComments"];
        assert_eq!(e.values.len(), 2);
        assert_eq!(e.values["VALUE_A"], 1);
        assert_eq!(e.values["VALUE_B"], 2);
    }
}
