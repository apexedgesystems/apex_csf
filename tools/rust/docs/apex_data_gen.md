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

## Examples

```bash
# Generate struct dictionaries into the build directory
apex_data_gen \
  --manifest apps/apex_hil_demo/apex_data.toml \
  --output build/native-linux-debug/apex_data_db

# Inspect a single component's output
apex_data_gen \
  --manifest apps/apex_hil_demo/apex_data.toml \
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
