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
cd tools/py && uv run pytest -v
```

### Local Development

```bash
cd tools/py
uv sync
uv run <command>  # or: source .venv/bin/activate

# Now tools are available directly
mc-plot --help
```

---

## See Also

- [rust_tools.md](rust_tools.md) - Rust CLI tools (tprm, serial, analysis, upx)
- [registry-info](../analysis/cpp/registry_info/docs/registry_info.md) - C++ registry inspector
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance measurement framework (C++, external)
