use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    // Re-run the script if these change
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=CUDA_HOME");
    println!("cargo:rerun-if-env-changed=PATH");

    let feature_cuda = env::var_os("CARGO_FEATURE_CUDA").is_some();

    // Locate CUDA toolkit root (CUDA_HOME, /usr/local/cuda, or via nvcc in PATH)
    let cuda_root = locate_cuda_root();

    // If CUDA is present, expose a custom cfg the code can key on:
    if cuda_root.is_some() {
        println!("cargo:rustc-cfg=cuda_present");
    }

    match (feature_cuda, cuda_root) {
        // Feature enabled but CUDA not found → warn (build may fail if you actually link)
        (true, None) => {
            println!(
                "cargo:warning=The 'cuda' feature is enabled, but no CUDA toolkit was found. \
                 Set CUDA_HOME or install CUDA (nvcc not found)."
            );
        }
        // Feature enabled and CUDA found → add likely link search paths
        (true, Some(root)) => {
            let candidates = candidate_lib_dirs(&root);
            let mut any = false;
            for dir in candidates {
                if dir.exists() {
                    println!("cargo:rustc-link-search=native={}", dir.display());
                    any = true;
                }
            }
            if !any {
                println!(
                    "cargo:warning=CUDA found at {}, but no lib directories detected. \
                     You may need to set CUDA_HOME to the toolkit root.",
                    root.display()
                );
            }
            // If your crate (or cuda-sys) requires explicit runtime linking, uncomment:
            // println!("cargo:rustc-link-lib=dylib=cudart");
            //
            // Many *-sys crates handle linking themselves; keep this commented unless needed.
        }
        // Feature disabled: just be nice and inform if CUDA is present.
        (false, Some(_)) => {
            println!(
                "cargo:warning=CUDA toolkit detected, but the 'cuda' feature is not enabled. \
                 Build with `--features cuda` to use it."
            );
        }
        _ => { /* feature disabled & no CUDA → nothing to say */ }
    }
}

/// Try to find CUDA root directory (CUDA_HOME, common default, or nvcc path).
fn locate_cuda_root() -> Option<PathBuf> {
    // 1) Respect CUDA_HOME
    if let Ok(home) = env::var("CUDA_HOME") {
        let p = PathBuf::from(home);
        if looks_like_cuda_root(&p) {
            return Some(p);
        }
    }
    // 2) Common default symlink on Linux
    let default = Path::new("/usr/local/cuda");
    if looks_like_cuda_root(default) {
        return Some(default.to_path_buf());
    }
    // 3) Derive from nvcc on PATH
    which("nvcc").and_then(|nvcc| {
        // nvcc is typically at <root>/bin/nvcc
        let bin_dir = nvcc.parent()?.to_path_buf();
        let root = bin_dir.parent()?.to_path_buf();
        if looks_like_cuda_root(&root) {
            Some(root)
        } else {
            None
        }
    })
}

/// Quick heuristic: a CUDA root usually has a "bin/nvcc" and one of the lib dirs.
fn looks_like_cuda_root(root: &Path) -> bool {
    let nvcc = root.join("bin").join(if cfg!(target_os = "windows") {
        "nvcc.exe"
    } else {
        "nvcc"
    });
    let lib64 = root.join("lib64");
    let targets = root.join("targets").join("x86_64-linux").join("lib");
    nvcc.exists() && (lib64.exists() || targets.exists())
}

/// Generate likely library directories for modern and older layouts.
fn candidate_lib_dirs(root: &Path) -> Vec<PathBuf> {
    vec![
        // Newer layout (Deb/rpm): <root>/targets/x86_64-linux/lib
        root.join("targets").join("x86_64-linux").join("lib"),
        // Older/common layout: <root>/lib64
        root.join("lib64"),
        // Fallback some distros use: <root>/lib
        root.join("lib"),
    ]
}

/// Minimal PATH search (so we don’t need extra build-deps).
fn which(cmd: &str) -> Option<PathBuf> {
    let path = env::var_os("PATH")?;
    for p in env::split_paths(&path) {
        let candidate = p.join(cmd);
        if candidate.is_file() && is_executable(&candidate) {
            return Some(candidate);
        }
        // Windows .exe
        #[cfg(windows)]
        {
            let candidate_exe = p.join(format!("{cmd}.exe"));
            if candidate_exe.is_file() {
                return Some(candidate_exe);
            }
        }
    }
    // Fallback: try `nvcc --version` just to check availability
    if cmd == "nvcc" {
        if let Ok(ok) = Command::new("nvcc")
            .arg("--version")
            .output()
            .map(|o| o.status.success())
        {
            if ok {
                return Some(PathBuf::from("nvcc"));
            }
        }
    }
    None
}

#[cfg(unix)]
fn is_executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    fs::metadata(path)
        .map(|m| m.is_file() && (m.permissions().mode() & 0o111) != 0)
        .unwrap_or(false)
}

#[cfg(not(unix))]
fn is_executable(path: &Path) -> bool {
    // On non-Unix, assume a present file is runnable enough for our purposes.
    path.is_file()
}
