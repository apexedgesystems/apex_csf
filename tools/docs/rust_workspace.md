# Rust tools: the workspace and how it builds

How the Rust tools are laid out, built, installed and extended. For what
each tool does, see [rust_tools.md](rust_tools.md).

---

## Shape

One Cargo workspace at `tools/` with one lock. Its members live under
`<role>/rust/<crate>`, where the role names the consumer:

| Role       | Consumer                                                                              | Crates                                    |
| ---------- | ------------------------------------------------------------------------------------- | ----------------------------------------- |
| `build`    | the apex build itself (TPRM compilation and packing, spec headers, data dictionaries) | `apex-tprm` (library), `apex-build-tools` |
| `ops`      | people and systems operating a deployed apex system                                   | `apex-ops-tools`                          |
| `analysis` | inspection of the data files a run leaves behind                                      | `apex-analysis-tools`                     |

Executables install to `bin/tools/<role>/`, so every consumer names a
role, never a language: the TPRM rules run `bin/tools/build/cfg2bin`.
Only the build role is a dependency of the apex build; the other roles
build on request.

### Source layout

```
tools/
+-- Cargo.toml                 # [workspace]: members, shared package fields,
|                              #   every dependency version, the profiles
+-- Cargo.lock                 # the one lock
+-- rustfmt.toml
+-- build/rust/
|   +-- apex-tprm/             # lib apex_tprm: cdef, manifest, proto, binary, pack, ...
|   |   +-- src/  tests/  docs/
|   \-- apex-build-tools/      # bins cfg2bin, tprm_pack, cdef_gen, apex_data_gen
|       +-- src/bin/  tests/  docs/
+-- ops/rust/
|   \-- apex-ops-tools/        # lib apex_ops_tools: serial, upx
|       +-- src/  src/bin/  tests/  docs/   # tprm_template, upx_tool, serial_dev_*
\-- analysis/rust/
    \-- apex-analysis-tools/   # lib apex_analysis_tools: registry, scheduler
        +-- src/  src/bin/  docs/           # rdat_tool, sdat_tool
```

Each crate keeps its tests and its per-tool docs beside its sources.

### Build output

```
build/hosted-x86_64-debug/
+-- .env                            # PATH lines, one per bin/tools/<dir>
+-- bin/tools/
|   +-- build/                      # cfg2bin tprm_pack cdef_gen apex_data_gen
|   +-- ops/                        # tprm_template upx_tool serial_dev_checker serial_dev_tester
|   +-- analysis/                   # rdat_tool sdat_tool
|   +-- py/  sh/  cpp/              # the other families
\-- apex_csf-rust-target/
    +-- build/  ops/  analysis/     # one cargo target dir per role
```

---

## How CMake builds it

`tools/CMakeLists.txt` holds two lists: `_APEX_TOOL_FAMILIES` (the
Python, shell and C++ tools, built by language) and `_APEX_TOOL_ROLES`
(`build ops analysis`). For each role that has crates under
`<role>/rust/`, it generates one build script and one custom command:

1. `cargo build --release -p <member>...` into
   `${CMAKE_BINARY_DIR}/apex_csf-rust-target/<role>`. The members and
   their executables come from `cargo metadata --no-deps --offline
--locked`, read once at configure: a package belongs to the role whose
   directory holds its manifest, and its `bin` targets are what the role
   installs. The manifests are configure inputs, so a manifest edit
   reconfigures before the next build.
2. Each executable cargo currently declares is copied from that target
   dir's `release/` to `bin/tools/<role>/` by name and declared as a
   byproduct, so file-level dependencies on it (the TPRM rules) resolve in
   a fresh tree. Only current bin targets are copied; a name installed by
   an earlier build is not removed (`make clean-rust` clears the role
   output dirs).
3. A stamp (`apex_csf-stamps/rust-<role>-tools.built`) records the
   build. It depends on every `*.rs` under the workspace, every member
   manifest and the lock, so an edit anywhere regenerates the roles that
   share the library; cargo is incremental past that point.

The custom target is `apex_csf_<role>_rust_tools`, one part of the role
aggregate `apex_csf_<role>_tools` that also carries the role's shell
tools. Two aggregates sit above the roles: `apex_csf_tools_default` is
in ALL and carries the build role plus the py family; `apex_csf_tools`
carries every tool. A plain
`ninja` therefore compiles the four build tools and nothing from ops or
analysis.

The CMake file knows no tool names and reads no TOML itself. A tool
joins a role by its `Cargo.toml` entry alone.

### Cargo home and offline builds

`CARGO_HOME` comes from the environment: the dev image bakes a pinned,
offline-ready registry cache and sets `CARGO_NET_OFFLINE=true`; locally
it is `~/.cargo` and builds resolve online. Target dirs are per build
tree and per role, so compiled artifacts never cross trees.

---

## Make surface

