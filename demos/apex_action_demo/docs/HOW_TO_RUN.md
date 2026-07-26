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

The build compiles `tprm/toml/` and packs the master archive from
`tprm/tprm.manifest` (target `apex_tprm_ApexActionDemo`, part of the
default build), so a fresh build always carries a matching TPRM:

```
build/hosted-x86_64-debug/demos/apex_action_demo/exec/tprm/master.tprm
```

## Quick Start (Inside Dev Container)

```bash
# Build
make compose-debug

TPRM=build/hosted-x86_64-debug/demos/apex_action_demo/exec/tprm

# Run (auto-shutdown after 30 seconds)
./build/hosted-x86_64-debug/bin/ApexActionDemo \
  --config $TPRM/master.tprm \
  --shutdown-after 30

# Run indefinitely (Ctrl+C to stop)
./build/hosted-x86_64-debug/bin/ApexActionDemo \
  --config $TPRM/master.tprm
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
python3 demos/apex_action_demo/scripts/checkout.py --host localhost
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

# Output: build/hosted-x86_64-debug/zenith_targets/ApexActionDemo/
```

Copy the output to `zenith/targets/<target-name>/` and add a `[[targets]]`
block to zenith's `config.toml`.

## Configuration

All runtime parameters are configured via TPRM TOML files in
`tprm/toml/`, and `tprm/tprm.manifest` is the packing recipe that maps
each fullUid to its TOML source. To change watchpoint thresholds or
sequence campaigns:

1. Edit the relevant TOML file (e.g., `tprm/toml/action.toml`)
2. Rebuild -- the tprm target recompiles the payload and repacks
   `master.tprm` automatically
3. Restart the application with the regenerated `master.tprm`

Adding a component to the archive is one manifest line
(`0x<fullUid>  toml/<file>.toml`); a fullUid arriving twice is a
configure error, so the manifest is also the collision check.

## RTS Sequences

The `[sequences]` group in `tprm/tprm.manifest` maps each slot to its
TOML source under `tprm/toml/rts/`; the build compiles the bank into
the generated tprm directory in slot naming:

```
build/hosted-x86_64-debug/demos/apex_action_demo/exec/tprm/rts/000.rts  # fault campaign
build/hosted-x86_64-debug/demos/apex_action_demo/exec/tprm/rts/001.rts  # cleanup
```

The executive boot-loads sequences from `<fs-root>/bank_a/rts/`, and
the deployment stages the generated bank there, so the packaged app
boots with the full campaign resident:

```bash
# Inside the dev container: package, then boot the deployed filesystem
cmake --build build/hosted-x86_64-debug --target package_ApexActionDemo
cd build/hosted-x86_64-debug/packages/ApexActionDemo && ./run.sh
```

The quick-start invocation above runs without a staged filesystem, so
boot-time sequences (and the checkout's campaign checks) need the
packaged form; the operations client can still load sequences into
slots at runtime either way.

## Component Map

| Component     | fullUid  | Type     | Description                              |
| ------------- | -------- | -------- | ---------------------------------------- |
| Executive     | 0x000000 | CORE     | System executive at 10 Hz                |
| Scheduler     | 0x000100 | CORE     | Task scheduler                           |
| Interface     | 0x000400 | CORE     | TCP/SLIP on port 9000                    |
| ActionEngine  | 0x000500 | CORE     | Watchpoints, sequences, event dispatch   |
| SensorModel   | 0x00D200 | SW_MODEL | Temperature ramp with overtemp detection |
| DataTransform | 0x00CA00 | SUPPORT  | Command-driven byte-level data mutation  |
| SystemMonitor | 0x00C800 | SUPPORT  | CPU/memory/FD health monitoring          |
