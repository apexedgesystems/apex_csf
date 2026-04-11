//! apex_data_gen: generate JSON struct dictionaries from apex_data.toml manifests.
//!
//! Reads component manifests and C++ headers to produce struct dictionaries
//! for external C2 systems, fault injection, and debugging.
//!
//! Usage:
//!   apex_data_gen --manifest apex_data.toml --output apex_data_db/
//!   apex_data_gen --manifest apex_data.toml --output - (stdout)

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::Parser;
use serde_json::{json, Map as JsonMap, Value as Json};

use apex_rust_tools::tunable_params::{
    self,
    manifest::{EnumEntry, Manifest, StructEntry},
    ParsedEnum, TemplateOptions,
};

/* ----------------------------- CLI Definition ----------------------------- */

#[derive(Parser, Debug)]
#[command(name = "apex_data_gen")]
#[command(about = "Generate JSON struct dictionaries from apex_data.toml manifests")]
struct Cli {
    /// Path to apex_data.toml manifest file
    #[arg(long, value_name = "PATH")]
    manifest: PathBuf,

    /// Output directory for JSON files (use '-' for stdout)
    #[arg(long, value_name = "PATH")]
    output: PathBuf,

    /// Pretty-print JSON output
    #[arg(long, short = 'p')]
    pretty: bool,
}

/* --------------------------------- Main ----------------------------------- */

