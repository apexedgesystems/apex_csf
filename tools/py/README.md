# Apex Python Tools

**Location:** `tools/py/`
**Package:** `apex_tools`
**Platform:** Cross-platform Python >=3.10

Command-line tools for the Apex CSF C++ project. Python tools focus on
visualization and C2 interface generation.

---

## Quick Start

```bash
# Build tools (from project root)
make tools-py

# Source environment and run
cd build/native-linux-debug
source .env
mc-plot --help
```

The `.env` file sets `PYTHONPATH` and `PATH` so scripts can find dependencies.
The build directory is relocatable (copy anywhere, source `.env`, and run).

---

## Tools

### C2 (1)

| Tool      | Purpose                                                     | Docs                          |
| --------- | ----------------------------------------------------------- | ----------------------------- |
| `c2-deck` | Generate consolidated cmd/tlm deck from struct dictionaries | [c2_deck.md](docs/c2_deck.md) |

### Monte Carlo (1)

| Tool      | Purpose                                                            | Docs                          |
| --------- | ------------------------------------------------------------------ | ----------------------------- |
| `mc-plot` | Monte Carlo result visualization (histograms, scatter, yield, etc) | [mc_plot.md](docs/mc_plot.md) |

Benchmarking tools (`bench-plot`) have moved to the external
[Vernier](https://github.com/apexedgesystems/vernier) project.

---

## Development

### Run Tests

```bash
# From project root
make test-py

# Or directly with poetry
cd tools/py && poetry run pytest -v
```

### Local Development

```bash
cd tools/py
poetry install
poetry shell

# Now tools are available directly
mc-plot --help
```

---

## See Also

- [tools/rust/README.md](../rust/README.md) - Rust CLI tools (tprm, serial, analysis, upx)
- [tools/cpp/README.md](../cpp/README.md) - C++ diagnostic and utility tools
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance measurement framework (C++, external)
