# How to Run ApexOpsDemo

## Overview

ApexOpsDemo is a pure SIL (Software-in-the-Loop) application that generates
configurable waveform telemetry for Zenith operations interface development
and testing. It requires no hardware -- runs entirely as a POSIX process on
any Linux host or Raspberry Pi.

## Prerequisites

- Build the project: `make compose-debug`

The build compiles `tprm/toml/` and packs the master archive from
`tprm/tprm.manifest` (target `apex_tprm_ApexOpsDemo`, part of the
default build), so a fresh build always carries a matching TPRM:

```
build/hosted-x86_64-debug/demos/apex_ops_demo/exec/tprm/master.tprm
```

## Quick Start (Inside Dev Container)

```bash
# Build
make compose-debug

TPRM=build/hosted-x86_64-debug/demos/apex_ops_demo/exec/tprm

# Run (auto-shutdown after 30 seconds)
./build/hosted-x86_64-debug/bin/ApexOpsDemo \
  --config $TPRM/master.tprm \
  --shutdown-after 30

# Run indefinitely (Ctrl+C to stop)
./build/hosted-x86_64-debug/bin/ApexOpsDemo \
  --config $TPRM/master.tprm
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
python3 demos/apex_ops_demo/scripts/checkout.py --host localhost

# Skip destructive tests (restart, library reload):
python3 demos/apex_ops_demo/scripts/checkout.py --host localhost \
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

# Output: build/hosted-x86_64-debug/zenith_targets/ApexOpsDemo/
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
`tprm/toml/`, and `tprm/tprm.manifest` is the packing recipe that maps
each fullUid to its TOML source. To change waveform parameters:

1. Edit `tprm/toml/wave_gen_0.toml` (frequency, amplitude, waveType, etc.)
2. Rebuild -- the tprm target recompiles the payload and repacks
   `master.tprm` automatically
3. Restart the application with the regenerated `master.tprm`

Adding a component to the archive is one manifest line
(`0x<fullUid>  toml/<file>.toml`); a fullUid arriving twice is a
configure error, so the manifest is also the collision check.

Or reload at runtime:

```python
c2.update_tprm(0x00D000, "modified_wave_gen_0.tprm")
```

## RTS Demo Sequences

The `[sequences]` group in `tprm/tprm.manifest` maps each slot to its
TOML source under `tprm/toml/rts/`; the build compiles the bank into
the generated tprm directory in slot naming:

| Generated file | Source                      | Description                                  |
| -------------- | --------------------------- | -------------------------------------------- |
| `rts/001.rts`  | `rts_001_noop_sweep.toml`   | Sends NOOP to each component with 1s spacing |
| `rts/002.rts`  | `rts_002_wave_control.toml` | DATA_WRITE zeros WaveGen#0 output, waits 3s  |

Load and run via the operations client:

```python
# Upload sequence file (compiled by the build)
c2.send_file(
    "build/hosted-x86_64-debug/demos/apex_ops_demo/exec/tprm/rts/001.rts",
    "rts/noop_sweep.rts",
)

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