fn main() -> ExitCode {
    let cli = Cli::parse();

    match run(&cli) {
        Ok(path) => {
            if cli.output.to_string_lossy() != "-" {
                println!("Struct dictionary written to {}", path.display());
            }
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

/* ----------------------------- Core Logic --------------------------------- */

fn run(cli: &Cli) -> Result<PathBuf, Box<dyn std::error::Error>> {
    // Parse manifest
    let manifest = tunable_params::manifest::parse_manifest(&cli.manifest)?;

    // Determine header search directory (same dir as manifest, then inc/)
    let manifest_dir = cli.manifest.parent().unwrap_or(Path::new("."));

    // Collect all headers to parse
    let headers = discover_headers(manifest_dir, &manifest)?;

    // Two-pass parsing: collect all defines/enums first, then parse structs
    // with the merged define set. This handles cross-header constants like
    // MAX_MONITORED_CORES defined in Config.hpp but used in Tlm.hpp.
    let mut all_enums: BTreeMap<String, ParsedEnum> = BTreeMap::new();
    let mut all_contents: Vec<String> = Vec::new();

    for header_path in &headers {
        let content = tunable_params::read_file(header_path)?;

        // Collect enums from all headers
        let enums = tunable_params::collect_enums(&content)?;
        for (name, parsed_enum) in enums {
            all_enums.insert(name, parsed_enum);
        }

        all_contents.push(content);
    }

    // Concatenate all header contents so defines/constexprs are visible
    // to all struct definitions (simulates #include resolution)
    let merged_content = all_contents.join("\n");

    let opts = TemplateOptions::default();
    let mut all_parsed: BTreeMap<String, Json> = BTreeMap::new();

    let parsed = tunable_params::parse_header(&merged_content, &opts)?;
    if let Json::Object(obj) = parsed {
        for (key, value) in obj {
            if !key.starts_with("__") {
                all_parsed.insert(key, value);
            }
        }
    }

    // Build struct dictionary with enums
    let dict = build_struct_dictionary(&manifest, &all_parsed, &all_enums)?;

    // Write output
    let output_path = write_output(&cli.output, &manifest.component, &dict, cli.pretty)?;

    Ok(output_path)
}

/// Discover header files to parse based on manifest location.
fn discover_headers(
    manifest_dir: &Path,
    manifest: &Manifest,
) -> Result<Vec<PathBuf>, Box<dyn std::error::Error>> {
    let mut headers = Vec::new();

    // Check for explicit headers in manifest struct entries
    for entry in manifest.structs.values() {
        if let Some(ref header) = entry.header {
            let path = manifest_dir.join(header);
            if path.exists() && !headers.contains(&path) {
                headers.push(path);
            }
        }
    }

    // Check for explicit headers in manifest enum entries
    for entry in manifest.enums.values() {
        if let Some(ref header) = entry.header {
            let path = manifest_dir.join(header);
            if path.exists() && !headers.contains(&path) {
                headers.push(path);
            }
        }
    }

    // If all entries have explicit headers, skip automatic discovery.
    // This avoids parsing unrelated headers (e.g., complex class headers
    // in the same inc/ directory as simple data structs).
    let all_structs_explicit = manifest.structs.values().all(|e| e.header.is_some());
    let all_enums_explicit =
        manifest.enums.is_empty() || manifest.enums.values().all(|e| e.header.is_some());
    let skip_auto_discovery =
        !manifest.structs.is_empty() && all_structs_explicit && all_enums_explicit;

    if !skip_auto_discovery {
        // Default: look for inc/*.hpp in manifest directory
        let inc_dir = manifest_dir.join("inc");
        if inc_dir.is_dir() {
            for entry in fs::read_dir(&inc_dir)? {
                let entry = entry?;
                let path = entry.path();
                if path.extension().map_or(false, |e| e == "hpp" || e == "h") {
                    if !headers.contains(&path) {
                        headers.push(path);
                    }
                }
            }
        }

        // Also check manifest directory itself for headers
        for entry in fs::read_dir(manifest_dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().map_or(false, |e| e == "hpp" || e == "h") {
                if !headers.contains(&path) {
                    headers.push(path);
                }
            }
        }
    }

    if headers.is_empty() {
        return Err(format!(
            "No header files found in {} or {}/inc/",
            manifest_dir.display(),
            manifest_dir.display()
        )
        .into());
    }

    Ok(headers)
}

/// Build struct dictionary JSON from manifest and parsed headers.
fn build_struct_dictionary(
    manifest: &Manifest,
    parsed: &BTreeMap<String, Json>,
    all_enums: &BTreeMap<String, ParsedEnum>,
) -> Result<Json, Box<dyn std::error::Error>> {
    let mut structs = JsonMap::new();

    for (struct_name, entry) in &manifest.structs {
        // Look up parsed struct
        let parsed_struct = parsed.get(struct_name);

        let struct_json = build_struct_entry(struct_name, entry, parsed_struct)?;
        structs.insert(struct_name.clone(), struct_json);
    }

    // Build enums section
    let mut enums = JsonMap::new();

    for (enum_name, entry) in &manifest.enums {
        let enum_json = build_enum_entry(enum_name, entry, all_enums.get(enum_name))?;
        enums.insert(enum_name.clone(), enum_json);
    }

    // Build final output (only include enums if there are any)
    if enums.is_empty() {
        Ok(json!({
            "component": manifest.component,
            "structs": structs,
        }))
    } else {
        Ok(json!({
            "component": manifest.component,
            "structs": structs,
            "enums": enums,
        }))
    }
}

/// Build a single enum entry for the dictionary.
fn build_enum_entry(
    name: &str,
    _entry: &EnumEntry,
    parsed: Option<&ParsedEnum>,
) -> Result<Json, Box<dyn std::error::Error>> {
    let mut result = JsonMap::new();

    if let Some(pe) = parsed {
        // Underlying type (default to int if not specified)
        let underlying = pe.underlying_type.as_deref().unwrap_or("int");
        result.insert("underlying_type".into(), json!(underlying));

        // Values as object
        let mut values = JsonMap::new();
        for (val_name, val_num) in &pe.values {
            values.insert(val_name.clone(), json!(val_num));
        }
        result.insert("values".into(), Json::Object(values));
    } else {
        // Enum not found in parsed headers
        result.insert("underlying_type".into(), json!("int"));
        result.insert("values".into(), json!({}));
        result.insert(
            "_warning".into(),
            json!(format!("Enum '{}' not found in headers", name)),
        );
    }

    Ok(Json::Object(result))
}

/// Build a single struct entry for the dictionary.
fn build_struct_entry(
    name: &str,
    entry: &StructEntry,
    parsed: Option<&Json>,
) -> Result<Json, Box<dyn std::error::Error>> {
    let mut result = JsonMap::new();

    // Category
    result.insert("category".into(), json!(entry.category.to_string()));

    // Opcode for COMMAND/TELEMETRY
    if let Some(ref opcode) = entry.opcode {
        result.insert("opcode".into(), json!(opcode));
    }

    // Fields from parsed header
    if let Some(Json::Array(fields)) = parsed {
        let (struct_fields, total_size) = convert_fields_to_dictionary_format(fields)?;
        result.insert("size".into(), json!(total_size));
        result.insert("fields".into(), Json::Array(struct_fields));
    } else {
        // Struct not found in parsed headers
        result.insert("size".into(), json!(0));
        result.insert("fields".into(), json!([]));
        result.insert(
            "_warning".into(),
            json!(format!("Struct '{}' not found in headers", name)),
        );
    }

    Ok(Json::Object(result))
}

/// Convert parsed fields to struct dictionary format with offsets.
fn convert_fields_to_dictionary_format(
    fields: &[Json],
) -> Result<(Vec<Json>, usize), Box<dyn std::error::Error>> {
    let mut result = Vec::new();
    let mut offset = 0usize;

    for field_obj in fields {
        if let Json::Object(obj) = field_obj {
            for (field_name, field_meta) in obj {
                let size = field_meta.get("size").and_then(|v| v.as_u64()).unwrap_or(0) as usize;

                let logical_type = field_meta
                    .get("type")
                    .and_then(|v| v.as_str())
                    .unwrap_or("unknown");

                let mut entry = JsonMap::new();
                entry.insert("name".into(), json!(field_name));
                entry.insert("type".into(), json!(logical_type));
                entry.insert("offset".into(), json!(offset));
                entry.insert("size".into(), json!(size));

                // Include default value from parser (for TPRM authoring reference)
                if let Some(value) = field_meta.get("value") {
                    entry.insert("value".into(), value.clone());
                }

                // Handle arrays
                if logical_type == "array" {
                    if let Some(elem_type) = field_meta.get("element_type") {
                        entry.insert("element_type".into(), elem_type.clone());
                    }
                    if let Some(dims) = field_meta.get("dims") {
                        entry.insert("dims".into(), dims.clone());
                    }
                }

                // Handle nested structs
                if logical_type == "struct" {
                    if let Some(nested_fields) = field_meta.get("fields") {
                        entry.insert("fields".into(), nested_fields.clone());
                    }
                }

                result.push(Json::Object(entry));
                offset += size;
            }
        }
    }

    Ok((result, offset))
}

/// Write output to file or stdout.
fn write_output(
    output: &Path,
    component: &str,
    dict: &Json,
    pretty: bool,
) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let json_str = if pretty {
        serde_json::to_string_pretty(dict)?
    } else {
        serde_json::to_string(dict)?
    };

    if output.to_string_lossy() == "-" {
        println!("{}", json_str);
        return Ok(PathBuf::from("-"));
    }

    // If output is a directory, write to component.json
    let output_path = if output.is_dir() || output.to_string_lossy().ends_with('/') {
        fs::create_dir_all(output)?;
        output.join(format!("{}.json", component))
    } else {
        if let Some(parent) = output.parent() {
            if !parent.as_os_str().is_empty() {
                fs::create_dir_all(parent)?;
            }
        }
        output.to_path_buf()
    };

    fs::write(&output_path, json_str)?;
    Ok(output_path)
}