| Target                                  | Does                                                                      |
| --------------------------------------- | ------------------------------------------------------------------------- |
| `make tools-build`                      | the build role: what the apex build stands on                             |
| `make tools-ops`, `make tools-analysis` | the other roles, on request                                               |
| `make tools`                            | every tool (all roles and families)                                       |
| `make test-rust`                        | `cargo test --workspace` at `tools/`                                      |
| `make clippy`                           | `cargo clippy --workspace --all-targets --locked -- -D warnings`          |
| `make compat-tprm`                      | the TPRM contract: `-p apex-tprm --test golden_vectors` plus the C++ side |
| `make coverage-rust`                    | `cargo llvm-cov --workspace`; report at `tools/coverage-rust.lcov`        |
| `make clean-rust`                       | the role output dirs, the per-role target dirs and `tools/target`         |

The gates stand on the build role: `apex-data-db`, `cdef`, `check-cdef`,
`check-data-db` and `check-data-db-contract` list `tools-build` as their
prerequisite; `tprm-templates` lists `tools-ops` for `tprm_template`.

Each `tools-<name>` target configures the tree if needed, runs
`ninja build.ninja` so the graph reflects the current CMakeLists, then
builds `apex_csf_<name>_tools` if that target exists.

---

## The .env file

```bash
# build/hosted-x86_64-debug/.env
export PATH="$PWD/bin/tools/build:$PATH"
export PATH="$PWD/bin/tools/ops:$PATH"
export PATH="$PWD/bin/tools/analysis:$PATH"
export PATH="$PWD/bin/tools/py:$PATH"
export PYTHONPATH="$PWD/lib/python:$PYTHONPATH"
```

Lines are appended once per `bin/tools/<dir>` by the env helper; `$PWD`
keeps the build directory relocatable. Source it from the build
directory:

```bash
make tools
cd build/hosted-x86_64-debug && source .env
cfg2bin --help
rdat_tool --help
```

---

## Dependencies and the lock

Every external dependency's version is written once, in
`[workspace.dependencies]` of `tools/Cargo.toml`; members take it with
`name = { workspace = true }`. Dependabot watches `/tools`. The image
fetch layer in `docker/base.Dockerfile` copies the root manifest, the
lock and each member's `Cargo.toml` (no sources) and runs
`cargo fetch --locked`, which is why members declare their targets with
explicit `path`s: cargo resolves the workspace from manifests alone. CI
rebuilds the gate image when `tools/Cargo.*` or a member manifest
changes.

Measured on x86_64 Linux as distinct normal-edge packages, local members
included (`cargo tree --locked -e normal --prefix none <selection> | sed
'/^$/d' | sed 's/ (.*//' | sort -u | wc -l`): the build role resolves
44, ops 61, analysis 34, the whole workspace 69. Bundled SQLite, the
serial stack and the hashing crates belong to the ops and analysis roles
only.

---

## Adding things

### A tool to an existing crate

1. Write `src/bin/<tool>.rs` in the member crate.
2. Add a `[[bin]]` entry with `name` and `path` to that crate's
   `Cargo.toml`. If it needs a crate not yet in the workspace, add the
   version to `[workspace.dependencies]` and reference it with
   `{ workspace = true }`.
3. `make tools-<role>`; the executable appears in `bin/tools/<role>/`.
4. Document it in the crate's `docs/<tool>.md` and link it from
   [rust_tools.md](rust_tools.md).

### A member crate

1. Create `tools/<role>/rust/<crate>/Cargo.toml` inheriting the shared
   fields (`version.workspace = true`, and likewise edition, authors,
   license) and declaring its `[lib]` and `[[bin]]` targets with explicit
   paths. The `members` globs pick it up.
2. Add one `COPY` line for its `Cargo.toml` to the rust fetch layer in
   `docker/base.Dockerfile`, next to the existing members.
3. `cargo metadata` at `tools/` updates the lock.

### A role

1. Add `"<role>/rust/*"` to `members` in `tools/Cargo.toml`, so the
   role's crates are workspace members and inherit the shared fields.
2. Add the role to `_APEX_TOOL_ROLES` in `tools/CMakeLists.txt` and to
   `TOOL_ROLES` in `mk/tools.mk`.
3. Follow the member-crate steps above for its first crate; give the
   role a CI scope in `.github/workflows/ci.yml` only if it should gate
   on its own.

---

## Testing

```bash
make test-rust                       # every member
cd tools && cargo test -p apex-tprm  # one member
cd tools && cargo test -p apex-build-tools --test apex_data_gen
```

Integration tests that run a binary live with the crate that builds it
(`proto_spec` runs `cdef_gen`, so it sits in `apex-build-tools`).
`apex-tprm`'s `golden_vectors` reads `compat/tprm` four levels up.

---

## Troubleshooting

### "Command not found: cfg2bin"

Source `.env` from the build directory, or call the tool by its role
path: `build/hosted-x86_64-debug/bin/tools/build/cfg2bin`.

### "No build tools to build"

The configure skipped the tools: cargo is not on PATH, or the tree was
configured with `APEX_BUILD_TOOLS=OFF`. A tools-enabled configure
without cargo fails at the TPRM rules instead, naming the missing target.

### "Cargo not found"

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Stale binaries after moving crates

```bash
make clean-rust
make tools
```

---

## See also

- [rust_tools.md](rust_tools.md): what each tool does, by role
- [tools/CMakeLists.txt](../CMakeLists.txt), [mk/tools.mk](../../mk/tools.mk)
- [proto_profile.md](../build/rust/apex-tprm/docs/proto_profile.md): the
  proto subset `cdef_gen` reads
