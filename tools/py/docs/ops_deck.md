# ops-deck

Generate a consolidated command/telemetry operations deck from Apex struct dictionaries.

---

## Overview

`c2-deck` produces a single Markdown document describing all registered
components, wire protocol, opcodes, and data struct layouts. The deck is
suitable for C2 developers without requiring access to Apex source or build
system.

---

## Options

```
ops-deck --db <path> [--output <path>]
```

| Option            | Description                                                                          |
| ----------------- | ------------------------------------------------------------------------------------ |
| `--db <path>`     | Path to `apex_data_db/` directory containing `.json` struct dictionaries (required). |
| `--output <path>` | Output file path. Defaults to stdout if omitted.                                     |

---

## Output Sections

The generated Markdown deck contains:

1. **Components** -- Summary table of all registered components.
2. **System Opcodes** -- Standard APROTO opcodes (0x0000-0x00FF).
3. **Commands** -- Per-component command definitions with field layout tables.
4. **Telemetry** -- Per-component telemetry definitions with field layout tables.
5. **Tunable Parameters** -- Parameters that can be written at runtime.
6. **State Data** -- Read-only component state structs.
7. **Wire Protocol** -- APROTO header format and SLIP framing reference.

---

## Examples

```bash
# Generate deck to stdout
ops-deck --db build/native-linux-debug/apex_data_db | less

# Write deck to file
ops-deck --db build/native-linux-debug/apex_data_db --output ApexHilDemo-deck.md

# Run apex-data-db first if dictionaries are not yet generated
make apex-data-db
ops-deck --db build/native-linux-debug/apex_data_db --output deck.md
```

---

## See Also

- [apex_data_gen](../../../tools/rust/docs/apex_data_gen.md) -- Generates the JSON struct dictionaries consumed by this tool.
- [ops_sdk_package.sh](../../../tools/sh/docs/ops_sdk_package.md) -- Bundles struct dictionaries and a generated deck into a deployable SDK package.
