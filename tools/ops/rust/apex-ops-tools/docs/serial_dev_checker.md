# serial_dev_checker

Scan serial devices, probe availability, and print a color-coded status summary.

---

## Overview

`serial_dev_checker` reads a TOML config listing serial ports, probes each for
accessibility and contention, and prints a status table to the terminal. By
default it uses `lsof` to detect ports held by other processes; use `--fast` to
skip that check for bulk scans.

---

## Options

```
serial_dev_checker --config <file> [options]
```

| Option            | Description                                           |
| ----------------- | ----------------------------------------------------- |
| `--config <file>` | Path to TOML config file listing ports (required).    |
| `--report <file>` | Write a plain-text status report to file.             |
| `-v, --verbosity` | Print per-device assessment during the scan.          |
| `--fast`          | Skip `lsof` and termios checks for faster bulk scans. |
| `--show-causes`   | Show trigger flags when status is `connected_in_use`. |

---

## Config Format

```toml
[[ports]]
label = "MCU A"
path  = "/dev/ttyACM0"

[[ports]]
label = "MCU B"
path  = "/dev/ttyACM1"
```

Each entry requires `path`; `label` is optional.

---

## Exit Codes

| Code | Meaning                                    |
| ---- | ------------------------------------------ |
| 0    | Success, results printed.                  |
| 1    | Config file not found or no ports defined. |

---

## Examples

```bash
# Verbose scan from a config file
serial_dev_checker --config ports.toml -v

# Write status report to file
serial_dev_checker --config ports.toml --report status.txt

# Fast scan with cause details (skips lsof)
serial_dev_checker --config ports.toml --fast --show-causes
```

Example output:

```
Serial Port Summary:
Label                Path                          Status
------------------------------------------------------------------------
MCU A                /dev/ttyACM0                  connected_in_use
MCU B                /dev/ttyACM1                  connected_openable

Status Summary:
connected_in_use    : 1
connected_openable  : 1
```

---

## See Also

- [serial_dev_tester](serial_dev_tester.md) -- Verify serial port loopback and interconnect wiring.
