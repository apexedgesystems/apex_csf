# How to Run ApexOpsDemo

## Overview

ApexOpsDemo is a pure SIL (Software-in-the-Loop) application that generates
configurable waveform telemetry for Zenith operations interface development
and testing. It requires no hardware -- runs entirely as a POSIX process on
any Linux host or Raspberry Pi.

## Prerequisites

- Build the project: `make compose-debug`
- Compiled TPRM archive: `apps/apex_ops_demo/tprm/master.tprm`

## Quick Start (Inside Dev Container)

```bash
# Build
make compose-debug

# Run (auto-shutdown after 30 seconds)
./build/native-linux-debug/bin/ApexOpsDemo \
  --config apps/apex_ops_demo/tprm/master.tprm \
  --shutdown-after 30

# Run indefinitely (Ctrl+C to stop)
./build/native-linux-debug/bin/ApexOpsDemo \
  --config apps/apex_ops_demo/tprm/master.tprm
```

## Operations Client Connection

While the app is running, connect from another terminal:

```python
from apex_tools.ops.client import AprotoClient

with AprotoClient("localhost", 9000) as c2:
    c2.noop()                        # Connectivity check
    c2.get_health()                  # Executive health
    c2.get_scheduler_health()        # Scheduler health
    c2.get_registry()                # Component self-description
    c2.get_data_catalog()            # Data block enumeration
    c2.inspect(0x00D000, category=2) # WaveGen#0 state
    c2.inspect(0x00D000, category=4) # WaveGen#0 output
```

## System Checkout

Run the full checkout script to verify all capabilities:

```bash
python3 apps/apex_ops_demo/scripts/checkout.py --host localhost

# Skip destructive tests (restart, library reload):
python3 apps/apex_ops_demo/scripts/checkout.py --host localhost \
  --skip-restart --skip-reload-lib
```

## Raspberry Pi Deployment

See `DEPLOY_PROCEDURE.md` for cross-compilation and RPi deployment steps.

## Zenith Integration

Generate target configs for Zenith:

```bash
# Build struct dictionaries
make apex-data-db

# Generate Zenith target directory
make zenith-target APP=ApexOpsDemo

# Output: build/native-linux-debug/zenith_targets/ApexOpsDemo/
#   app_manifest.json   Component list + protocol config
#   commands.json       Opcode table
#   telemetry.json      Default plot layouts (customize as needed)
#   structs/*.json      Struct dictionaries
```

Copy the output to `zenith/targets/<target-name>/` and add a `[[targets]]`
block to zenith's `config.toml`. Customize `telemetry.json` for preferred
plot layouts (groupings, Y-axis ranges, thresholds).

## Configuration

All runtime parameters are configured via TPRM TOML files in
`tprm/toml/`. To change waveform parameters:

1. Edit `tprm/toml/wave_gen_0.toml` (frequency, amplitude, waveType, etc.)
2. Recompile: `cfg2bin --config tprm/toml/wave_gen_0.toml --output tprm/wave_gen_0.tprm`
3. Repack: run `tprm_pack pack` with all entries (see TPRM compilation section)
4. Restart the application with the new `master.tprm`

Or reload at runtime:

```python
c2.update_tprm(0x00D000, "modified_wave_gen_0.tprm")
```

## TPRM Compilation

Individual TOML files are compiled to binary with `cfg2bin`, then packed
into a single archive with `tprm_pack`:

```bash
TOOLS=build/native-linux-debug/bin/tools/rust
DIR=apps/apex_ops_demo/tprm

# Compile each TOML to binary
$TOOLS/cfg2bin --config $DIR/toml/executive.toml     --output $DIR/executive.tprm
$TOOLS/cfg2bin --config $DIR/toml/scheduler.toml     --output $DIR/scheduler.tprm
$TOOLS/cfg2bin --config $DIR/toml/interface.toml      --output $DIR/interface.tprm
$TOOLS/cfg2bin --config $DIR/toml/wave_gen_0.toml    --output $DIR/wave_gen_0.tprm
$TOOLS/cfg2bin --config $DIR/toml/wave_gen_1.toml    --output $DIR/wave_gen_1.tprm
$TOOLS/cfg2bin --config $DIR/toml/system_monitor.toml --output $DIR/system_monitor.tprm

# Pack into archive
$TOOLS/tprm_pack pack \
  -e "0x000000:$DIR/executive.tprm" \
  -e "0x000100:$DIR/scheduler.tprm" \
  -e "0x000400:$DIR/interface.tprm" \
  -e "0x00D000:$DIR/wave_gen_0.tprm" \
  -e "0x00D001:$DIR/wave_gen_1.tprm" \
  -e "0x00C800:$DIR/system_monitor.tprm" \
  -o "$DIR/master.tprm"
```

## RTS Demo Sequences

Two sample RTS sequences are included in `tprm/rts/`:

| File                       | Description                                  |
| -------------------------- | -------------------------------------------- |
| `rts_001_noop_sweep.rts`   | Sends NOOP to each component with 1s spacing |
| `rts_002_wave_control.rts` | DATA_WRITE zeros WaveGen#0 output, waits 3s  |

Load and run via the operations client:

```python
# Upload sequence file
c2.send_file("apps/apex_ops_demo/tprm/rts/rts_001_noop_sweep.rts", "rts/noop_sweep.rts")

# Load into slot 0
c2.send_command(0x000500, 0x0500, b"\x00.apex_fs/rts/noop_sweep.rts\x00")

# Start
c2.start_rts(0)

# Stop (or let it complete)
c2.stop_rts(0)
```

## Component Map

| Component        | fullUid  | Type     | Description                |
| ---------------- | -------- | -------- | -------------------------- |
| Executive        | 0x000000 | CORE     | System executive           |
| Scheduler        | 0x000100 | CORE     | 8-task scheduler at 100 Hz |
| Interface        | 0x000400 | CORE     | TCP/SLIP on port 9000      |
| ActionEngine     | 0x000500 | CORE     | Sequences and watchpoints  |
| WaveGenerator #0 | 0x00D000 | SW_MODEL | 1 Hz sine (default)        |
| WaveGenerator #1 | 0x00D001 | SW_MODEL | 5 Hz square (default)      |
| TelemetryManager | 0x00C900 | SUPPORT  | Push telemetry dispatcher  |
| SystemMonitor    | 0x00C800 | SUPPORT  | CPU/memory health          |
| TestPlugin       | 0x00FA00 | SW_MODEL | Hot-swap test target       |

## Waveform Types

| Value | Type      | Description                                    |
| ----- | --------- | ---------------------------------------------- |
| 0     | SINE      | Standard sine wave                             |
| 1     | SQUARE    | Square wave with configurable duty cycle       |
| 2     | TRIANGLE  | Triangle wave                                  |
| 3     | SAWTOOTH  | Sawtooth (ramp) wave                           |
| 4     | COMPOSITE | Fourier composite (fundamental + 3rd harmonic) |
