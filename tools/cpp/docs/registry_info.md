# registry-info

Inspect RDAT registry database files produced by `ApexRegistry::exportToFile()`.

---

## Overview

`registry-info` parses binary RDAT files and prints components, tasks, and data
entries in human-readable or JSON format. Use filter flags to narrow output to a
specific section; without flags, all sections are printed.

---

## Options

```
registry-info [options] <rdat-file>
```

| Option         | Description                          |
| -------------- | ------------------------------------ |
| `--json`       | Output in JSON format.               |
| `--summary`    | Show counts only (no entry details). |
| `--components` | Show component entries only.         |
| `--tasks`      | Show task entries only.              |
| `--data`       | Show data entries only.              |
| `--help`       | Show help.                           |

---

## Output Fields

| Section    | Fields                                    |
| ---------- | ----------------------------------------- |
| Components | fullUid, componentId, instanceIndex, name |
| Tasks      | fullUid, taskUid, name                    |
| Data       | fullUid, category, name, size             |

---

## Examples

```bash
# Human-readable full dump
registry-info .apex_fs/db/registry.rdat

# Summary (counts only)
registry-info --summary .apex_fs/db/registry.rdat

# JSON export
registry-info --json .apex_fs/db/registry.rdat > registry.json

# Filter to data entries only
registry-info --data .apex_fs/db/registry.rdat
```

---

## See Also

- [rdat_tool](../../../tools/rust/docs/rdat_tool.md) -- Rust tool with additional SQLite export and richer analysis.
- [Registry Library](../../../src/system/core/components/registry/README.md) -- API documentation.
