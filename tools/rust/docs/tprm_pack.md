# tprm_pack

Pack, unpack, list, and diff TPRM archives for multi-component configuration.

---

## Overview

`tprm_pack` manages packed TPRM archives -- single files that bundle tunable
parameter blobs for all components in an application. Archives are keyed by
`fullUid`, which encodes `(componentId << 8) | instanceIndex`.

---

## Commands

### pack

Combine individual `.tprm` files into a single archive.

```
tprm_pack pack -e <uid:file> [-e ...] [-r <slot:file>] [-a <slot:file>] -o <output>
```

| Option                   | Description                                                              |
| ------------------------ | ------------------------------------------------------------------------ | ------------------- |
| `-e, --entry <uid:file>` | Add a component entry. `uid` is `fullUid` in hex or decimal. Repeatable. |
| `-r, --rts <slot:file>`  | Add a Real-Time Sequence entry (`uid = 0xFF0000                          | slot`). Repeatable. |
| `-a, --ats <slot:file>`  | Add an Absolute Time Sequence entry (`uid = 0xFE0000                     | slot`). Repeatable. |
| `-o, --output <path>`    | Output packed `.tprm` file (required).                                   |

### unpack

Extract all entries from a packed archive to individual files.

```
tprm_pack unpack -i <input> -o <output-dir>
```

| Option                | Description                           |
| --------------------- | ------------------------------------- |
| `-i, --input <path>`  | Input packed `.tprm` file (required). |
| `-o, --output <path>` | Output directory (required).          |

### list

Print the entry table of a packed archive.

```
tprm_pack list -i <input>
```

| Option               | Description                           |
| -------------------- | ------------------------------------- |
| `-i, --input <path>` | Input packed `.tprm` file (required). |

### diff

Compare two packed archives and show what changed.

```
tprm_pack diff [--show-unchanged] <old.tprm> <new.tprm>
```

| Option             | Description                            |
| ------------------ | -------------------------------------- |
| `--show-unchanged` | Also print entries that are identical. |

---

## Examples

```bash
# Pack executive + scheduler + two model instances
tprm_pack pack \
  -e 0x000000:exec.tprm \
  -e 0x000100:scheduler.tprm \
  -e 0x006600:model_0.tprm \
  -e 0x006601:model_1.tprm \
  -o master.tprm

# Inspect an archive's entry table
tprm_pack list -i master.tprm

# Compare two archives before deploying
tprm_pack diff baseline.tprm candidate.tprm

# Unpack to inspect or edit individual entries
tprm_pack unpack -i master.tprm -o out/
```

---

## See Also

- [cfg2bin](cfg2bin.md) -- Produces the individual `.tprm` entries that `tprm_pack` combines.
- [tprm_template](tprm_template.md) -- Generates TOML templates for authoring those entries.
