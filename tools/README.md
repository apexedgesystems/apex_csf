# tools

Command-line tools that stand beside the apex C++ tree, built through the
same CMake configure (`tools/CMakeLists.txt`) and installed under
`bin/tools/<role>/` in the build directory.

## Layout

Every tool lives under the role that consumes it, then its language, then
one directory per unit in the language's own package shape:

| Role       | Consumer                                                                             | Installs to           |
| ---------- | ------------------------------------------------------------------------------------ | --------------------- |
| `build`    | the apex build itself: TPRM compilation and packing, spec headers, data dictionaries | `bin/tools/build/`    |
| `ops`      | operating a deployed apex system                                                     | `bin/tools/ops/`      |
| `analysis` | inspecting the data files a run leaves behind                                        | `bin/tools/analysis/` |

```
tools/
  Cargo.toml  Cargo.lock          the Rust workspace and its one lock
  pyproject.toml  uv.lock         the Python (uv) workspace and its one lock
  build/
    rust/{apex-tprm, apex-build-tools}/   cfg2bin, tprm_pack, cdef_gen, apex_data_gen
    sh/                                   the image bake, the cross setup scripts, the tools packager
    py/bin/                               gen-platform-consumers.py (a script, not a package)
    proto/apex/options.proto              the proto profile cdef_gen reads
    fixtures/check-data-db/               the data-db gate's contract fixtures
  ops/
    rust/apex-ops-tools/                  tprm_template, upx_tool, serial_dev_checker, serial_dev_tester
    py/apex_ops/                          ops-deck, zenith-target; the APROTO client and protocol
    sh/bin/                               ops_sdk_package.sh
  analysis/
    rust/apex-analysis-tools/             rdat_tool, sdat_tool
    py/apex_analysis/                     mc-plot
    cpp/registry_info/                    registry-info
```

A plain build compiles the build role only; `make tools-ops`,
`make tools-analysis` and `make tools` build the rest. Each unit keeps
its sources, tests and docs beside its manifest:

| Language | Unit                    | Shape                                                 | Built by                                                   |
| -------- | ----------------------- | ----------------------------------------------------- | ---------------------------------------------------------- |
| Rust     | a crate                 | `Cargo.toml`, `src/`, `tests/`, `docs/`               | cargo, one target dir per role                             |
| Python   | a distribution          | `pyproject.toml`, `src/<package>/`, `tests/`, `docs/` | uv, one wheel per member into `tools-wheels/<role>`        |
| C++      | a tool directory        | `CMakeLists.txt`, `src/`, `inc/`, `utst/`             | `apex_add_tool`, which derives the role from the directory |
| Shell    | a script under `sh/bin` | `bin/`, `docs/`                                       | copied as it is                                            |

Mechanics and recipes: [docs/rust_workspace.md](docs/rust_workspace.md);
the tools themselves: [docs/rust_tools.md](docs/rust_tools.md),
[docs/python_tools.md](docs/python_tools.md),
[registry-info](analysis/cpp/registry_info/docs/registry_info.md),
[ops/sh/](ops/sh/README.md).

## Adding a tool

Register it the way its language registers tools; nothing else in the
build names it.

- **Rust**: a `[[bin]]` entry and its source in the member crate of the
  right role.
- **Python**: a module in the member package and a `[project.scripts]`
  entry in that member's `pyproject.toml`.
- **C++**: a directory `tools/<role>/cpp/<tool>` with a `CMakeLists.txt`
  calling `apex_add_tool`.
- **Shell**: a file under `tools/<role>/sh/bin/`.

Then `make tools-<role>`; the executable appears in `bin/tools/<role>/`.
A tool's doc goes in its unit's `docs/` and is linked from the index for
its language.

## Adding a language under a role

The tree is built so a new language (say, Go) slots in with these steps,
every one of them:

1. **Sources**: `tools/<role>/go/<unit>/` with the language's native
   manifest, and the single project files at `tools/` if the language has
   a workspace notion.
2. **Build section**: a role loop in `tools/CMakeLists.txt` mirroring the
   shell or Python one: per role, find the units, generate the build
   script into `bin/tools/<role>`, one stamp and target
   `${PROJECT_NAME}_<role>_go_tools`, appended to `_ROLE_PARTS_<role>` so
   the role aggregate picks it up.
3. **Checks**: an executed test entry (`test-go` in `mk/test.mk`) and a
   CI classifier scope in `.github/workflows/ci.yml` if it should gate on
   its own.
4. **Hermetic bake**: install the toolchain and bake an offline module
   cache in `docker/base.Dockerfile` (the cargo and uv sections are the
   patterns), or release builds will fail offline.
5. **Release assets**: executables under `bin/tools/<role>` ride the
   apex-tools tarball automatically (`tools/build/sh/package-tools.sh`
   packages the whole tree). Only a language-native package artifact
   (like a wheel) needs a packaging change -- extend package-tools.sh,
   never a second implementation.

## Adding a role

Add it to `_APEX_TOOL_ROLES` in `tools/CMakeLists.txt` and `TOOL_ROLES` in
`mk/tools.mk`; the Rust and Python workspace member globs
(`<role>/rust/*`, `*/py/*`) take its crates and packages. Give it a CI
scope only if it should gate on its own.
