//! tprm_template: generate JSON/TOML templates from C++ headers or JSON struct dictionaries.
//!
//! Parses C++ headers or reads JSON struct dictionaries (from apex_data_gen) and generates
//! TOML/JSON templates with field type/size/value info for TPRM configuration authoring.
//!
//! Usage (header mode):
//!   tprm_template --header MyConfig.hpp --format toml
//!   tprm_template --header MyConfig.hpp --format toml --struct MyTunableParams
//!   tprm_template --header MyConfig.hpp --format json --output custom_name.json
//!
//! Usage (JSON mode - new):
//!   tprm_template --json apex_data_db/Component.json --struct ComponentTunableParams
//!   tprm_template --json apex_data_db/Component.json --struct ComponentTunableParams -o config.toml

use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::Parser;
use serde_json::Value as Json;
use toml_edit::{Array, DocumentMut, Item, Table, Value as TomlValue};

use apex_rust_tools::tunable_params::{self, Format, TemplateOptions};

/* ----------------------------- CLI Definition ------------------------------ */

#[derive(Parser, Debug)]
#[command(name = "tprm_template")]
#[command(about = "Generate JSON/TOML templates from C++ headers or JSON struct dictionaries.")]
struct Cli {
    /// Path to C++ header file (.hpp or .h)
    #[arg(long, value_name = "PATH", conflicts_with = "json")]
    header: Option<PathBuf>,

    /// Path to JSON struct dictionary file (from apex_data_gen)
    #[arg(long, value_name = "PATH", conflicts_with = "header")]
    json: Option<PathBuf>,

    /// Output format (required for header mode, ignored for JSON mode which always outputs TOML)
    #[arg(long, value_name = "json|toml", value_parser = parse_format)]
    format: Option<Format>,

    /// Output file path (default inferred from input for header mode, stdout for JSON mode)
    #[arg(long, short = 'o', value_name = "PATH")]
    output: Option<PathBuf>,

    /// Struct name to generate template for (required)
    #[arg(long, value_name = "NAME")]
    r#struct: String,

    /// Fail on ambiguous constructs instead of emitting placeholders (header mode only)
    #[arg(long)]
    strict: bool,
}

fn parse_format(s: &str) -> Result<Format, String> {
    s.parse::<Format>()
        .map_err(|_| "must be 'json' or 'toml'".to_string())
}

/* ---------------------------------- Main ----------------------------------- */

fn main() -> ExitCode {
    let cli = Cli::parse();

    // Must have either --header or --json
    if cli.header.is_none() && cli.json.is_none() {
        eprintln!("error: must specify either --header or --json");
        return ExitCode::from(2);
    }

    let result = if let Some(ref header) = cli.header {
        run_from_header(&cli, header)
    } else if let Some(ref json_path) = cli.json {
        run_from_json(&cli, json_path)
    } else {
        unreachable!()
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/* ----------------------------- Header Mode --------------------------------- */

fn run_from_header(cli: &Cli, header: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // Format is required for header mode
    let format = cli.format.ok_or("--format is required for header mode")?;

    let opts = TemplateOptions {
        strict: cli.strict,
        struct_filter: Some(cli.r#struct.clone()),
        ..Default::default()
    };

    // If no output provided, infer from the header extension
    let out_path = match &cli.output {
        Some(p) => p.clone(),
        None => tunable_params::infer_output_path(header, format)?,
    };

    // Generate the template (read -> parse -> emit)
    let final_path = tunable_params::generate_template(header, format, Some(&out_path), &opts)?;

    let fmt = match format {
        Format::Json => "JSON",
        Format::Toml => "TOML",
    };
    println!("Metadata written to {} as {}", final_path.display(), fmt);
    Ok(())
}

/* ----------------------------- JSON Mode ----------------------------------- */

fn run_from_json(cli: &Cli, json_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let struct_name = &cli.r#struct;

    let content = fs::read_to_string(json_path)?;
    let dict: Json = serde_json::from_str(&content)?;

    let structs = dict
        .get("structs")
        .and_then(|v| v.as_object())
        .ok_or("JSON missing 'structs' field")?;

    // Find the requested struct
    let entry = structs
        .get(struct_name)
        .ok_or_else(|| format!("struct '{}' not found in JSON", struct_name))?;

    // Generate TOML
    let toml_str = generate_toml_from_json(struct_name, entry)?;

    // Write output
    match &cli.output {
        Some(out_path) => {
            fs::write(out_path, &toml_str)?;
            println!("Template written to {}", out_path.display());
        }
        None => {
            print!("{}", toml_str);
        }
    }

    Ok(())
}

/// Generate TOML template from a struct entry in apex_data_db JSON format.
fn generate_toml_from_json(
    struct_name: &str,
    entry: &Json,
) -> Result<String, Box<dyn std::error::Error>> {
    let fields = entry
        .get("fields")
        .and_then(|v| v.as_array())
        .ok_or("struct entry missing 'fields' array")?;

    let mut doc = DocumentMut::new();
    let mut table = Table::new();

    for field in fields {
        let name = field
            .get("name")
            .and_then(|v| v.as_str())
            .ok_or("field missing 'name'")?;

        let mut field_table = Table::new();

        // Type (map "float" -> "double" for clarity)
        if let Some(ty) = field.get("type").and_then(|v| v.as_str()) {
            let mapped_type = if ty == "float" { "double" } else { ty };
            field_table["type"] = Item::Value(TomlValue::from(mapped_type));
        }

        // Size
        if let Some(size) = field.get("size").and_then(|v| v.as_u64()) {
            field_table["size"] = Item::Value(TomlValue::from(size as i64));
        }

        // Value
        if let Some(value) = field.get("value") {
            field_table["value"] = Item::Value(json_value_to_toml(value));
        }

        // Array metadata
        if let Some(elem_type) = field.get("element_type").and_then(|v| v.as_str()) {
            field_table["element_type"] = Item::Value(TomlValue::from(elem_type));
        }
        if let Some(dims) = field.get("dims").and_then(|v| v.as_array()) {
            let mut arr = Array::default();
            for d in dims {
                if let Some(n) = d.as_u64() {
                    arr.push(n as i64);
                }
            }
            field_table["dims"] = Item::Value(TomlValue::Array(arr));
        }

        table[name] = Item::Table(field_table);
    }

    doc[struct_name] = Item::Table(table);
    Ok(doc.to_string())
}

/// Convert a JSON value to a TOML value.
fn json_value_to_toml(v: &Json) -> TomlValue {
    match v {
        Json::Null => TomlValue::from(""),
        Json::Bool(b) => TomlValue::from(*b),
        Json::Number(n) => {
            if let Some(i) = n.as_i64() {
                TomlValue::from(i)
            } else if let Some(f) = n.as_f64() {
                TomlValue::from(f)
            } else {
                TomlValue::from(n.to_string())
            }
        }
        Json::String(s) => TomlValue::from(s.clone()),
        Json::Array(arr) => {
            let mut a = Array::default();
            for elem in arr {
                a.push(json_value_to_toml(elem));
            }
            TomlValue::Array(a)
        }
        Json::Object(_) => {
            // Nested objects become strings (shouldn't happen in our use case)
            TomlValue::from(v.to_string())
        }
    }
}
