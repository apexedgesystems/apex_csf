//! cdef_gen: generate committed .auto headers from component specs.
//!
//! For every struct with an ordered `[[fields.<Struct>]]` definition in
//! the manifest, emits `.auto/<Struct>_auto.hpp` next to the manifest:
//! the packed TParams struct, its static_assert(sizeof), and the
//! layout-hash constant the v3 payload prelude is checked against.
//!
//! Usage:
//!   cdef_gen --manifest <apex_data.toml> [--output <dir>]
//!
//! Default output is `<manifest dir>/.auto/`. check-cdef regenerates
//! into a scratch directory and diffs against the committed tree, so a
//! hand-edited generated file cannot merge. Manifests without field
//! specs produce nothing and exit success (spec adoption is
//! per-struct opt-in).

use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;

use clap::Parser;

use apex_rust_tools::tunable_params::{cdef, manifest, proto};

#[derive(Parser, Debug)]
#[command(name = "cdef_gen")]
#[command(about = "Generate committed .auto headers from component specs")]
struct Cli {
    /// Path to the component's apex_data.toml
    #[arg(long, value_name = "PATH")]
    manifest: PathBuf,

    /// Output directory (default: <manifest dir>/.auto)
    #[arg(long, short = 'o', value_name = "DIR")]
    output: Option<PathBuf>,

    /// Also emit the once-only component stub into <manifest dir>/inc
    /// (refuses to overwrite: stubs are user-owned after generation)
    #[arg(long)]
    stub: bool,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(&cli) {
        Ok(count) => {
            if count > 0 {
                println!("{count} header(s) generated");
            }
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}

fn run(cli: &Cli) -> Result<usize, Box<dyn std::error::Error>> {
    let m = manifest::parse_manifest(&cli.manifest)?;
    if m.fields.is_empty() && m.commands.is_empty() {
        return Ok(0);
    }

    let out_dir = cli.output.clone().unwrap_or_else(|| {
        cli.manifest
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .join(".auto")
    });
    fs::create_dir_all(&out_dir)?;

    let mut count = 0;
    for struct_name in m.fields.keys() {
        if !m.structs.contains_key(struct_name) {
            return Err(format!(
                "fields declared for '{struct_name}', which is not a registered struct"
            )
            .into());
        }
        let header = cdef::generate_header(&m, struct_name)?;
        let path = out_dir.join(format!("{struct_name}_auto.hpp"));
        fs::write(&path, header)?;
        println!("{}", path.display());
        count += 1;
    }

    if !m.commands.is_empty() {
        let base = cdef::generate_cmd_base(&m)?;
        let path = out_dir.join(format!("{}CmdBase_auto.hpp", m.component));
        fs::write(&path, base)?;
        println!("{}", path.display());
        count += 1;
    }

    // Every spec-defined component ships its protobuf interface --
    // the same profile ingest accepts, so the emission is also the
    // canonical form of a proto-authored spec.
    if !m.fields.is_empty() {
        let interface = proto::emit(&m)?;
        let path = out_dir.join(format!("{}.proto", m.component));
        fs::write(&path, interface)?;
        println!("{}", path.display());
        count += 1;
    }

    if cli.stub {
        let inc_dir = cli
            .manifest
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .join("inc");
        let stub_path = inc_dir.join(format!("{}.hpp", m.component));
        if stub_path.exists() {
            return Err(format!(
                "refusing to overwrite user-owned stub {}",
                stub_path.display()
            )
            .into());
        }
        fs::create_dir_all(&inc_dir)?;
        fs::write(&stub_path, cdef::generate_stub(&m)?)?;
        println!("{} (stub, user-owned from here)", stub_path.display());
        count += 1;
    }
    Ok(count)
}
