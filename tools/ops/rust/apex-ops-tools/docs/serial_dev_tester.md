# serial_dev_tester

Verify serial port loopback and interconnect wiring via configurable test pairs.

---

## Overview

`serial_dev_tester` opens serial ports and sends test messages to verify correct
wiring. Loopback tests check TX->RX on a single device; interconnect tests verify
data transfer between two ports. Results are printed with pass/fail/error status.
Test messages include random suffixes to reduce false positives.

---

## Options

```
serial_dev_tester --config <file> [options]
```

| Option              | Description                                          |
| ------------------- | ---------------------------------------------------- |
| `--config <file>`   | Path to TOML config defining test pairs (required).  |
| `--report <file>`   | Write results to file in addition to console output. |
| `-v, --verbosity`   | Enable verbose debug output.                         |
| `--timeout-ms <ms>` | Read/write timeout per test step (default: 300).     |
| `--baud <rate>`     | Baud rate for each port (default: 115200).           |

---

## Config Format

```toml
[[pairs]]
label = "MCU A Loopback"
type  = "loopback"
port  = "/dev/ttyACM0"

[[pairs]]
label = "MCU A <-> MCU B"
type  = "interconnect"
ports = ["/dev/ttyACM0", "/dev/ttyACM1"]
```

- **loopback**: requires `port` (single device with TX and RX shorted).
- **interconnect**: requires `ports` (two device paths).
- Missing or malformed fields produce `error` results.

---

## Exit Codes

| Code | Meaning                              |
| ---- | ------------------------------------ |
| 0    | All tests passed.                    |
| 1    | One or more tests failed or errored. |

---

## Examples

```bash
# Run all test pairs from config
serial_dev_tester --config pairs.toml

# Verbose run with report file
serial_dev_tester --config pairs.toml -v --report results.txt

# Custom baud and timeout
serial_dev_tester --config pairs.toml --baud 9600 --timeout-ms 500
```

Example output:

```
Loopback/Interconnect Test Results:
Label                     Type          Status     Details
---------------------------------------------------------------------------
MCU A Loopback            loopback      pass       loopback verified
MCU A <-> MCU B           interconnect  fail       /dev/ttyACM1 did not receive expected message.
```

Status values: **pass** (data received), **fail** (ports open but transfer failed),
**error** (could not open port or invalid config).

---

## See Also

- [serial_dev_checker](serial_dev_checker.md) -- Check port availability before running tests.
