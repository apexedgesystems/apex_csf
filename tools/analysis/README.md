# tools/analysis

What inspects the data a run leaves behind: the registry and scheduler
data files and Monte Carlo results.
Nothing in the apex build depends on this role.

## Tools

Executables install to `bin/tools/analysis/` in the build directory. All
are run by hand.

| Tool            | What it does                                                             | Page                                                     |
| --------------- | ------------------------------------------------------------------------ | -------------------------------------------------------- |
| `rdat_tool`     | Registry RDAT files: info, dump, JSON export, SQLite export              | [rdat_tool](rust/apex-analysis-tools/docs/rdat_tool.md)  |
| `sdat_tool`     | Scheduler SDAT files: info, dump, JSON export                            | [sdat_tool](rust/apex-analysis-tools/docs/sdat_tool.md)  |
| `mc-plot`       | Monte Carlo results: histograms, scatter plots, yield                    | [mc-plot](py/apex_analysis/docs/mc_plot.md)              |
| `registry-info` | Registry RDAT files: components, tasks and data entries, as text or JSON | [registry-info](cpp/registry_info/docs/registry_info.md) |

How each language's units build:
[../docs/rust_workspace.md](../docs/rust_workspace.md),
[../docs/python_tools.md](../docs/python_tools.md). `registry-info` is a
C++ tool built by `apex_add_tool`: it links the apex registry library and
is compiled by the C++ build.

## Build and test

```bash
make tools-analysis              # the role's executables and wheel
make test-rust ROLES=analysis    # its Rust tests
make clippy ROLES=analysis       # its lints
make test-py                     # the Python suite
```
