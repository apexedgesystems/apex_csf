# sdat_tool

Inspect and export SDAT scheduler database files produced by the Apex scheduler.

---

## Overview

`sdat_tool` parses binary SDAT files and provides human-readable, JSON, and
summary output modes. SDAT files contain the full scheduler snapshot: task
entries (fullUid, frequency, priority, pool, sequencing info) and the per-tick
execution schedule.

---

## Commands

### info

Print a summary of the SDAT file.

```
sdat_tool info <sdat-file>
```

Output includes: version, component/task counts, fundamental frequency, tick
count, task statistics (total, active, max/avg per tick), frequency
distribution, and thread pool distribution.

### dump

Print all entries in human-readable format.

```
sdat_tool dump <sdat-file>
```

Output includes: header fields, full task table with all metadata, and the
per-tick execution schedule.

### json

Export the schedule as JSON.

```
sdat_tool json [--pretty] <sdat-file>
```

| Option         | Description               |
| -------------- | ------------------------- |
| `-p, --pretty` | Pretty-print JSON output. |

JSON output includes: version, flags, fundamental frequency, tick count, tasks
array, and tick schedule array.

---

## Examples

```bash
# Quick summary
sdat_tool info .apex_fs/db/scheduler.sdat

# Full dump for inspecting task ordering
sdat_tool dump .apex_fs/db/scheduler.sdat

# JSON export for external processing
sdat_tool json --pretty .apex_fs/db/scheduler.sdat > schedule.json
```

---

## See Also

- [rdat_tool](rdat_tool.md) -- Companion tool for component registry RDAT files.
