# upx_tool

Batch test, compress, decompress, compare, and patch UPX-packed shared objects.

---

## Overview

`upx_tool` wraps the `upx` binary for batch operations on `.so` and `.so.upx`
files. Supports recursive directory traversal, parallel processing, output
naming templates, and a safe atomic patch flow. Requires `upx` on PATH.

---

## Commands

### test / verify

Run `upx -t` integrity checks on packed files.

```
upx_tool test <path>
upx_tool verify <path>
```

### decompress

Decompress `.upx` files to unpacked output.

```
upx_tool decompress [options] <path>
```

Output default: `foo.so.upx` -> `foo.so.unpacked`.

### compress

Compress `.so` files with UPX (default flags: `--best --lzma`).

```
upx_tool compress [--flags "<args>"] [options] <path>
```

### compare

Pair `foo.so` <-> `foo.so.upx` by basename and verify byte equivalence.

```
upx_tool compare [--print-sha] [options] <dir>
```

### patch

Safe atomic replacement: test -> optional compare to original -> backup -> decompress -> rename.
On failure, the backup is restored.

```
upx_tool patch --upx <F.upx> --target <T.so> [--orig <O.so>] [--backup-dir <dir>] [--print-sha]
```

---

## Options

These apply to `test`, `verify`, `compress`, `decompress`, and `compare`.

| Option                  | Description                                                                 |
| ----------------------- | --------------------------------------------------------------------------- |
| `--recursive`           | Recurse into subdirectories.                                                |
| `--out-dir <path>`      | Place outputs here (default: alongside inputs).                             |
| `--name-template <str>` | Output filename template. Placeholders: `{base}`, `{ext}`, `{ts}`, `{pid}`. |
| `--overwrite`           | Overwrite outputs if they exist (otherwise uniquify).                       |
| `--delete`              | Delete source file after success.                                           |
| `--archive-dir <path>`  | Archive source as `.tar.gz` after success (implies `--delete`).             |
| `--parallel`            | Enable multithreaded processing.                                            |
| `--jobs <N>`            | Thread count (>= 1); enables `--parallel` when N > 1.                       |
| `--cores <list>`        | Pin workers to CPU list, e.g. `0,1,3` (best-effort).                        |
| `--stop-on-error`       | Stop at first error.                                                        |
| `--flags "<args>"`      | Override default UPX flags (compress only).                                 |

---

## Exit Codes

| Code | Meaning                                                           |
| ---- | ----------------------------------------------------------------- |
| 0    | Completed successfully. Empty directory is a no-op and returns 0. |
| 1    | One or more failures, invalid options, or `upx` not available.    |

---

## Examples

```bash
# Test all packed libs under build/lib
upx_tool test build/lib

# Decompress a single file (-> libfoo.so.unpacked)
upx_tool decompress libfoo.so.upx

# Decompress all *.upx into a separate directory
upx_tool decompress --recursive --out-dir out/ build/lib

# Compress all .so using 8 threads
upx_tool compress --recursive --jobs 8 ./lib

# Compare pairs, print SHA256
upx_tool compare --print-sha ./lib

# Patch a target in place with backup
upx_tool patch --upx libfoo.so.upx --target /usr/local/lib/libfoo.so \
  --backup-dir backups/ --print-sha
```
