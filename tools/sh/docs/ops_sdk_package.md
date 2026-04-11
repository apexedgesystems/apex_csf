# ops_sdk_package.sh

Operations integration SDK packager. Bundles struct dictionaries and runtime metadata into a self-contained tarball.

---

## Overview

`ops_sdk_package.sh` assembles a C2 SDK for integrators who need no access to
the Apex source code or build system. The package includes struct dictionaries,
runtime registry and scheduler exports, and a generated quick-start README
covering the SLIP framing and APROTO protocol.

---

## Options

```
ops_sdk_package.sh --app <name> --build-dir <path> [options]
```

| Option               | Description                                                 |
| -------------------- | ----------------------------------------------------------- |
| `--app <name>`       | Application name (required).                                |
| `--build-dir <path>` | Build directory (required).                                 |
| `--output <path>`    | Output directory (default: `<build-dir>/ops_sdk`).          |
| `--port <port>`      | Default TCP port to document in the README (default: 9000). |
| `--help`             | Show help.                                                  |

---

## Prerequisites

- Run `make apex-data-db` first to generate struct dictionaries.
- Run the application at least once to produce `registry.rdat` and
  `scheduler.sdat` exports (optional but recommended for the runtime/ section).

---

## Output Structure

```
ops_sdk/<app>/
  structs/*.json            Struct dictionaries (field names, types, offsets, sizes)
  runtime/registry.rdat     Component metadata (if available)
  runtime/scheduler.sdat    Task schedule (if available)
  README.md                 Quick-start guide (SLIP framing, APROTO protocol, Python examples)
ops_sdk/<app>-ops-sdk.tar.gz
```

---

## Examples

```bash
# Generate struct dictionaries, then package SDK
make apex-data-db
ops_sdk_package.sh --app ApexHilDemo --build-dir build/native-linux-debug

# Via Make convenience target
make ops-sdk APP=ApexHilDemo

# Custom port documented in the generated README
ops_sdk_package.sh --app ApexHilDemo \
  --build-dir build/native-linux-debug \
  --port 9100
```

---

## See Also

- [apex_data_gen](../../../tools/rust/docs/apex_data_gen.md) -- Generates the struct dictionaries bundled by this tool.
- [ops-deck](../../../tools/py/docs/ops_deck.md) -- Generates a human-readable command/telemetry deck from the same dictionaries.
