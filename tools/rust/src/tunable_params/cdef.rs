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

use std::collections::BTreeMap;

/// All field specs in a manifest, keyed by struct name: the lookup
/// context nested fields resolve against.
pub type FieldsMap = BTreeMap<String, Vec<FieldDef>>;

/// Resolve a `type = "nested"` field to its embedded struct's fields.
/// One level only: the embedded struct must be all leaves.
fn resolve_nested<'a>(
    all: &'a FieldsMap,
    host: &str,
    f: &FieldDef,
) -> Result<&'a [FieldDef], Error> {
    let sub_name = f.r#struct.as_deref().ok_or_else(|| {
        Error::Parse(format!(
            "{host}.{}: type = \"nested\" requires struct = \"<Name>\"",
            f.name
        ))
    })?;
    let sub = all.get(sub_name).ok_or_else(|| {
        Error::Parse(format!(
            "{host}.{}: nested struct '{sub_name}' has no [[fields.{sub_name}]] spec",
            f.name
        ))
    })?;
    if let Some(deep) = sub.iter().find(|s| s.r#type == "nested") {
        return Err(Error::Parse(format!(
            "{host}.{}: '{sub_name}.{}' is itself nested -- one level of nesting only",
            f.name, deep.name
        )));
    }
    Ok(sub)
}

/// Shape checks that apply to every field before emission: a leaf
/// needs a positive size and no struct key; a nested field is sized
/// by its embedded struct.
fn validate_field_shape(host: &str, f: &FieldDef) -> Result<(), Error> {
    if f.r#type == "nested" {
        if f.default.is_some() {
            return Err(Error::Parse(format!(
                "{host}.{}: nested fields take no default (defaults live on the embedded struct's leaves)",
                f.name
            )));
        }
        return Ok(());
    }
    if f.r#struct.is_some() {
        return Err(Error::Parse(format!(
            "{host}.{}: struct = ... only applies to type = \"nested\"",
            f.name
        )));
    }
    if f.size == 0 {
        return Err(Error::Parse(format!(
            "{host}.{}: leaf fields need a positive size",
            f.name
        )));
    }
    Ok(())
}

