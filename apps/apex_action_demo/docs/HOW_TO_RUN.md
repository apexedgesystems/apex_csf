# How to Run ApexActionDemo

## Overview

ApexActionDemo demonstrates the action engine's telemetry monitoring,
onboard command sequencing, and runtime data mutation. A SensorModel
generates temperature telemetry. The action engine monitors it with
watchpoints and triggers RTS/ATS command sequences. DataTransform
applies byte-level fault injection via the sequencer.

No hardware dependencies -- runs as a POSIX process on any Linux host
or Raspberry Pi.

## Prerequisites

- Build the project: `make compose-debug`
- Compiled TPRM archive: `apps/apex_action_demo/tprm/master.tprm`

## Quick Start (Inside Dev Container)

```bash
# Build
make compose-debug

# Run (auto-shutdown after 30 seconds)
./build/native-linux-debug/bin/ApexActionDemo \
  --config apps/apex_action_demo/tprm/master.tprm \
  --shutdown-after 30

# Run indefinitely (Ctrl+C to stop)
./build/native-linux-debug/bin/ApexActionDemo \
  --config apps/apex_action_demo/tprm/master.tprm
```

## Operations Client Connection

While the app is running, connect from another terminal:

```python
from apex_tools.ops.client import AprotoClient

with AprotoClient("localhost", 9000) as c2:
    c2.noop()                          # Connectivity check
    c2.get_health()                    # Executive health
    c2.inspect(0x00D200, category=4)   # SensorModel OUTPUT (temp, rate)
    c2.inspect(0x000500, category=4)   # Action engine stats

    # Start an RTS by sequence ID (catalog lookup)
    import struct
    c2.send_command(0x000500, 0x0510, struct.pack("<H", 40))

    # Read DataTransform stats
    c2.inspect(0x00CA00, category=2)
```

## System Checkout

Run the full checkout script to verify all 77 capabilities:

```bash
python3 apps/apex_action_demo/scripts/checkout.py --host localhost
```

## Raspberry Pi Deployment

See `DEPLOY_PROCEDURE.md` for cross-compilation and RPi deployment steps.

## Zenith Integration

Generate target configs for Zenith:

```bash
# Build struct dictionaries
make apex-data-db

# Generate Zenith target directory
make zenith-target APP=ApexActionDemo

# Output: build/native-linux-debug/zenith_targets/ApexActionDemo/
```

Copy the output to `zenith/targets/<target-name>/` and add a `[[targets]]`
block to zenith's `config.toml`.

## Configuration

All runtime parameters are configured via TPRM TOML files in
`tprm/toml/`. To change watchpoint thresholds or sequence campaigns:

1. Edit the relevant TOML file (e.g., `tprm/toml/action.toml`)
2. Recompile: `cfg2bin --config tprm/toml/action.toml --output tprm/action.tprm`
3. Repack: run `tprm_pack pack` with all entries
4. Restart the application with the new `master.tprm`

## TPRM Compilation

```bash
TOOLS=build/native-linux-debug/bin/tools/rust
DIR=apps/apex_action_demo/tprm

# Compile each TOML to binary
$TOOLS/cfg2bin --config $DIR/toml/executive.toml      --output $DIR/executive.tprm
$TOOLS/cfg2bin --config $DIR/toml/scheduler.toml      --output $DIR/scheduler.tprm
$TOOLS/cfg2bin --config $DIR/toml/interface.toml       --output $DIR/interface.tprm
$TOOLS/cfg2bin --config $DIR/toml/action.toml          --output $DIR/action.tprm
$TOOLS/cfg2bin --config $DIR/toml/data_transform.toml  --output $DIR/data_transform.tprm
$TOOLS/cfg2bin --config $DIR/toml/system_monitor.toml  --output $DIR/system_monitor.tprm

# Compile RTS sequences
$TOOLS/cfg2bin --config $DIR/toml/rts/rts_001_fault_campaign.toml --output $DIR/rts/000.rts
$TOOLS/cfg2bin --config $DIR/toml/rts/rts_002_cleanup.toml        --output $DIR/rts/001.rts

# Pack into archive
$TOOLS/tprm_pack pack \
  -e "0x000000:$DIR/executive.tprm" \
  -e "0x000100:$DIR/scheduler.tprm" \
  -e "0x000400:$DIR/interface.tprm" \
  -e "0x000500:$DIR/action.tprm" \
  -e "0x00CA00:$DIR/data_transform.tprm" \
  -e "0x00C800:$DIR/system_monitor.tprm" \
  -o "$DIR/master.tprm"
```

## Component Map

| Component      | fullUid  | Type     | Description                                    |
| -------------- | -------- | -------- | ---------------------------------------------- |
| Executive      | 0x000000 | CORE     | System executive at 10 Hz                      |
| Scheduler      | 0x000100 | CORE     | Task scheduler                                 |
| Interface      | 0x000400 | CORE     | TCP/SLIP on port 9000                          |
| ActionEngine   | 0x000500 | CORE     | Watchpoints, sequences, event dispatch         |
| SensorModel    | 0x00D200 | SW_MODEL | Temperature ramp with overtemp detection       |
| DataTransform  | 0x00CA00 | SUPPORT  | Command-driven byte-level data mutation        |
| SystemMonitor  | 0x00C800 | SUPPORT  | CPU/memory/FD health monitoring                |
