# apex_data_gen

Generate JSON struct dictionaries from `apex_data.toml` manifests.

---

## Overview

`apex_data_gen` parses C++ headers referenced by an `apex_data.toml` manifest
and produces one `.json` file per component. Each JSON file describes all
registered structs (field names, types, byte offsets, sizes) and enums. These
dictionaries are consumed by `tprm_template`, `c2-deck`, and `c2_sdk_package.sh`.

---

## Options

```
apex_data_gen --manifest <path> --output <path> [--pretty]
```

| Option              | Description                                                       |
| ------------------- | ----------------------------------------------------------------- |
| `--manifest <path>` | Path to `apex_data.toml` manifest file (required).                |
| `--output <path>`   | Output directory for `.json` files, or `-` for stdout (required). |
| `-p, --pretty`      | Pretty-print JSON output.                                         |

---

## Field definitions (cdef)

An optional ordered `[[fields.<StructName>]]` array makes the spec the
source of truth for that struct: the committed `.auto` header (packed
struct, `static_assert(sizeof)`, layout-hash constant) generates from
it via `cdef_gen`, the dictionary entry derives from it directly
(`spec_defined: true`, no header parse), and `make check-cdef`
regenerates and diffs so hand edits to generated files cannot merge.
Each entry carries `name`, `type`, `size`, optional `count` (array
length), `default`, and `doc`; array order is layout order. The
component-level `namespace` key names the C++ namespace generated
headers open.

## Command and telemetry declarations

Optional `[[commands]]` / `[[telemetry]]` arrays declare the
component's C2 surface in the same spec the dispatch base generates
from, so the dictionary names exactly the opcodes the component
handles. Commands carry `name`, `opcode`, optional `request` /
`response` payload struct names, and `doc`; telemetry entries carry
`name`, `opcode`, `struct`, and `doc`. Payload structs must be
registered structs (their layouts are ordinary `[[fields]]` specs);
a dangling reference fails generation. The sections appear in the
JSON as `commands` and `telemetry` arrays when present.

A manifest whose registered structs are all spec-defined needs no C++
headers at all -- fully spec-born components generate their dictionary
from the manifest alone.

## Proto-authored specs

A component-level `proto_spec` key references a profile `.proto` file
whose messages populate the field specs instead of inline `[[fields]]`
arrays (one definition source per component -- both present is an
error). Every spec-defined component also emits its canonical
interface as `.auto/<Component>.proto`. See
[proto_profile.md](proto_profile.md) for the profile.

## Constraint declarations

An optional `[constraints.<StructName>]` section declares each field's
legal range once at the component level -- `min`/`max` (inclusive),
`allowed` (explicit value list), `step` (granularity from `min`):

```toml
[constraints.WaveGenTunableParams]
frequency = { min = 0.0, max = 50.0 }
waveType = { allowed = [0, 1, 2, 3, 4] }
```

The declaration flows into the dictionary JSON per field (a
`constraints` object the ground UI reads), into generated authoring
templates via `tprm_template`, and into the on-board constraint tables
the build compiles (see `cmake/apex/Tprm.cmake`). Constraints naming an
unregistered struct or an unknown field fail generation.

## Examples

```bash
# Generate struct dictionaries into the build directory
apex_data_gen \
  --manifest demos/apex_hil_demo/apex_data.toml \
  --output build/hosted-x86_64-debug/apex_data_db

# Inspect a single component's output
apex_data_gen \
  --manifest demos/apex_hil_demo/apex_data.toml \
  --output - \
  --pretty | less

# Via the Make convenience target
make apex-data-db
```

---

## See Also

- [tprm_template](tprm_template.md) -- Consumes these JSON dictionaries to generate TOML config templates.
- [c2-deck](../../../tools/py/docs/c2_deck.md) -- Generates a command/telemetry deck from these dictionaries.
- [c2_sdk_package.sh](../../../tools/sh/docs/c2_sdk_package.md) -- Bundles these dictionaries into a deployable C2 SDK.