/// Append one leaf's canonical rows at `offset`; returns its span.
fn push_leaf_rows(spec: &mut String, f: &FieldDef, offset: u32) -> u32 {
    match f.count {
        None => {
            spec.push_str(&format!("{}:{}:{}:{};", f.name, f.r#type, f.size, offset));
            f.size
        }
        Some(n) => {
            let total = f.size * n;
            spec.push_str(&format!("{}:array:{}:{};", f.name, total, offset));
            spec.push_str(&format!("[{}:{}x{}]", f.r#type, f.size, n));
            total
        }
    }
}

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
/// the serializer's emission-order walk (`name:type:size:offset;` per
/// leaf, array element shape appended, `|size:total` terminator) over
/// the template this spec generates. Offsets pin the byte layout:
/// packed and padded arrangements of the same fields hash apart, and
/// any byte moving changes the hash.
/// Nested fields inline their embedded struct's leaves (per element
/// for arrays, no wrapper row) -- exactly what the serializer's walk
/// emits over the nested value tables, so agreement survives nesting.
pub fn canonical_spec(host: &str, fields: &[FieldDef], all: &FieldsMap) -> Result<String, Error> {
    let mut spec = String::new();
    let mut offset: u32 = 0;
    for f in fields {
        validate_field_shape(host, f)?;
        if f.r#type == "nested" {
            let sub = resolve_nested(all, host, f)?;
            for _ in 0..f.count.unwrap_or(1) {
                for s in sub {
                    validate_field_shape(host, s)?;
                    offset += push_leaf_rows(&mut spec, s, offset);
                }
            }
        } else {
            offset += push_leaf_rows(&mut spec, f, offset);
        }
    }
    spec.push_str(&format!("|size:{offset}"));
    Ok(spec)
}

/// Alignment validation for non-packed specs: offsets are cumulative
/// (no implicit padding, ever -- gaps must be explicit pad fields), so
/// a natural-alignment struct is only reproducible when every field
/// lands on its own alignment and the total is a multiple of the
/// widest. Packed specs skip this; their sequential bytes are the
/// layout by definition.
pub fn validate_natural_alignment(
    manifest: &Manifest,
    struct_name: &str,
    fields: &[FieldDef],
) -> Result<(), Error> {
    // A string is a char buffer: its size is byte length, its
    // alignment is 1. Numeric fields align to their own width. A
    // nested field aligns as its embedded struct does: 1 when packed,
    // otherwise the widest of its leaves.
    let leaf_align = |f: &FieldDef| -> u32 {
        if f.r#type == "string" || f.r#type == "char" {
            1
        } else {
            f.size.min(8)
        }
    };
    let mut offset: u32 = 0;
    let mut max_align: u32 = 1;
    for f in fields {
        let (align, span) = if f.r#type == "nested" {
            let sub = resolve_nested(&manifest.fields, struct_name, f)?;
            let sub_name = f.r#struct.as_deref().unwrap_or_default();
            let sub_packed = manifest.structs.get(sub_name).is_some_and(|e| e.packed);
            let align = if sub_packed {
                1
            } else {
                sub.iter().map(&leaf_align).max().unwrap_or(1)
            };
            let sub_size: u32 = sub.iter().map(|s| s.size * s.count.unwrap_or(1)).sum();
            (align, sub_size * f.count.unwrap_or(1))
        } else {
            (leaf_align(f), f.size * f.count.unwrap_or(1))
        };
        max_align = max_align.max(align);
        if align > 0 && offset % align != 0 {
            return Err(Error::Parse(format!(
                "{struct_name}.{}: offset {offset} misaligns a {align}-byte-aligned field; \
                 add explicit pad fields or mark the struct packed = true",
                f.name
            )));
        }
        offset += span;
    }
    if max_align > 0 && offset % max_align != 0 {
        return Err(Error::Parse(format!(
            "{struct_name}: total {offset} is not a multiple of the widest \
             alignment {max_align}; add trailing pad fields or mark packed = true"
        )));
    }
    Ok(())
}

/// Total serialized size of the spec's layout in bytes.
pub fn layout_size(host: &str, fields: &[FieldDef], all: &FieldsMap) -> Result<u32, Error> {
    let mut total: u32 = 0;
    for f in fields {
        if f.r#type == "nested" {
            let sub = resolve_nested(all, host, f)?;
            let sub_size: u32 = sub.iter().map(|s| s.size * s.count.unwrap_or(1)).sum();
            total += sub_size * f.count.unwrap_or(1);
        } else {
            total += f.size * f.count.unwrap_or(1);
        }
    }
    Ok(total)
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

    let packed = manifest.structs.get(struct_name).is_some_and(|e| e.packed);
    if !packed {
        validate_natural_alignment(manifest, struct_name, fields)?;
    }
    let hash =
        super::payload::crc32(canonical_spec(struct_name, fields, &manifest.fields)?.as_bytes());
    let size = layout_size(struct_name, fields, &manifest.fields)?;

    let mut shout = String::new();
    for c in struct_name.chars() {
        if c.is_uppercase() && !shout.is_empty() && !shout.ends_with('_') {
            shout.push('_');
        }
        shout.push(c.to_ascii_uppercase());
    }

    let mut offset_asserts = String::new();
    let mut off: u32 = 0;
    for f in fields {
        offset_asserts.push_str(&format!(
            "static_assert(offsetof({struct_name}, {}) == {off}, \"field offset diverged\");\n",
            f.name
        ));
        off += layout_size(struct_name, std::slice::from_ref(f), &manifest.fields)?;
    }

    // Nested members come from sibling generated headers; a quoted
    // include resolves next to the including file, so no include path
    // is needed for .auto-to-.auto references.
    let mut nested_includes = String::new();
    let mut seen_subs: Vec<&str> = Vec::new();
    for f in fields {
        if f.r#type == "nested" {
            let sub = f.r#struct.as_deref().unwrap_or_default();
            if !seen_subs.contains(&sub) {
                seen_subs.push(sub);
                nested_includes.push_str(&format!("#include \"{sub}_auto.hpp\"\n"));
            }
        }
    }
    if !nested_includes.is_empty() {
        nested_includes.push('\n');
    }

    let mut body = String::new();
    for f in fields {
        // Fixed text: size is the byte capacity of a null-padded char
        // buffer; a count makes it a fixed array of such buffers.
        let decl = if f.r#type == "nested" {
            let sub = f.r#struct.as_deref().unwrap_or_default();
            match f.count {
                None => format!("{sub} {}{{}}", f.name),
                Some(n) => format!("{sub} {}[{n}]{{}}", f.name),
            }
        } else if f.r#type == "string" {
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

    // Sequential offsets are the layout either way; the attribute is
    // only needed when natural alignment would insert padding.
    let struct_intro = if packed {
        format!("struct __attribute__((packed)) {struct_name}")
    } else {
        format!("struct {struct_name}")
    };

    Ok(format!(
        "// Generated by cdef_gen from the {component} spec -- DO NOT EDIT.\n\
         // Regenerate: make cdef (check-cdef diffs this file against the spec).\n\
         // Overrides belong in the component's .ovr/ directory.\n\
         #ifndef APEX_CDEF_AUTO_{shout}_HPP\n\
         #define APEX_CDEF_AUTO_{shout}_HPP\n\
         \n\
         #include <cstddef>\n\
         #include <cstdint>\n\
         \n\
         {nested_includes}\
         {ns_open}\n\
         /// Spec-defined tunable parameters ({size} bytes, packed by\n\
         /// construction: field order and sizes come from the spec).\n\
         {struct_intro} {{\n\
         {body}}};\n\
         static_assert(sizeof({struct_name}) == {size}, \"layout diverged from the spec\");\n\
         {offset_asserts}\
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

/// Generate the `.auto` spec base for the component: a CRTP mixin
/// (`<C>SpecBase<TDerived, TBase>`) that owns the spec-derived
/// machinery so stubs shrink to identity + logic:
///
/// - command dispatch (size-verified decode -> pure-virtual hook ->
///   response encode; malformed = ERROR_PARAM pre-user-code, unknown
///   opcodes fall through to the tier base);
/// - the ParamBank/ModelData members for the categorized structs;
/// - loadTprm enforcing the generated layout hash (validateParams /
///   onParamsLoaded hooks for user policy);
/// - doInit registering the spec's [[tasks]] (bound to same-named
///   TDerived methods) and data blocks (onInit hook for extras).
pub fn generate_cmd_base(manifest: &Manifest) -> Result<String, Error> {
    if manifest.commands.is_empty() && manifest.tasks.is_empty() {
        return Err(Error::Parse(
            "no [[commands]] or [[tasks]] in the manifest".to_string(),
        ));
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
    {
        let mut seen_uids: Vec<u8> = Vec::new();
        for t in &manifest.tasks {
            if seen_uids.contains(&t.uid) {
                return Err(Error::Parse(format!(
                    "task '{}': duplicate task uid {}",
                    t.name, t.uid
                )));
            }
            seen_uids.push(t.uid);
            if t.name.is_empty() || !t.name.chars().all(|c| c.is_alphanumeric() || c == '_') {
                return Err(Error::Parse(format!("bad task name '{}'", t.name)));
            }
        }
    }

    let component = &manifest.component;
    let class = format!("{component}SpecBase");
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

    // Categorized data structs the base owns members for (only when
    // the spec defines their layouts).
    let struct_of = |cat: super::manifest::DataCategory| -> Option<String> {
        manifest
            .structs
            .iter()
            .find(|(name, e)| e.category == cat && manifest.fields.contains_key(*name))
            .map(|(name, _)| name.clone())
    };
    let tunable = struct_of(super::manifest::DataCategory::TunableParam);
    let state = struct_of(super::manifest::DataCategory::State);
    let output = struct_of(super::manifest::DataCategory::Output);

    for member_struct in [&tunable, &state, &output].into_iter().flatten() {
        if !seen.contains(&member_struct) {
            includes.push_str(&format!("#include \"{member_struct}_auto.hpp\"\n"));
        }
    }
    let mut infra_includes = String::new();
    if tunable.is_some() {
        infra_includes.push_str(
            "#include \"src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp\"\n",
        );
    }
    if tunable.is_some() || state.is_some() || output.is_some() {
        infra_includes.push_str(
            "#include \"src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp\"\n",
        );
    }
    if tunable.is_some() {
        infra_includes.push_str("#include <filesystem>\n");
    }

    // TaskUid enum + registrations from [[tasks]].
    let mut task_enum = String::new();
    let mut task_regs = String::new();
    if !manifest.tasks.is_empty() {
        task_enum.push_str("  enum class TaskUid : std::uint8_t {\n");
        for t in &manifest.tasks {
            let doc = t.doc.as_deref().unwrap_or("Spec-declared task.");
            let mut shout_name = String::new();
            for ch in t.name.chars() {
                if ch.is_uppercase() && !shout_name.is_empty() && !shout_name.ends_with('_') {
                    shout_name.push('_');
                }
                shout_name.push(ch.to_ascii_uppercase());
            }
            task_enum.push_str(&format!("    {shout_name} = {}, ///< {doc}\n", t.uid));
            task_regs.push_str(&format!(
                "    this->template registerTask<TDerived, &TDerived::{name}>(\n        \
                 static_cast<std::uint8_t>(TaskUid::{shout_name}), static_cast<TDerived*>(this), \"{name}\");\n",
                name = t.name
            ));
        }
        task_enum.push_str("  };\n\n");
    }

    // Data-block registrations + members + accessors + loadTprm.
    let mut data_regs = String::new();
    let mut members = String::new();
    let mut accessors = String::new();
    let mut load_tprm = String::new();
    let mut param_hooks = String::new();
    if let Some(ts) = &tunable {
        let mut shout_struct = String::new();
        for ch in ts.chars() {
            if ch.is_uppercase() && !shout_struct.is_empty() && !shout_struct.ends_with('_') {
                shout_struct.push('_');
            }
            shout_struct.push(ch.to_ascii_uppercase());
        }
        data_regs.push_str(&format!(
            "    this->registerData(system_core::data::DataCategory::TUNABLE_PARAM, \"tunableParams\",\n                       &inspectParams_, sizeof({ts}));\n"
        ));
        members.push_str(&format!(
            "  system_core::system_component::ParamBank<{ts}> paramBank_{{}};\n  {ts} inspectParams_{{}};\n"
        ));
        param_hooks.push_str(&format!(
            "  /// Accept or reject an incoming parameter set (policy hook).\n  \
             [[nodiscard]] virtual bool validateParams(const {ts}&) noexcept {{ return true; }}\n  \
             /// Called after every successful publish (seed state from params here).\n  \
             virtual void onParamsLoaded() noexcept {{}}\n"
        ));
        load_tprm.push_str(&format!(
            "  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {{\n    \
             if (!this->isRegistered()) {{\n      return false;\n    }}\n    \
             const std::filesystem::path PATH = tprmDir / this->tprmFilename(this->fullUid());\n    \
             bool loaded = false;\n    \
             if (std::filesystem::exists(PATH)) {{\n      \
             loaded = paramBank_.load(\n                   PATH, this->fullUid(),\n                   \
             [this](const {ts}& p) noexcept {{ return validateParams(p); }},\n                   \
             &{shout_struct}_LAYOUT_HASH) ==\n               \
             system_core::system_component::Status::SUCCESS;\n    }}\n    \
             if (!loaded) {{\n      (void)paramBank_.load({ts}{{}});\n    }}\n    \
             // First load publishes the initial generation; a RELOAD on a\n    \
             // live bank must APPLY the staged set instead -- publishInitial\n    \
             // is a no-op once a generation is active (silent-no-effect\n    \
             // defect class, caught by zenith 2026-08-22).\n    \
             if (paramBank_.activeGeneration() == 0) {{\n      \
             (void)paramBank_.publishInitial();\n    }} else {{\n      \
             (void)paramBank_.apply();\n    }}\n    \
             inspectParams_ = paramBank_.active();\n    \
             onParamsLoaded();\n    \
             this->setConfigured(true);\n    \
             return loaded;\n  }}\n\n  \
             /// Staged-payload verification enforces the spec hash for this\n  \
             /// component (VERIFY_TPRM and the verify-gated RELOAD).\n  \
             [[nodiscard]] const std::uint32_t* expectedLayoutHash() const noexcept override {{\n    \
             return &{shout_struct}_LAYOUT_HASH;\n  }}\n\n"
        ));
    }
    if let Some(ss) = &state {
        data_regs.push_str(&format!(
            "    this->registerData(system_core::data::DataCategory::STATE, \"state\", &state_.get(),\n                       sizeof({ss}));\n"
        ));
        members.push_str(&format!("  system_core::data::State<{ss}> state_{{}};\n"));
        accessors.push_str(&format!(
            "  [[nodiscard]] const {ss}& state() const noexcept {{ return state_.get(); }}\n"
        ));
    }
    if let Some(os) = &output {
        data_regs.push_str(&format!(
            "    this->registerData(system_core::data::DataCategory::OUTPUT, \"output\", &output_.get(),\n                       sizeof({os}));\n"
        ));
        members.push_str(&format!("  system_core::data::Output<{os}> output_{{}};\n"));
        accessors.push_str(&format!(
            "  [[nodiscard]] const {os}& output() const noexcept {{ return output_.get(); }}\n"
        ));
    }
    if !accessors.is_empty() {
        accessors.push('\n');
    }

    let do_init = format!(
        "  [[nodiscard]] std::uint8_t doInit() noexcept override {{\n{task_regs}{data_regs}    return onInit();\n  }}\n\n"
    );

    // Dispatch only when the spec declares commands.
    let dispatch = if manifest.commands.is_empty() {
        String::new()
    } else {
        format!(
            "  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,\n                                           apex::compat::rospan<std::uint8_t> payload,\n                                           std::vector<std::uint8_t>& response) noexcept override {{\n    switch (opcode) {{\n{cases}    default:\n      return TBase::handleCommand(opcode, payload, response);\n    }}\n  }}\n"
        )
    };
    let dispatch_includes = if manifest.commands.is_empty() {
        String::new()
    } else {
        "#include \"src/utilities/compatibility/inc/compat_span.hpp\"\n".to_string()
    };
    let dispatch_std_includes = if manifest.commands.is_empty() {
        ""
    } else {
        "#include <cstring>\n#include <vector>\n"
    };

    Ok(format!(
        "// Generated by cdef_gen from the {component} spec -- DO NOT EDIT.\n\
         // Regenerate: make cdef (check-cdef diffs this file against the spec).\n\
         // Implement the task methods and on<Command> hooks in the component\n\
         // (stub-generated); the base owns members, loadTprm, doInit, dispatch.\n\
         #ifndef APEX_CDEF_AUTO_{shout}_SPEC_BASE_HPP\n\
         #define APEX_CDEF_AUTO_{shout}_SPEC_BASE_HPP\n\
         \n\
         #include \"src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp\"\n\
         {dispatch_includes}\
         {infra_includes}\
         {includes}\n\
         #include <cstdint>\n\
         {dispatch_std_includes}\
         \n\
         {ns_open}\n\
         /// Spec-generated component base (CRTP over the derived component\n\
         /// and its tier base): owns the categorized data members, the\n\
         /// hash-enforcing loadTprm, the [[tasks]]-driven doInit, and the\n\
         /// command dispatch. Unknown opcodes fall through to the tier base.\n\
         template <typename TDerived, typename TBase> class {class} : public TBase {{\n\
         public:\n\
           using TBase::TBase;\n\
         \n\
         {task_enum}\
         {accessors}\
         {load_tprm}\
         protected:\n\
           /// Extra derived-class initialization after spec registration.\n\
           [[nodiscard]] virtual std::uint8_t onInit() noexcept {{ return 0; }}\n\
         {param_hooks}\
         {hooks}\n\
         {do_init}\
         {dispatch}\
         \n\
         {members}\
         }};\n\
         \n\
         {ns_close}\
         #endif // APEX_CDEF_AUTO_{shout}_SPEC_BASE_HPP\n"
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

    // Task method skeletons from [[tasks]] (the generated base binds
    // same-named methods in its doInit).
    let mut task_methods = String::new();
    if manifest.tasks.is_empty() {
        task_methods.push_str(
            "  // No [[tasks]] declared; add methods here and tasks to the spec\n  // when the component becomes schedulable.\n",
        );
    }
    for task in &manifest.tasks {
        let doc = task.doc.as_deref().unwrap_or("Spec-declared task.");
        task_methods.push_str(&format!(
            "  /** @brief {doc} */\n  std::uint8_t {name}() noexcept {{\n    // Component periodic logic belongs here.\n    return 0;\n  }}\n\n",
            name = task.name
        ));
    }

    let spec_base = format!("{component}SpecBase<{component}, {base}>");
    let base_include_line =
        format!("#include \"{component}SpecBase_auto.hpp\" // via the .auto include path\n");

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
         // USER-OWNED after generation: fill in the component logic. The\n\
         // generated SpecBase owns members, loadTprm, doInit, and dispatch;\n\
         // this file owns identity, task methods, and hooks.\n\
         #ifndef APEX_SPEC_STUB_{label}_HPP\n\
         #define APEX_SPEC_STUB_{label}_HPP\n\
         \n\
         #include \"{base_include}\"\n\
         {base_include_line}\
         \n\
         #include <cstdint>\n\
         \n\
         {ns_open}\n\
         class {component} final : public {spec_base} {{\n\
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
         {task_methods}\
         protected:\n\
         {hooks}\
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
            count = 4
        "#,
        )
        .unwrap()
    }

    #[test]
    fn header_carries_struct_assert_and_hash() {
        let h = generate_header(&pilot_manifest(), "WaveGenTunableParams").unwrap();
        assert!(h.contains("struct WaveGenTunableParams {"));
        assert!(h.contains("float frequency{1.0F}; ///< Primary frequency [Hz]"));
        assert!(h.contains("std::uint8_t reserved[4]{};"));
        assert!(h.contains("static_assert(sizeof(WaveGenTunableParams) == 8"));
        assert!(h.contains("static_assert(offsetof(WaveGenTunableParams, reserved) == 4"));
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
        let spec = canonical_spec("WaveGenTunableParams", fields, &m.fields).unwrap();
        assert_eq!(
            spec,
            "frequency:float:4:0;reserved:array:4:4;[uint:1x4]|size:8"
        );
    }

    #[test]
    fn misaligned_layout_needs_packed() {
        // A float after a lone byte cannot reproduce as a natural
        // struct; packed = true accepts it and emits the attribute.
        let toml = |packed: &str| {
            format!(
                r#"
                component = "Mon"
                [structs]
                MonTunableParams = {{ category = "TUNABLE_PARAM"{packed} }}
                [[fields.MonTunableParams]]
                name = "enabled"
                type = "uint"
                size = 1
                [[fields.MonTunableParams]]
                name = "threshold"
                type = "float"
                size = 4
            "#
            )
        };
        let natural = parse_manifest_str(&toml("")).unwrap();
        let err = generate_header(&natural, "MonTunableParams").unwrap_err();
        assert!(format!("{err}").contains("misaligns"), "{err}");

        let packed = parse_manifest_str(&toml(", packed = true")).unwrap();
        let h = generate_header(&packed, "MonTunableParams").unwrap();
        assert!(h.contains("struct __attribute__((packed)) MonTunableParams {"));
        assert!(h.contains("static_assert(sizeof(MonTunableParams) == 5"));
        assert!(h.contains("static_assert(offsetof(MonTunableParams, threshold) == 1"));
    }

    #[test]
    fn packed_and_natural_same_bytes_share_a_hash() {
        // The hash names the byte layout, not the C++ spelling: a
        // layout legal without the attribute hashes identically with
        // it, because offsets are sequential either way.
        let toml = |packed: &str| {
            format!(
                r#"
                component = "Mon"
                [structs]
                MonTunableParams = {{ category = "TUNABLE_PARAM"{packed} }}
                [[fields.MonTunableParams]]
                name = "threshold"
                type = "float"
                size = 4
                [[fields.MonTunableParams]]
                name = "count"
                type = "uint"
                size = 4
            "#
            )
        };
        let natural = parse_manifest_str(&toml("")).unwrap();
        let packed = parse_manifest_str(&toml(", packed = true")).unwrap();
        assert_eq!(
            canonical_spec("Mon", &natural.fields["MonTunableParams"], &natural.fields).unwrap(),
            canonical_spec("Mon", &packed.fields["MonTunableParams"], &packed.fields).unwrap()
        );
    }

    fn nested_manifest(count: &str) -> Manifest {
        parse_manifest_str(&format!(
            r#"
            component = "Tlm"
            [structs]
            Sub = {{ category = "STRUCT" }}
            TlmTprm = {{ category = "TUNABLE_PARAM" }}
            [[fields.Sub]]
            name = "fullUid"
            type = "uint"
            size = 4
            [[fields.Sub]]
            name = "rateDiv"
            type = "uint"
            size = 4
            default = 1
            [[fields.TlmTprm]]
            name = "collectRateHz"
            type = "uint"
            size = 4
            default = 1
            [[fields.TlmTprm]]
            name = "subs"
            type = "nested"
            struct = "Sub"{count}
        "#
        ))
        .unwrap()
    }

    #[test]
    fn nested_fields_inline_into_the_hash_and_emit_members() {
        let m = nested_manifest("\ncount = 2");
        let spec = canonical_spec("TlmTprm", &m.fields["TlmTprm"], &m.fields).unwrap();
        // Sub's leaves repeat per element at running offsets, no
        // wrapper row -- the serializer's walk over [[subs]] tables.
        assert_eq!(
            spec,
            "collectRateHz:uint:4:0;fullUid:uint:4:4;rateDiv:uint:4:8;\
             fullUid:uint:4:12;rateDiv:uint:4:16;|size:20"
        );
        let h = generate_header(&m, "TlmTprm").unwrap();
        assert!(h.contains("#include \"Sub_auto.hpp\""));
        assert!(h.contains("Sub subs[2]{};"));
        assert!(h.contains("static_assert(sizeof(TlmTprm) == 20"));
        assert!(h.contains("static_assert(offsetof(TlmTprm, subs) == 4"));

        let scalar = nested_manifest("");
        let h = generate_header(&scalar, "TlmTprm").unwrap();
        assert!(h.contains("Sub subs{};"));
        assert!(h.contains("static_assert(sizeof(TlmTprm) == 12"));
    }

    #[test]
    fn nested_agrees_with_the_serializer_walk() {
        // The value-TOML side: [[subs]] tables of annotated leaves.
        // Its emitted spec must equal the generator's canonical form.
        let m = nested_manifest("\ncount = 2");
        let value = serde_json::json!({
            "collectRateHz": { "type": "uint", "size": 4, "value": 10 },
            "subs": [
                { "fullUid": { "type": "uint", "size": 4, "value": 1 },
                  "rateDiv": { "type": "uint", "size": 4, "value": 2 } },
                { "fullUid": { "type": "uint", "size": 4, "value": 3 },
                  "rateDiv": { "type": "uint", "size": 4, "value": 4 } },
            ]
        });
        let (bytes, hash) = crate::tunable_params::binary::serialize_value_with_layout(
            &serde_json::json!({ "TlmTprm": value }),
        )
        .unwrap();
        assert_eq!(bytes.len(), 20);
        let spec = canonical_spec("TlmTprm", &m.fields["TlmTprm"], &m.fields).unwrap();
        assert_eq!(hash, super::super::payload::crc32(spec.as_bytes()));
    }

    #[test]
    fn nesting_is_one_level_only() {
        let m = parse_manifest_str(
            r#"
            component = "X"
            [structs]
            A = { category = "STRUCT" }
            B = { category = "STRUCT" }
            XTprm = { category = "TUNABLE_PARAM" }
            [[fields.A]]
            name = "v"
            type = "uint"
            size = 4
            [[fields.B]]
            name = "a"
            type = "nested"
            struct = "A"
            [[fields.XTprm]]
            name = "b"
            type = "nested"
            struct = "B"
        "#,
        )
        .unwrap();
        let err = generate_header(&m, "XTprm").unwrap_err();
        assert!(format!("{err}").contains("one level"), "{err}");
    }

    #[test]
    fn nested_needs_a_declared_struct() {
        let m = parse_manifest_str(
            r#"
            component = "X"
            [structs]
            XTprm = { category = "TUNABLE_PARAM" }
            [[fields.XTprm]]
            name = "sub"
            type = "nested"
            struct = "Ghost"
        "#,
        )
        .unwrap();
        let err = generate_header(&m, "XTprm").unwrap_err();
        assert!(format!("{err}").contains("no [[fields.Ghost]]"), "{err}");
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
        assert!(h.contains("template <typename TDerived, typename TBase> class SpecSensorSpecBase"));
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
        assert!(s.contains(
            "SpecSensorSpecBase<SpecSensor, system_core::system_component::SwModelBase>"
        ));
        assert!(s.contains("COMPONENT_ID = 212"));
        assert!(s.contains("return \"SPEC_SNS\";"));
        assert!(s.contains("#include \"SpecSensorSpecBase_auto.hpp\""));
        assert!(s.contains("onSetMode(const SetModeRequest& request)"));
        // Machinery lives in the generated base now, not the stub.
        assert!(!s.contains("paramBank_"));
        assert!(!s.contains("loadTprm(const"));
        assert!(!s.contains("doInit()"));
    }

    #[test]
    fn spec_base_reload_applies_not_republishes() {
        // RELOAD on a live bank must apply() -- publishInitial() is a
        // no-op after the first generation (the silent-no-effect
        // defect zenith isolated on 2026-08-22).
        let m = parse_manifest_str(
            r#"
            component = "X"
            component_id = 1
            [structs]
            XTunableParams = { category = "TUNABLE_PARAM" }
            [[fields.XTunableParams]]
            name = "v"
            type = "float"
            size = 4
            [[tasks]]
            name = "step"
            uid = 1
        "#,
        )
        .unwrap();
        let h = generate_cmd_base(&m).unwrap();
        assert!(h.contains("if (paramBank_.activeGeneration() == 0) {"));
        assert!(h.contains("(void)paramBank_.apply();"));
        // Tasks drive doInit; the derived method binds by name.
        assert!(h.contains("registerTask<TDerived, &TDerived::step>"));
        assert!(h.contains("STEP = 1,"));
    }

    #[test]
    fn spec_base_publishes_expected_layout_hash() {
        // Spec-born components enforce their layout hash at staged-verify
        // and verify-gated RELOAD: the base overrides expectedLayoutHash()
        // with the generated constant.
        let m = parse_manifest_str(
            r#"
            component = "X"
            component_id = 1
            [structs]
            XTunableParams = { category = "TUNABLE_PARAM" }
            [[fields.XTunableParams]]
            name = "v"
            type = "float"
            size = 4
            [[tasks]]
            name = "step"
            uid = 1
        "#,
        )
        .unwrap();
        let h = generate_cmd_base(&m).unwrap();
        assert!(h.contains("const std::uint32_t* expectedLayoutHash() const noexcept override"));
        assert!(h.contains("return &X_TUNABLE_PARAMS_LAYOUT_HASH;"));
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
