# tools

Command-line tools that stand beside the apex C++ tree, built through the
same CMake configure (`tools/CMakeLists.txt`) and installed under
`bin/tools/` in the build directory.

## Layout

Rust tools are one Cargo workspace at `tools/` whose crates sit under the
role that consumes them, and they install by role:

| Role       | Consumer                                                                             | Installs to           |
| ---------- | ------------------------------------------------------------------------------------ | --------------------- |
| `build`    | the apex build itself: TPRM compilation and packing, spec headers, data dictionaries | `bin/tools/build/`    |
| `ops`      | operating a deployed apex system                                                     | `bin/tools/ops/`      |
| `analysis` | inspecting the data files a run leaves behind                                        | `bin/tools/analysis/` |

A plain build compiles the build role only; `make tools-ops`,
`make tools-analysis` and `make tools` build the rest. See
[docs/rust_workspace.md](docs/rust_workspace.md) for the mechanics and
[docs/rust_tools.md](docs/rust_tools.md) for the tools.

Python tools are one uv workspace at `tools/` with members under their
role ([docs/python_tools.md](docs/python_tools.md)); shell and C++ tools
live under their role too: [ops/sh/](ops/sh/README.md),
[registry-info](analysis/cpp/registry_info/docs/registry_info.md).

## Adding a Rust tool

A `[[bin]]` entry in the member crate of the right role is the whole
registration; `make tools-<role>` builds it into `bin/tools/<role>/`.
Steps, including a new member crate or a new role, in
[docs/rust_workspace.md](docs/rust_workspace.md#adding-things).

## Adding a tool family

The tree is built so a new language family (say, Go) slots in with these
steps -- every one of them, honestly:

1. **Sources**: `tools/go/` with its native manifest (`go.mod`).
2. **Build section**: add a gated block in `tools/CMakeLists.txt` mirroring
   the python or shell one (find the toolchain, generated build script into
   `bin/tools/go`, `add_custom_target(${PROJECT_NAME}_go_tools)`, append to
   `_TOOLS_DEPS`) and add `go` to `_APEX_TOOL_FAMILIES` at the top -- the
   env helper picks up the PATH entry from the list.
3. **Make surface**: add `go` to `TOOL_FAMILIES` in `mk/tools.mk`
   (`tools-go` and the `tools` aggregate follow), and give the family an
   executed check (`test-go` in `mk/test.mk`, plus a CI classifier scope
   in `.github/workflows/ci.yml` if it should gate independently).
4. **Hermetic bake**: install the toolchain and bake an offline module
   cache in `docker/base.Dockerfile` (the cargo/uv sections are the
   patterns), or release builds will fail offline.
5. **Release assets**: executables under `bin/tools/go` ride the
   apex-tools tarball automatically (`tools/build/sh/package-tools.sh`
   packages the whole tree). Only a language-native package artifact
   (like the python wheel) needs a packaging change -- extend
   package-tools.sh, never a second implementation.
