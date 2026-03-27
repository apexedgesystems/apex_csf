# c2_sdk_package.sh

C2 integration SDK packager. Bundles struct dictionaries and runtime metadata into a self-contained tarball.

---

## Overview

`c2_sdk_package.sh` assembles a C2 SDK for integrators who need no access to
the Apex source code or build system. The package includes struct dictionaries,
runtime registry and scheduler exports, and a generated quick-start README
covering the SLIP framing and APROTO protocol.

---

## Options

```
c2_sdk_package.sh --app <name> --build-dir <path> [options]
```

| Option               | Description                                                 |
| -------------------- | ----------------------------------------------------------- |
| `--app <name>`       | Application name (required).                                |
| `--build-dir <path>` | Build directory (required).                                 |
| `--output <path>`    | Output directory (default: `<build-dir>/c2_sdk`).           |
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
c2_sdk/<app>/
  structs/*.json            Struct dictionaries (field names, types, offsets, sizes)
  runtime/registry.rdat     Component metadata (if available)
  runtime/scheduler.sdat    Task schedule (if available)
  README.md                 Quick-start guide (SLIP framing, APROTO protocol, Python examples)
c2_sdk/<app>-c2-sdk.tar.gz
```

---

## Examples

```bash
# Generate struct dictionaries, then package SDK
make apex-data-db
c2_sdk_package.sh --app ApexHilDemo --build-dir build/native-linux-debug

# Via Make convenience target
make c2-sdk APP=ApexHilDemo

# Custom port documented in the generated README
c2_sdk_package.sh --app ApexHilDemo \
  --build-dir build/native-linux-debug \
  --port 9100
```

---

## See Also

- [apex_data_gen](../../../tools/rust/docs/apex_data_gen.md) -- Generates the struct dictionaries bundled by this tool.
- [c2-deck](../../../tools/py/docs/c2_deck.md) -- Generates a human-readable command/telemetry deck from the same dictionaries.
