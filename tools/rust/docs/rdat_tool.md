# rdat_tool

Inspect and export RDAT registry database files produced by `ApexRegistry`.

---

## Overview

`rdat_tool` parses binary RDAT files and provides human-readable, JSON, and
SQLite output modes. RDAT files contain the full component registry snapshot:
component entries (fullUid, name, type), task entries, data entries (category,
size), and a string table.

---

## Commands

### info

Print a summary of the RDAT file.

```
rdat_tool info <rdat-file>
```

Output includes: format version, flags, component/task/data counts, component
type breakdown, and data category breakdown with byte totals.

### dump

Print all entries in human-readable format.

```
rdat_tool dump [--type <type>] <rdat-file>
```

| Option              | Description                                                                                     |
| ------------------- | ----------------------------------------------------------------------------------------------- |
| `-t, --type <type>` | Filter to one component type: `EXECUTIVE`, `CORE`, `SW_MODEL`, `HW_MODEL`, `SUPPORT`, `DRIVER`. |

### json

Export the registry as JSON.

```
rdat_tool json [--pretty] <rdat-file>
```

| Option         | Description               |
| -------------- | ------------------------- |
| `-p, --pretty` | Pretty-print JSON output. |

### sqlite

Export the registry to a SQLite database.

```
rdat_tool sqlite <rdat-file> <output.db>
```

Creates tables: `metadata`, `components`, `tasks`, `data_entries`. Indexes on
`fullUid`, `component_type`, and `category` are created automatically.

---

## Reference

### Component types

| Value | Name      | Description                 |
| ----- | --------- | --------------------------- |
| 0     | EXECUTIVE | Root executive (singleton). |
| 1     | CORE      | Core infrastructure.        |
| 2     | SW_MODEL  | Software simulation models. |
| 3     | HW_MODEL  | Hardware emulation models.  |
| 4     | SUPPORT   | Runtime support services.   |
| 5     | DRIVER    | Real hardware interfaces.   |

### Data categories

| Value | Name          | Description                  |
| ----- | ------------- | ---------------------------- |
| 0     | STATIC_PARAM  | Compile-time constants.      |
| 1     | TUNABLE_PARAM | Runtime-configurable (TPRM). |
| 2     | STATE         | Runtime-mutable state.       |
| 3     | INPUT         | Input data.                  |
| 4     | OUTPUT        | Output data.                 |

---

## Examples

```bash
# Quick summary
rdat_tool info .apex_fs/db/registry.rdat

# Full dump, software models only
rdat_tool dump --type SW_MODEL .apex_fs/db/registry.rdat

# JSON export
rdat_tool json --pretty .apex_fs/db/registry.rdat > registry.json

# SQLite export for ad-hoc querying
rdat_tool sqlite .apex_fs/db/registry.rdat registry.db
sqlite3 registry.db "SELECT name, component_type FROM components"
sqlite3 registry.db "SELECT category, COUNT(*), SUM(size) FROM data_entries GROUP BY category"
```

---

## See Also

- [sdat_tool](sdat_tool.md) -- Companion tool for scheduler SDAT files.
- [registry-info](../../../tools/cpp/docs/registry_info.md) -- C++ tool with filtered views of the same RDAT format.
- [Registry Library](../../../src/system/core/components/registry/README.md) -- API documentation.
