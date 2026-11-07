# Apex Python Tools

**Location:** one uv workspace at `tools/` with a lock; `apex_ops` under `tools/ops/py/apex_ops`, `apex_analysis` under `tools/analysis/py/apex_analysis`
**Packages:** `apex_ops`, `apex_analysis`
**Platform:** Cross-platform Python >=3.11

Command-line tools for the Apex CSF C++ project. Python tools focus on
visualization and operations interface generation.

---

## Quick Start

```bash
# Build the roles that carry Python tools (from project root)
make tools-ops tools-analysis

# Source environment and run
cd build/hosted-x86_64-debug
source .env
mc-plot --help
```

The `.env` file sets `PYTHONPATH` and `PATH` so scripts can find dependencies.
The build directory is relocatable (copy anywhere, source `.env`, and run).

---

## Tools

### Operations (1)

| Tool       | Purpose                                                     | Docs                                               |
| ---------- | ----------------------------------------------------------- | -------------------------------------------------- |
| `ops-deck` | Generate consolidated cmd/tlm deck from struct dictionaries | [ops_deck.md](../ops/py/apex_ops/docs/ops_deck.md) |

### Monte Carlo (1)

| Tool      | Purpose                                                            | Docs                                                       |
| --------- | ------------------------------------------------------------------ | ---------------------------------------------------------- |
| `mc-plot` | Monte Carlo result visualization (histograms, scatter, yield, etc) | [mc_plot.md](../analysis/py/apex_analysis/docs/mc_plot.md) |

Benchmarking tools (`bench-plot`) have moved to the external
[Vernier](https://github.com/apexedgesystems/vernier) project.

---

## Development

### Run Tests

```bash
# From project root
make test-py

# Or directly with uv
cd tools && uv run --all-packages pytest -v
```

### Local Development

```bash
cd tools
uv sync --all-packages
uv run <command>  # or: source .venv/bin/activate

# Now tools are available directly
mc-plot --help
```

---

## The workspace and the lock

`tools/pyproject.toml` is the workspace root and not itself a package;
its `members` glob `*/py/*` spans every role, and `tools/uv.lock` pins
the whole workspace. Every consumer reads the lock, not the manifests:
CMake's configure takes the member list from `uv export --frozen` (a
member's role is its directory), the image bake syncs the locked
dependencies, the release counts the wheels it expects from the same
export, and `make test-py` begins with `uv lock --check`. So a manifest
edit (a new member, a changed `dependencies` entry) exists to the build
once `uv lock` has recorded it and the lock is committed with it; until
then CMake does not see the member and `make test-py` fails on the stale
lock. `uv lock` is the one step that resolves against the index, so it
runs with network; the images are offline and only read the lock.

---

## Adding things

### A tool to an existing member

1. Write the module under `src/<package>/` with a `main()`.
2. Add a `[project.scripts]` entry, `<tool> = "<package>.<module>:main"`,
   to the member's `pyproject.toml`. A console script changes no
   dependency, so the lock stays as it is.
3. `make tools-<role>`; the executable appears in `bin/tools/<role>/`.
4. Document it in the member's `docs/<tool>.md` and link it from the
   tables above.

### A member package

`ops/py/apex_ops` is the pattern.

1. Create `tools/<role>/py/<package>/pyproject.toml`: the `[project]`
   table (`name`, `version`, `requires-python = ">=3.11"`,
   `dependencies`), its `[project.scripts]`, the hatchling
   `[build-system]`, `[tool.hatch.build.targets.wheel]` naming
   `src/<package>`, and the `[tool.uv] cache-keys` entry so a frozen
   sync rebuilds the wheel when a source changes. Sources go in
   `src/<package>/`, tests in `tests/`, docs in `docs/`. The workspace
   glob picks the directory up; nothing lists members by hand.
2. `cd tools && uv lock`, and commit `uv.lock` with the member. This is
   what makes the member exist to CMake, the bake and the release (the
   lock section above).
3. Add one `COPY` line for its `pyproject.toml` to the uv layer in
   `docker/base.Dockerfile`, next to the existing members, so the image's
   offline cache resolves it.
4. Add `--cov=<package>` to `coverage-py` in `mk/test.mk`.
5. `make tools-<role>`; its console scripts appear in `bin/tools/<role>/`
   and its wheel in `tools-wheels/<role>/`.

### A dependency

Add it to the member's `dependencies` with a bounded version, then
`cd tools && uv lock` and commit the lock. The frozen consumers above
resolve it from the lock; the image bake caches it on the next image
build.

### A role

1. Add the role to `_APEX_TOOL_ROLES` in `tools/CMakeLists.txt` and to
   `TOOL_ROLES` in `mk/tools.mk`. The `*/py/*` glob already spans it.
2. Follow the member-package steps above for its first package; give the
   role a CI scope in `.github/workflows/ci.yml` only if it should gate
   on its own.

---

## See Also

- [rust_tools.md](rust_tools.md) - Rust CLI tools (tprm, serial, analysis, upx)
- [registry-info](../analysis/cpp/registry_info/docs/registry_info.md) - C++ registry inspector
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance measurement framework (C++, external)
