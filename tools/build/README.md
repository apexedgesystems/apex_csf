# tools/build

What the apex build, its gates and its images invoke. A plain build depends
on this role and no other: `make tools-build` is the only tool prerequisite
of the C++ build, and it is what the gate images are baked for.

---

## Tools

Executables install to `bin/tools/build/` in the build directory.

| Tool            | What it does                                                              | Invoked by                                                               | Page                                                         |
| --------------- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------ |
| `cfg2bin`       | Converts a TOML or JSON parameter file to its binary TPRM blob            | `cmake/apex/Tprm.cmake`, `cmake/apex/TprmConstraintGen.cmake`            | [cfg2bin](rust/apex-build-tools/docs/cfg2bin.md)             |
| `tprm_pack`     | Packs, unpacks, lists and diffs the TPRM archive of a configuration       | `cmake/apex/Tprm.cmake`                                                  | [tprm_pack](rust/apex-build-tools/docs/tprm_pack.md)         |
| `cdef_gen`      | Generates a component's `.auto` headers from its spec                     | `make cdef`, `make check-cdef` (the gate that pins spec to header)       | [cdef_gen](rust/apex-build-tools/docs/cdef_gen.md)           |
| `apex_data_gen` | Generates JSON struct dictionaries from registered `apex_data.toml` files | `cmake/apex/DataDefinitions.cmake`, `make apex-data-db`, `check-data-db` | [apex_data_gen](rust/apex-build-tools/docs/apex_data_gen.md) |

The four share the library crate `apex-tprm`
([proto profile](rust/apex-tprm/docs/proto_profile.md), which `cdef_gen`
reads). How the crates build and where they install:
[../docs/rust_workspace.md](../docs/rust_workspace.md).

---

## Scripts

Run in place from the source tree; nothing installs them.

| Script                             | What it does                                                                   | Invoked by                                                                |
| ---------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------- |
| `sh/bake-external-deps.sh`         | Clones each pinned FetchContent dependency into the image for offline builds   | `docker/base.Dockerfile` (the `bakes` stage)                              |
| `sh/setup-cross-apt.sh`            | Configures apt for a foreign architecture's packages                           | the rpi, jetson and riscv64 Dockerfiles under `docker/dev/`               |
| `sh/setup-cross-toolchain.sh`      | Installs a cross-compilation toolchain for one architecture                    | the same three Dockerfiles                                                |
| `sh/setup-sysroot.sh`              | Installs the foreign-architecture libraries the cross builds link              | the same three Dockerfiles                                                |
| `sh/package-tools.sh`              | Packages the tools' outputs into the release assets                            | `make artifact-tools` (local and release); `make test-sh` exercises it    |
| `py/bin/gen-platform-consumers.py` | Generates the per-platform sections of the compose files and the publish graph | `make regen-platforms`, `make check-platforms`                            |
| `py/bin/ci-classify.py`            | Decides which CI jobs a change runs and which images it rebuilds               | `.github/workflows/ci.yml`, `docker-images.yml`, `make check-ci-classify` |

A script copied into an image is an input of that image: changing it
rebuilds the images that copy it (see
[What CI runs for a tools change](../README.md#what-ci-runs-for-a-tools-change)).

---

## Also here

- `proto/apex/options.proto`: the proto profile's option definitions.
- `fixtures/check-data-db/`: the contract fixtures of the dictionary gate.
- `fixtures/ci-classify/`: the change classifier's cases and fixture
  Dockerfiles.

---

## Build and test

```bash
make tools-build                 # the four executables
make test-rust ROLES=build       # the role's Rust tests
make clippy ROLES=build          # its lints
make check-cdef                  # specs against their committed .auto headers
make check-data-db               # the registered dictionaries generate clean
make check-ci-classify           # the classifier against its cases
make check-platforms             # the generated sections against the registry
make test-sh                     # the tools packager's regression
```
