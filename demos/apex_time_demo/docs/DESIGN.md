# Time Demo Design

How `ApexTimeDemo` exercises the TimeServer / IPps stack inside a
real `ApexExecutive` deployment.

## Goal

Show every meaningful state of the PPS time-distribution subsystem
without requiring a real GPS receiver, /dev/pps device, or hardware
wiring. The demo must run identically on any POSIX host (dev
container, Jetson, RPi, generic Linux) and exit cleanly via the
standard executive shutdown path.

## Components

| Component     | Owner              | Wiring                                          |
| ------------- | ------------------ | ----------------------------------------------- |
| TimeServer    | base ApexExecutive | Auto-registered (componentId=6).                |
| MockPps       | TimeDemoExecutive  | `setPpsSource(&pps_)` in registerComponents.    |
| SystemMonitor | TimeDemoExecutive  | `registerComponent(&sysMonitor_, ...)`.         |
| GPS simulator | TimeDemoExecutive  | `std::thread` started in `configureComponents`. |

## Why a thread for the simulator

TimeServer's correlation depends on edges arriving roughly 1 Hz over
wall-clock time, with deliberate gaps to trigger STALE / FREERUN. The
executive's frame loop runs at 100 Hz; we don't want to inject edges
from inside `tick()` because the 1-Hz pacing would couple to the
scheduler frequency. A separate thread that sleeps 1 s between
injections gives wall-clock pacing independent of frame rate.

The thread:

- Captures `this` so it can call `pps_.injectEdge()` and
  `timeServer().handleSetReferenceTime()`.
- Reads `simRunning_` (atomic bool) each loop. Destructor sets it
  false and joins.
- Records its start time once and computes "seconds since start" each
  pass to drive the scenario timeline.

## Scenario timeline

```
seconds   simulator action                         observable state
-------   ---------------------------------------- ----------------
0..3      idle (cold-start dark period)            valid=NONE
3         SET_REFERENCE_TIME(1700000000s);
          inject first PPS edge.                   valid=VALID, quality=FINE
4..18     1 Hz edges; SET_REFERENCE_TIME not       quality=FINE -> PRECISE
          repeated.                                (after driftFilterTaps=4 edges)
18..25    no edges (dropout window)                STALE around t=20,
                                                    FREERUN around t=23
                                                    (TPRM holdoverLimitS=5)
25        resetCorrelation;
          SET_REFERENCE_TIME(1700000025s);
          resume 1 Hz edges.                       valid=VALID, quality=FINE
25..      1 Hz edges                               quality returns to PRECISE
                                                    after 4 more edges.
```

### Why the thresholds work out

The TPRM the demo uses (defaults in TimeServer except
`holdoverLimitS=5` for visibility):

```
maxStalenessUs   = 1'500'000  (1.5 s)
holdoverLimitS   = 5          (5 s for the demo; default 60)
driftFilterTaps  = 4          (so PRECISE arrives after 4 edges)
```

The 7-second dropout (18..25 s) overshoots both thresholds: 1.5 s in,
valid drops to STALE; 5 s in, FREERUN. The 1-Hz edge cadence with 4
taps means PRECISE returns within 4 s of recovery.

## Why not load a TPRM file

ApexExecutive accepts a `--config <master.tprm>` flag for
TPRM-driven configuration. The demo deliberately skips this -- the
scenarios are timing-driven, not config-driven, and adding a TPRM
authoring step distracts from the actual TimeServer behaviour. A
production deployment WOULD load a TPRM that overrides the demo's
short `holdoverLimitS`.

## What the demo does NOT cover

- **Hardware-specific HAL paths** (LinuxPps `/dev/pps0` ioctl,
  Stm32Pps EXTI, etc.) -- the demo uses MockPps. These are exercised
  by the per-platform unit tests in `TestHalLinux`, `TestHalStm32`,
  etc.
- **Distributed timing modes** (SECONDARY/RELAY/PTP_SYNC/CAN_SYNC)
  -- these require multi-node infrastructure. The
  `TestSystemCoreTimeServer` tests `RelayModeAcceptsRemoteTnt`,
  `PtpSyncTickAnchorsAndPublishes`, etc. cover the per-mode
  behaviour. A multi-node demo is a follow-up.
- **Real-hardware GPS integration** -- exercised on actual
  flight-config hardware before deployment, not in this demo.

## File layout

```
demos/apex_time_demo/
├── CMakeLists.txt
├── README.md
├── app_data.toml          (Zenith target descriptor)
├── release.mk             (release manifest)
├── docs/
│   ├── DESIGN.md          (this file)
│   ├── HOW_TO_RUN.md
│   ├── RESULTS.md
│   └── DEPLOY_PROCEDURE.md
└── exec/
    ├── CMakeLists.txt
    ├── inc/TimeDemoExecutive.hpp
    └── src/
        ├── TimeDemoExecutive.cpp
        └── main.cpp
```
