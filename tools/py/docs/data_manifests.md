# data_manifests.py

List the component data manifests the dictionary generator reads, checked against what CMake registers.

---

## Overview

`data_manifests.py` finds every `*_data.toml` or `apex_data.toml` under the
given roots, outside `build/` and hidden directories, and keeps the ones whose
parsed TOML carries a top-level `component` key; a file carrying `application`
is an application descriptor and is skipped, and one carrying neither or both
is an error. By default the result must equal the manifests the CMakeLists.txt
files under the same roots register with `apex_add_data_manifest(...)`, read as
the helper reads them through a small CMake lexer: no arguments for the default
`apex_data.toml` beside the file, `MANIFEST <path>` quoted or unquoted with
`${CMAKE_CURRENT_SOURCE_DIR}` resolved, line and bracket comments hiding a call
(an opener inside a comment or a quoted value is text), and any other argument
shape an error. Every registered file must exist and the inventory must not be
empty. `make check-data-db` runs it first and generates every manifest it
lists; a problem prints a message and exits 1.

---

## Options

```
data_manifests.py --roots <dir> [<dir> ...] [--no-registration-check]
```

| Option                      | Description                                                             |
| --------------------------- | ----------------------------------------------------------------------- |
| `--roots <dir> [<dir> ...]` | Directories searched for manifests and CMakeLists.txt files (required). |
| `--no-registration-check`   | List the manifests without comparing them to the CMake registrations.   |

---

## Examples

```bash
# One manifest path per line, exit 1 with a message on any problem
python3 tools/py/src/apex_tools/ops/data_manifests.py --roots demos src apps

# Discovery only, without the registration comparison
python3 tools/py/src/apex_tools/ops/data_manifests.py --roots demos src apps \
  --no-registration-check

# The gate that consumes the inventory
make check-data-db
```

---

## See Also

- [apex_data_gen](../../rust/docs/apex_data_gen.md) -- Generates the JSON struct dictionary from each manifest this tool lists.
