# How to Run ApexTimeDemo

## Overview

`ApexTimeDemo` runs a standard `ApexExecutive` with `TimeServer` wired
to a `MockPps`. A built-in GPS simulator thread drives boot
convergence, drift accumulation, PPS dropout, and recovery scenarios.
You inspect the live state through the ops client just like any other
Apex app.

No hardware dependencies -- pure SIL on any POSIX host.

## Prerequisites

- Build the project: `make compose-debug`
- Python ops client (`apex_tools.ops.client.AprotoClient`) for
  introspection (optional but recommended).

## Quick start (inside dev container)

```bash
make compose-debug

# Run for 60 s, auto-shutdown
./build/hosted-x86_64-debug/bin/ApexTimeDemo --shutdown-after 60

# Run indefinitely (Ctrl+C to stop)
./build/hosted-x86_64-debug/bin/ApexTimeDemo
```

The executive starts on port 9000 by default (interface component's
TCP port). The GPS simulator thread starts at run-time and ticks the
scenarios according to wall-clock seconds.

## Ops-client connection

Connect from another terminal while the app is running:

```python
from apex_tools.ops.client import AprotoClient
import struct

with AprotoClient("localhost", 9000) as c2:
    c2.noop()                          # connectivity
    c2.get_health()                    # executive health

    # TimeServer (componentId=6, fullUid=0x000600)
    out = c2.inspect(0x000600, category=4)        # OUTPUT block
    tprm = c2.inspect(0x000600, category=1)       # TUNABLE_PARAM block

    # GET_TIME_STATUS opcode -- response is the TimeServerOutput block
    resp = c2.send_command(0x000600, 0x0603, b"")

    # Send a manual time set (UTC = 1700000000s in ns):
    payload = struct.pack("<q", 1_700_000_000 * 1_000_000_000)
    c2.send_command(0x000600, 0x0604, payload)    # OP_SET_TIME_MANUAL

    # Trigger a hard reset of the correlation:
    c2.send_command(0x000600, 0x0605, b"")        # OP_RESET_CORRELATION
```

## Watching the scenario timeline

Run the app and tail the system log to follow scenario transitions:

```bash
./build/hosted-x86_64-debug/bin/ApexTimeDemo --shutdown-after 35 &
tail -F .apex_fs/logs/core/system.log | grep -E "(TIME_DEMO_EXEC|GPS_SIM|TIME)"
```

Expected log entries:

- `TIME_DEMO_EXEC: TimeServer init: SUCCESS`
- `TIME_DEMO_EXEC: GPS simulator started.`
- (~3 s in) `GPS_SIM: Fix acquired; reference time delivered.`
- (~18 s in) `GPS_SIM: Simulated PPS dropout begins.`
- (~25 s in) `GPS_SIM: Recovery: resetCorrelation + fresh reference.`

## Inspecting state across scenarios

Run the app, then in a Python loop poll TimeServer's OUTPUT every
second:

```python
from apex_tools.ops.client import AprotoClient
import struct, time

with AprotoClient("localhost", 9000) as c2:
    for i in range(30):
        out = c2.inspect(0x000600, category=4)
        # First fields of TimeServerOutput: utcEpochNs (i64), metCycles (u64),
        # lastPpsLocalNs (i64), correlationOffsetNs (i64), nextToneEpochNs (i64),
        # driftEstimatePpb (i32), ppsCount (u32), correlationValid (u8),
        # timeSource (u8), timeQuality (u8), flags (u8)
        utc, met, lpps, off, ntt, drift, ppc, valid, src, qual, flg = \
            struct.unpack("<qQqqqIiBBBB", out[:56])
        print(f"t={i:2d}  utc={utc//1_000_000_000} valid={valid} "
              f"quality={qual} pps={ppc} drift={drift}")
        time.sleep(1)
```

Expected progression for a 30-second run:

```
t= 0  utc=0           valid=0 quality=0 pps=0   drift=0      # NONE
t= 1  utc=0           valid=0 quality=0 pps=0   drift=0      # NONE (still cold-start)
t= 2  utc=0           valid=0 quality=0 pps=0   drift=0      # NONE
t= 3  utc=1700000000  valid=1 quality=2 pps=1   drift=0      # FINE (first edge + ref)
t= 4  utc=1700000001  valid=1 quality=2 pps=2   drift=...    # FINE (drift filling)
t= 7  utc=1700000004  valid=1 quality=3 pps=5   drift=~0     # PRECISE
...
t=18  utc=...         valid=1 quality=3 pps=15  drift=...    # last edge before dropout
t=20  utc=...         valid=2 quality=3 pps=15  drift=...    # STALE
t=23  utc=...         valid=3 quality=1 pps=15  drift=...    # FREERUN
t=25  utc=1700000025  valid=1 quality=2 pps=1   drift=0      # recovered, FINE
t=26  utc=1700000026  valid=1 quality=2 pps=2   drift=...    # FINE
```

(Exact numbers vary by ±1 ms because the simulator runs on wall clock
not synchronized to the scheduler.)

## Args

- `--fs-root <dir>`   override Apex filesystem root (default `.apex_fs`)
- `--shutdown-after <N>`   exit after N seconds
- (Other standard ApexExecutive args inherited from the framework.)
