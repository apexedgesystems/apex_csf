# zenith-target

Generates the Zenith target configuration for one application from its
`app_data.toml` and the struct dictionaries.

---

## Overview

Reads an application's `app_data.toml` (its component manifest) and the
JSON struct dictionaries `apex_data_gen` produced, and writes a complete
target directory for the Zenith ground system: the component list with the
protocol configuration, the opcode table for the command panel, default
telemetry plot layouts, and a copy of the struct dictionaries. The core
components every apex application carries are included without being
listed.

---

## Options

```
zenith-target (--app <name> | --app-data <path>) [--apps-dir <roots>] --db <dir> --output <dir>
```

| Option               | Description                                                                         |
| -------------------- | ----------------------------------------------------------------------------------- |
| `--app <name>`       | Application name, searched for under the `--apps-dir` roots (this or `--app-data`). |
| `--app-data <path>`  | Direct path to an `app_data.toml` (this or `--app`).                                |
| `--apps-dir <roots>` | Colon-separated application roots (optional; default `demos:apps`).                 |
| `--db <dir>`         | The `apex_data_db/` directory of struct dictionaries (required).                    |
| `--output <dir>`     | Output directory for the target configuration (required).                           |
| `-h, --help`         | Print help.                                                                         |

---

## Examples

By application name, as the build runs it:

```bash
zenith-target --app ApexOpsDemo --apps-dir demos:apps \
  --db build/apex_data_db --output build/zenith_targets/ApexOpsDemo
```

From a manifest path:

```bash
zenith-target --app-data demos/apex_ops_demo/app_data.toml \
  --db build/apex_data_db --output out/
```

The output directory holds:

```
app_manifest.json   component list and protocol configuration
commands.json       opcode table for the command panel
telemetry.json      default telemetry plot layouts
structs/*.json      struct dictionaries, copied from apex_data_db
```

Through the build, which generates the dictionaries first:

```bash
make zenith-target APP=ApexOpsDemo
make zenith-validate APP=ApexOpsDemo HOST=<target>   # check it against a running target
```

---

## See Also

- [ops-deck](ops_deck.md) -- the command and telemetry deck from the same
  dictionaries
- [apex_data_gen](../../../../build/rust/apex-build-tools/docs/apex_data_gen.md)
  -- produces the struct dictionaries
