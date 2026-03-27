# tprm_template

Generate TOML or JSON templates for tunable parameter (TPRM) configuration.

---

## Overview

`tprm_template` reads either a C++ header file or a JSON struct dictionary
(from `apex_data_gen`) and produces a TOML or JSON template with all fields
populated with placeholder values. The template is the starting point for
TPRM configuration authoring.

---

## Options

```
tprm_template (--header <file> | --json <file>) --struct <name> [options]
```

| Option                  | Description                                                                     |
| ----------------------- | ------------------------------------------------------------------------------- |
| `--header <file>`       | C++ header file to parse (required if not using `--json`).                      |
| `--json <file>`         | JSON struct dictionary from `apex_data_gen` (required if not using `--header`). |
| `--struct <name>`       | Struct name to generate a template for (required).                              |
| `--format <json\|toml>` | Output format -- required in header mode, ignored in JSON mode (always TOML).   |
| `-o, --output <file>`   | Output path. Defaults to stdout (JSON mode) or `<struct>.toml` (header mode).   |
| `--strict`              | Header mode only: fail on ambiguous constructs instead of using placeholders.   |

---

## Exit Codes

| Code | Meaning                                                 |
| ---- | ------------------------------------------------------- |
| 0    | Success.                                                |
| 1    | Parse or emit error.                                    |
| 2    | Argument error (missing required args, invalid format). |

---

## Examples

```bash
# Generate TOML template from a JSON struct dictionary (most common)
tprm_template \
  --json build/apex_data_db/MyComponent.json \
  --struct MyTunableParams \
  -o configs/my_component.toml

# Generate TOML template directly from a C++ header
tprm_template \
  --header src/sim/models/inc/ModelConfig.hpp \
  --struct ModelTunableParams \
  --format toml

# Generate JSON template from a C++ header (for tooling integrations)
tprm_template \
  --header src/sim/models/inc/ModelConfig.hpp \
  --struct ModelTunableParams \
  --format json \
  -o template.json
```

### Typical TPRM authoring workflow

```bash
# 1. Generate struct dictionaries
make apex-data-db

# 2. Generate TOML template
tprm_template \
  --json build/apex_data_db/MyComponent.json \
  --struct MyTunableParams \
  -o configs/my_component.toml

# 3. Edit the template with actual values, then convert to binary
cfg2bin --config configs/my_component.toml --output configs/my_component.tprm

# 4. Pack into a master archive
tprm_pack pack -e 0x006600:configs/my_component.tprm -o master.tprm
```

---

## See Also

- [cfg2bin](cfg2bin.md) -- Converts the generated TOML template to a binary `.tprm` file.
- [tprm_pack](tprm_pack.md) -- Packs individual `.tprm` files into a master archive.
- [apex_data_gen](apex_data_gen.md) -- Generates the JSON struct dictionaries used by JSON mode.
