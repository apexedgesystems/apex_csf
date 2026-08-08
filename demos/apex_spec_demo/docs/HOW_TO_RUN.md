# How to Run ApexSpecDemo

## Overview

ApexSpecDemo is a pure SIL application: a single spec-born environment
sensor under the standard executive (scheduler, interface, system monitor).
It requires no hardware and runs as a POSIX process on any Linux host.

## Prerequisites

- Build the project: `make compose-debug`

The build compiles `tprm/toml/` and packs the master archive from
`tprm/tprm.manifest` (target `apex_tprm_ApexSpecDemo`, part of the default
build), so a fresh build always carries a matching TPRM:

```
build/hosted-x86_64-debug/demos/apex_spec_demo/exec/tprm/master.tprm
```

## Quick Start (Inside Dev Container)

```bash
TPRM=build/hosted-x86_64-debug/demos/apex_spec_demo/exec/tprm

# Run (auto-shutdown after 30 seconds)
./build/hosted-x86_64-debug/bin/ApexSpecDemo \
  --config $TPRM/master.tprm \
  --shutdown-after 30

# Run indefinitely (Ctrl+C to stop)
./build/hosted-x86_64-debug/bin/ApexSpecDemo \
  --config $TPRM/master.tprm
```

## System Checkout

While the app is running, from another terminal:

```bash
python3 demos/apex_spec_demo/scripts/checkout.py --host localhost
```

The checkout drives every command in the spec live -- mode transitions
(including the FAULT_INJECT guard), recalibration, stats, reset -- plus the
two negative paths (wrong-size payload, unknown opcode) and verifies each
effect through INSPECT of the spec-generated data blocks. A clean run ends
with `0 failed`.

Commands to model components ride the async queue: the wire ACK means
"accepted", and processing happens in the interface step. Effects are
therefore observed via INSPECT rather than response payloads.

## Regenerating After a Spec Change

```bash
# Regenerate .auto/ headers (structs + dispatch) in place
make cdef

# Verify committed .auto/ matches the spec (CI runs this)
make check-cdef

# Regenerate the ground dictionary
make apex-data-db
```

The stub is **not** regenerated: `cdef_gen --stub` emits
`sensor/inc/SpecSensor.hpp` only if it does not exist, and refuses to
overwrite it afterwards -- the file is user-owned. A spec change that adds a
command therefore shows up as a new pure-virtual hook in the regenerated
`.auto/` dispatch base, and the build fails until the component implements
it: the compiler is the migration checklist.

## TPRM Parameters

Authored parameter sets live in `tprm/toml/`. The sensor's set
(`spec_sensor.toml`) mirrors the spec's field shapes and constraint ranges;
`cfg2bin` stamps each payload with the spec's layout hash, and the
component's ParamBank verifies that hash at load -- a stale TPRM against a
changed spec is rejected loudly at boot (defaults still publish; the app
keeps running).
