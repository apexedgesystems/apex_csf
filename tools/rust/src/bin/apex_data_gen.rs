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
    manifest::{EnumEntry, FieldConstraints, FieldDef, Manifest, StructEntry},
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
    // in the same inc/ directory as simple data structs). Spec-defined
    // structs ([[fields]] in the manifest) need no header at all -- their
    // layout comes from the spec -- so they count as explicit here.
    let all_structs_explicit = manifest
        .structs
        .iter()
        .all(|(name, e)| e.header.is_some() || manifest.fields.contains_key(name));
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
                if path.extension().is_some_and(|e| e == "hpp" || e == "h")
                    && !headers.contains(&path)
                {
                    headers.push(path);
                }
            }
        }

        // Also check manifest directory itself for headers
        for entry in fs::read_dir(manifest_dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().is_some_and(|e| e == "hpp" || e == "h") && !headers.contains(&path)
            {
                headers.push(path);
            }
        }
    }

    // A fully spec-defined manifest (every struct has an ordered field
    // list, no enum entries) needs no C++ parsing at all; headers are
    // required only when some entry derives from one.
    let needs_headers = manifest
        .structs
        .keys()
        .any(|s| !manifest.fields.contains_key(s))
        || !manifest.enums.is_empty();
    if headers.is_empty() && needs_headers {
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
        let constraints = manifest.constraints.get(struct_name);

        // Spec-defined structs derive from their ordered field list --
        // the same source the .auto header generates from -- so the
        // dictionary (and everything downstream: templates, ground UI)
        // cannot drift from the spec. Others parse the C++ header.
        let struct_json = match manifest.fields.get(struct_name) {
            Some(spec) => build_struct_entry_from_spec(
                struct_name,
                entry,
                spec,
                &manifest.fields,
                constraints,
            )?,
            None => build_struct_entry(struct_name, entry, parsed.get(struct_name), constraints)?,
        };
        structs.insert(struct_name.clone(), struct_json);
    }

    // A constraint section naming an unregistered struct is a typo, not
    // an extension point.
    for constrained in manifest.constraints.keys() {
        if !manifest.structs.contains_key(constrained) {
            return Err(format!(
                "constraints declared for '{constrained}', which is not a registered struct"
            )
            .into());
        }
    }

    // Build enums section
    let mut enums = JsonMap::new();

    for (enum_name, entry) in &manifest.enums {
        let enum_json = build_enum_entry(enum_name, entry, all_enums.get(enum_name))?;
        enums.insert(enum_name.clone(), enum_json);
    }

    // Command/telemetry entries come straight from the spec -- the same
    // [[commands]]/[[telemetry]] arrays the dispatch base generates from --
    // so the ground dictionary names exactly the opcodes the component
    // dispatches. Payload structs must be registered (their layouts are
    // ordinary struct entries above).
    let mut commands = Vec::new();
    for c in &manifest.commands {
        for payload in [&c.request, &c.response].into_iter().flatten() {
            if !manifest.structs.contains_key(payload) {
                return Err(format!(
                    "command '{}' references '{payload}', which is not a registered struct",
                    c.name
                )
                .into());
            }
        }
        let mut entry = JsonMap::new();
        entry.insert("name".into(), json!(c.name));
        entry.insert("opcode".into(), json!(c.opcode));
        if let Some(rq) = &c.request {
            entry.insert("request".into(), json!(rq));
        }
        if let Some(rs) = &c.response {
            entry.insert("response".into(), json!(rs));
        }
        if let Some(doc) = &c.doc {
            entry.insert("doc".into(), json!(doc));
        }
        commands.push(Json::Object(entry));
    }

    let mut telemetry = Vec::new();
    for t in &manifest.telemetry {
        if !manifest.structs.contains_key(&t.r#struct) {
            return Err(format!(
                "telemetry '{}' references '{}', which is not a registered struct",
                t.name, t.r#struct
            )
            .into());
        }
        let mut entry = JsonMap::new();
        entry.insert("name".into(), json!(t.name));
        entry.insert("opcode".into(), json!(t.opcode));
        entry.insert("struct".into(), json!(t.r#struct));
        if let Some(doc) = &t.doc {
            entry.insert("doc".into(), json!(doc));
        }
        telemetry.push(Json::Object(entry));
    }

    // Build final output (optional sections only when present)
    let mut out = JsonMap::new();
    out.insert("component".into(), json!(manifest.component));
    if let Some(id) = manifest.component_id {
        out.insert("component_id".into(), json!(id));
    }
    out.insert("structs".into(), Json::Object(structs));
    if !enums.is_empty() {
        out.insert("enums".into(), Json::Object(enums));
        if !manifest.capabilities.is_empty() {
            out.insert(
                "capabilities".into(),
                Json::Array(
                    manifest
                        .capabilities
                        .iter()
                        .map(|c| Json::String(c.clone()))
                        .collect(),
                ),
            );
        }
    }
    if !commands.is_empty() {
        out.insert("commands".into(), Json::Array(commands));
    }
    if !telemetry.is_empty() {
        out.insert("telemetry".into(), Json::Array(telemetry));
    }
    Ok(Json::Object(out))
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

/// Build a struct entry from its spec field list (cdef path).
fn build_struct_entry_from_spec(
    struct_name: &str,
    entry: &StructEntry,
    spec: &[FieldDef],
    all_fields: &tunable_params::cdef::FieldsMap,
    constraints: Option<&BTreeMap<String, FieldConstraints>>,
) -> Result<Json, Box<dyn std::error::Error>> {
    let mut result = JsonMap::new();
    result.insert("category".into(), json!(entry.category.to_string()));
    if let Some(ref opcode) = entry.opcode {
        result.insert("opcode".into(), json!(opcode));
    }

    let mut fields = Vec::new();
    let mut offset = 0u32;
    for f in spec {
        let mut e = JsonMap::new();
        e.insert("name".into(), json!(f.name));
        let total =
            tunable_params::cdef::layout_size(struct_name, std::slice::from_ref(f), all_fields)?;
        if f.r#type == "nested" {
            e.insert("type".into(), json!("nested"));
            e.insert("struct".into(), json!(f.r#struct));
            if let Some(n) = f.count {
                e.insert("dims".into(), json!([n]));
            }
        } else {
            match f.count {
                None => {
                    e.insert("type".into(), json!(f.r#type));
                }
                Some(n) => {
                    e.insert("type".into(), json!("array"));
                    e.insert("element_type".into(), json!(f.r#type));
                    e.insert("dims".into(), json!([n]));
                }
            }
        }
        e.insert("offset".into(), json!(offset));
        e.insert("size".into(), json!(total));
        if let Some(d) = &f.default {
            let v: Json = serde_json::to_value(d.clone())?;
            match f.count {
                None => {
                    e.insert("value".into(), v);
                }
                Some(n) => {
                    e.insert("value".into(), json!(vec![v; n as usize]));
                }
            }
        } else if let Some(n) = f.count.filter(|_| f.r#type != "nested") {
            e.insert("value".into(), json!(vec![0; n as usize]));
        }
        if let Some(doc) = &f.doc {
            e.insert("doc".into(), json!(doc));
        }
        if let Some(c) = constraints.and_then(|m| m.get(&f.name)) {
            e.insert("constraints".into(), constraints_to_json(c));
        }
        fields.push(Json::Object(e));
        offset += total;
    }

    if let Some(cmap) = constraints {
        for key in cmap.keys() {
            if !spec.iter().any(|f| &f.name == key) {
                return Err(format!("constraints declared for unknown spec field: {key}").into());
            }
        }
    }

    result.insert("size".into(), json!(offset));
    result.insert("fields".into(), Json::Array(fields));
    result.insert("spec_defined".into(), json!(true));
    // The producer states the layout hash: the same CRC-32 over the
    // canonical field spec the v3 prelude is checked against on the
    // vehicle, so consumers (zenith) carry it instead of recomputing
    // from flattened field lists.
    let canonical = tunable_params::cdef::canonical_spec(struct_name, spec, all_fields)?;
    result.insert(
        "layout_hash".into(),
        json!(format!(
            "0x{:08X}",
            tunable_params::payload::crc32(canonical.as_bytes())
        )),
    );
    result.insert("canonical_spec".into(), json!(canonical));
    Ok(Json::Object(result))
}

/// Build a single struct entry for the dictionary.
fn build_struct_entry(
    name: &str,
    entry: &StructEntry,
    parsed: Option<&Json>,
    constraints: Option<&BTreeMap<String, FieldConstraints>>,
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
        let (struct_fields, total_size) = convert_fields_to_dictionary_format(fields, constraints)?;
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

/// Serialize a constraint declaration as the dictionary's per-field
/// `constraints` object (present keys only).
fn constraints_to_json(c: &FieldConstraints) -> Json {
    let mut obj = JsonMap::new();
    if let Some(v) = c.min {
        obj.insert("min".into(), json!(v));
    }
    if let Some(v) = c.max {
        obj.insert("max".into(), json!(v));
    }
    if let Some(ref v) = c.allowed {
        obj.insert("allowed".into(), json!(v));
    }
    if let Some(v) = c.step {
        obj.insert("step".into(), json!(v));
    }
    Json::Object(obj)
}

/// Convert parsed fields to struct dictionary format with offsets.
fn convert_fields_to_dictionary_format(
    fields: &[Json],
    constraints: Option<&BTreeMap<String, FieldConstraints>>,
) -> Result<(Vec<Json>, usize), Box<dyn std::error::Error>> {
    let mut result = Vec::new();
    let mut offset = 0usize;
    let mut unmatched: Vec<&String> = constraints.map(|c| c.keys().collect()).unwrap_or_default();

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

                // Attach the component-level constraint declaration: the
                // ground UI and the on-board table both read it from here.
                if let Some(c) = constraints.and_then(|m| m.get(field_name)) {
                    entry.insert("constraints".into(), constraints_to_json(c));
                    unmatched.retain(|k| *k != field_name);
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

    if !unmatched.is_empty() {
        return Err(format!(
            "constraints declared for unknown fields: {}",
            unmatched
                .iter()
                .map(|s| s.as_str())
                .collect::<Vec<_>>()
                .join(", ")
        )
        .into());
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
