# HIL Flight Demonstration - Design Document

## Overview

Closed-loop HIL (Hardware-in-the-Loop) flight simulation demonstrating Apex
cross-platform code reuse. POSIX host runs the plant model (3DOF dynamics +
gravity + drag) under a full **ApexExecutive** with TPRM-driven configuration.
STM32 runs the flight controller. Both communicate over UART/SLIP using the
same shared libraries.

## Architecture

### System Overview

```
  POSIX Host (Raspberry Pi 4 / Linux)                STM32 (NUCLEO-L476RG)
  +--------------------------------------------+     +--------------------------+
  |  HilExecutive                              |     |  LiteExecutive           |
  |                                            |     |  +--------------------+  |
  |  +------------------+                      |     |  | FlightController   |  |
  |  | HilPlantModel    |  VehicleState        |     |  |  Prediction model  |  |
  |  |   (SW_MODEL)     |-----+                |     |  |  State estimator   |  |
  |  | PointMassDynamics |    |                |     |  |  Guidance/control  |  |
  |  | DragModel         |    |                |     |  +--------------------+  |
  |  +------------------+    |                |     |  UART: USART1 (FTDI)    |
  |                          v                |     +--------------------------+
  |  +------------------+  +--------------+   |               ^
  |  | HilDriver #0     |->| UartAdapter  |---+-[SLIP/UART]---+
  |  |   (DRIVER)       |  | /dev/ttyUSB0 |   |
  |  +------------------+  +--------------+   |
  |                          |                |
  |  +------------------+  +--------------+   |     +--------------------------+
  |  | HilDriver #1     |->| UartAdapter  |---+---->| VirtualFlightCtrl        |
  |  |   (DRIVER)       |  | /dev/pts/X   |   |     |   (HW_MODEL)            |
  |  +------------------+  +--------------+   |     | FlightController (same)  |
  |                          |   PTY link      |     | PtyPair master end       |
  |                          +--------<--------+-----+--------------------------+
  |                                            |
  |  +------------------+                      |
  |  | HilComparator    |  diffs ControlCmd    |
  |  |   (SUPPORT)      |  from Driver #0 & #1 |
  |  +------------------+                      |
  |                                            |
  |  +------------------+                      |
  |  | SystemMonitor    |  health telemetry    |
  |  |   (SUPPORT)      |  (CPU, mem, thermal) |
  |  +------------------+                      |
  |                                            |
  |  Scheduler, FileSystem, Registry, Logs     |
  +--------------------------------------------+

  Shared code on both sides:
    - system_core_protocols_framing_slip (SLIP framing)
    - utilities_checksums_crc (CRC-16/XMODEM)
    - common/ headers (VehicleState, ControlCmd, HilProtocol)
    - FlightController algorithm (firmware/ and HW_MODEL use same class)
```

### Component Roles

| Component         | Type       | Role                                                                                                                                                         |
| ----------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| HilPlantModel     | `SW_MODEL` | Physics simulation (dynamics, gravity, drag). Publishes VehicleState.                                                                                        |
| HilDriver         | `DRIVER`   | Sends VehicleState, receives ControlCmd over serial link. Two instances: one for real STM32, one for emulated.                                               |
| VirtualFlightCtrl | `HW_MODEL` | Emulates the STM32 flight controller in software. Same FlightController algorithm. Sits behind a PTY so the driver talks to it identically to real hardware. |
| HilComparator     | `SUPPORT`  | Compares ControlCmd streams from both drivers. Logs divergence metrics.                                                                                      |
| SystemMonitor     | `SUPPORT`  | Platform health telemetry (CPU, memory, thermal). TPRM-configured for Pi 4.                                                                                  |

### Operating Modes

| Mode           | Components Active                     | Purpose                                               |
| -------------- | ------------------------------------- | ----------------------------------------------------- |
| **SIL**        | Plant + Driver #1 + VirtualFlightCtrl | Software-only validation. No hardware needed.         |
| **HIL**        | Plant + Driver #0                     | Real STM32 in the loop. Production-like.              |
| **Comparison** | All components + Comparator           | Both paths active. Validates STM32 matches emulation. |

### Platform Features

| Feature                           | Mechanism                                                        |
| --------------------------------- | ---------------------------------------------------------------- |
| Deterministic real-time execution | SCHED_FIFO, per-core thread pinning, 1 kHz clock                 |
| Cross-platform code reuse         | Same FlightController on POSIX and STM32                         |
| Hardware-in-the-loop              | UART/SLIP driver talks to real STM32 at 50 Hz                    |
| Software emulation                | HW_MODEL behind PTY, driver-transparent virtual link             |
| Real vs emulated comparison       | Comparator tracks divergence between hardware and software paths |
| C2 command and control            | APROTO/TCP with 11 addressable components, sub-10ms latency      |
| TPRM hot-reload                   | A/B bank swap, zero-downtime parameter changes                   |
| Library hot-swap                  | Lock, dlopen/dlclose, identity validation, auto-unlock           |
| Executive restart                 | Graceful shutdown + execve, CLI args preserved                   |
| Sleep/wake mode                   | Suspend task execution while clock and C2 stay active            |
| Action engine                     | Watchpoints, fault injection, timed actions, TPRM-driven         |
| Platform health telemetry         | CPU, memory, thermal via SystemMonitor at 1 Hz                   |
| Watchdog supervisor               | Separate process, heartbeat pipe, crash escalation               |
| Headless operation                | stdin isolation via `/dev/null`, UART RX flush after restart     |

## Directory Structure

```
apps/apex_hil_demo/
+-- CMakeLists.txt                 Routes firmware/ vs exec+model/ by platform
+-- release.mk                    Release manifest
+-- common/inc/                   Shared types: VehicleState, HilProtocol, HilConfig
+-- common/protocol_data.toml     C2 manifest: wire types + enums
+-- firmware/                     STM32 bare-metal flight controller
|   +-- CMakeLists.txt            apex_add_firmware()
|   +-- STM32L476RG.ld            Linker script
|   +-- inc/                      FlightController, FirmwareConfig, hal_conf
|   +-- src/                      main.cpp, FlightController.cpp, cxx_stubs.cpp
+-- model/                        Plant model + HW emulation (POSIX)
|   +-- CMakeLists.txt            apex_add_library(): apex_hil_demo_plant
|   +-- inc/                      PointMassDynamics, DragModel, HilPlantModel,
|   |                             HilPlantData, VirtualFlightCtrl, VirtualFlightCtrlData
|   +-- src/                      PointMassDynamics.cpp, DragModel.cpp
|   +-- plant_data.toml           C2 manifest: plant params + state
|   +-- vfc_data.toml             C2 manifest: VFC state
+-- driver/                       HilDriver (DriverBase, UART/SLIP)
|   +-- inc/                      HilDriver.hpp, HilDriverData.hpp
|   +-- driver_data.toml          C2 manifest: driver state
+-- support/                      HilComparator (SupportComponentBase)
|   +-- inc/                      HilComparator.hpp, HilComparatorData.hpp
|   +-- comparator_data.toml      C2 manifest: comparator params + state
+-- exec/                         POSIX executive
|   +-- CMakeLists.txt            apex_add_app(): ApexHilDemo
|   +-- inc/HilExecutive.hpp      ApexExecutive subclass
|   +-- src/                      main.cpp, HilExecutive.cpp
+-- watchdog/                     Standalone supervisor (ApexWatchdog)
+-- scripts/                      C2 demo and checkout scripts (Python)
+-- c2_data/                      Generated JSON struct dictionaries for C2
+-- test/plugin/                  TestPlugin_v1/v2 for hot-swap verification
+-- tprm/                         TPRM archives + source TOMLs
|   +-- master.tprm               100 Hz packed archive
|   +-- master_1khz.tprm          1 kHz variant
|   +-- safe_master.tprm          Degraded-mode (C2 only)
|   +-- toml/                     Source configs (executive, scheduler, drivers, etc.)
|   +-- ats/, rts/                Action sequences
+-- docs/                         HIL_DESIGN.md, HOW_TO_RUN.md, DEPLOY_PROCEDURE.md
```

## Virtual Transport Framework

When an HW_MODEL emulates real hardware, it needs a transport link that makes
it indistinguishable from the real device. The framework creates this link
automatically based on the HW_MODEL's declared transport kind. The driver
code is identical on both ends -- it does not know whether it is talking to
real hardware or an emulated model.

### How It Works

1. The HW_MODEL author declares a transport kind (e.g., `SERIAL_232`).
2. During executive startup, the framework inspects each registered HW_MODEL.
3. For each HW_MODEL, the framework creates the appropriate virtual link.
4. The HW_MODEL receives a device handle for its side of the link.
5. A matching DRIVER instance receives the other side as a device path or fd.
6. Both sides use their normal device interface (UartDevice, CanDevice, etc.).

The HW_MODEL author never creates a PtyPair, socketpair, or vcan interface.

### Supported Transports

| TransportKind | Virtual Link          | HW_MODEL Gets        | DRIVER Gets                     |
| ------------- | --------------------- | -------------------- | ------------------------------- |
| `SERIAL_232`  | `PtyPair`             | master fd            | `UartAdapter(slavePath)`        |
| `SERIAL_422`  | `PtyPair`             | master fd            | `UartAdapter(slavePath)`        |
| `SERIAL_485`  | `PtyPair`             | master fd            | `UartAdapter(slavePath, rs485)` |
| `CAN`         | vcan kernel interface | `CANBusAdapter`      | `CANBusAdapter`                 |
| `SPI`         | Unix socketpair       | `SpiSocketDevice`    | `SpiSocketDevice`               |
| `I2C`         | Unix socketpair       | `I2cSocketDevice`    | `I2cSocketDevice`               |
| `ETH_TCP`     | loopback TCP          | `TcpSocketServer`    | `TcpSocketClient`               |
| `ETH_UDP`     | loopback UDP          | `UdpSocketServer`    | `UdpSocketClient`               |
| `UNIX_STREAM` | `socketpair()`        | `UnixSocketServer`   | `UnixSocketClient`              |
| `BLUETOOTH`   | Unix socketpair       | `RfcommSocketDevice` | `RfcommSocketDevice`            |

### PTY Architecture (Serial Transports)

The HIL demo uses `SERIAL_232`. The `PtyPair` class creates a master/slave
pseudo-terminal pair. The slave path (e.g., `/dev/pts/7`) is opened by a
standard `UartAdapter` which configures it via termios exactly like real
hardware. The master fd goes to the HW_MODEL for raw byte I/O.

```
  HW_MODEL                                   DRIVER
  +------------------+                       +------------------+
  | VirtualFlightCtrl|                       | HilDriver        |
  |   read(masterFd) |<--- PTY kernel --->   | UartAdapter      |
  |   write(masterFd)|     /dev/pts/X        | .read() .write() |
  +------------------+                       +------------------+
```

### HW_MODEL Author Interface

The HW_MODEL author declares a transport kind and receives a device handle
from the framework. The base class provides `transportRead()` /
`transportWrite()` which delegate to the framework-provided fd. The author
never sees PtyPair, socketpair, or UartAdapter.

### DRIVER Interface

The DRIVER is the same class regardless of whether it talks to real hardware
or an emulated HW_MODEL. TPRM provides the device path: `/dev/ttyUSB0` for
real hardware, or the PTY slave path for emulation. The driver opens it via
`UartAdapter` and does standard I/O.

### Registration and Wiring

When the executive calls `registerModel(&virtualCtrl_)`, the framework sees
`componentType() == HW_MODEL` and calls `provisionTransport()`. For
`SERIAL_232`, this creates a PTY pair via `TransportLink`. The model gets the
master fd automatically; the executive wires the slave path to the emulated
driver via `peerDevicePath()`.

## Component Identity

| Component         | Type      | ID  | Instance | fullUid  |
| ----------------- | --------- | --- | -------- | -------- |
| Executive         | EXECUTIVE | 0   | 0        | 0x000000 |
| Scheduler         | CORE      | 1   | 0        | 0x000100 |
| FileSystem        | CORE      | 2   | 0        | 0x000200 |
| Registry          | CORE      | 3   | 0        | 0x000300 |
| Interface         | CORE      | 4   | 0        | 0x000400 |
| Action            | CORE      | 5   | 0        | 0x000500 |
| HilPlantModel     | SW_MODEL  | 120 | 0        | 0x007800 |
| VirtualFlightCtrl | HW_MODEL  | 121 | 0        | 0x007900 |
| HilDriver         | DRIVER    | 122 | 0        | 0x007A00 |
| HilDriver         | DRIVER    | 122 | 1        | 0x007A01 |
| HilComparator     | SUPPORT   | 123 | 0        | 0x007B00 |
| SystemMonitor     | SUPPORT   | 200 | 0        | 0x00C800 |

## Data Type Integration

All HIL components use the Apex `ModelData<T, DataCategory>` type system and
register their data blocks with the executive registry via `registerData()`.
This makes all component data queryable at runtime for C2 inspection, fault
injection, and telemetry export.

### Type Aliases

| Alias             | Meaning            | Access Pattern             |
| ----------------- | ------------------ | -------------------------- |
| `StaticParam<T>`  | Constants          | Read-only after init       |
| `TunableParam<T>` | Runtime-adjustable | External write, model read |
| `State<T>`        | Internal state     | Model read/write           |
| `Input<T>`        | External data in   | External write, model read |
| `Output<T>`       | External data out  | Model write, external read |

### Registry Data Map

| Component         | fullUid  | Category      | Struct                  | Size |
| ----------------- | -------- | ------------- | ----------------------- | ---- |
| HilPlantModel     | 0x007800 | TUNABLE_PARAM | HilPlantTunableParams   | 64 B |
| HilPlantModel     | 0x007800 | STATE         | HilPlantState           | 40 B |
| HilPlantModel     | 0x007800 | OUTPUT        | VehicleState            | 48 B |
| VirtualFlightCtrl | 0x007900 | STATE         | VfcState                | 16 B |
| HilDriver #0      | 0x007A00 | STATE         | DriverState             | 24 B |
| HilDriver #0      | 0x007A00 | OUTPUT        | ControlCmd              | 16 B |
| HilDriver #1      | 0x007A01 | STATE         | DriverState             | 24 B |
| HilDriver #1      | 0x007A01 | OUTPUT        | ControlCmd              | 16 B |
| HilComparator     | 0x007B00 | TUNABLE_PARAM | ComparatorTunableParams | 8 B  |
| HilComparator     | 0x007B00 | STATE         | ComparatorState         | 16 B |

Total: 10 data blocks, 256 bytes of inspectable runtime data.

## HilPlantModel

SwModelBase subclass wrapping PointMassDynamics + DragModel into a schedulable
model with three tasks:

| Task      | UID | Rate   | Priority      | Description                         |
| --------- | --- | ------ | ------------- | ----------------------------------- |
| plantStep | 1   | 100 Hz | 127 (highest) | Gravity + drag + thrust integration |
| control   | 2   | 50 Hz  | 63 (high)     | PD altitude-hold controller (SIL)   |
| telemetry | 3   | 1 Hz   | -128 (lowest) | Status logging                      |

### Tunable Parameters (TPRM, 64 bytes)

| Parameter | Type   | Default | Description                       |
| --------- | ------ | ------- | --------------------------------- |
| mass      | double | 10.0    | Vehicle mass [kg]                 |
| dragCd    | double | 0.5     | Drag coefficient                  |
| dragArea  | double | 0.1     | Reference area [m^2]              |
| targetAlt | double | 100.0   | PD controller target altitude [m] |
| ctrlKp    | double | 2.0     | Proportional gain [N/m]           |
| ctrlKd    | double | 1.5     | Derivative gain [N/(m/s)]         |
| thrustMax | double | 200.0   | Maximum thrust magnitude [N]      |

### PD Controller

The SIL-mode controller computes thrust to hold `targetAlt`:

```
thrustZ = Kp * (targetAlt - altitude) - Kd * verticalVelocity + weight
```

Thrust is clamped to `[0, thrustMax]` and applied in the NED frame
(negative z = upward thrust).

In the full system, this embedded PD controller is replaced by the
HilDriver/VirtualFlightCtrl or HilDriver/real-STM32 path. The plant model
publishes VehicleState and the drivers deliver ControlCmd back. The plant
does not know or care which controller produced the thrust.

## Threading Model (Raspberry Pi 4)

ApexExecutive manages 6 threads, each configured via TPRM with scheduling
policy, priority, and CPU affinity. The configuration is optimized for the
Raspberry Pi 4's 4x Cortex-A72 cores.

### Thread-to-Core Mapping

```
  Core 0             Core 1             Core 2             Core 3
  +-----------+      +-----------+      +-----------+      +-----------+
  | CLOCK     |      | TASK_EXEC |      | EXT_IO    |      | STARTUP   |
  | FIFO @ 90 |      | FIFO @ 80 |      | OTHER     |      | SHUTDOWN  |
  | (tick gen) |      | (dispatch)|      | (serial)  |      | WATCHDOG  |
  +-----------+      +-----------+      +-----------+      | OTHER     |
                                                           +-----------+
```

**Core 0 -- CLOCK (SCHED_FIFO, priority 90):** Generates the system tick.
Highest-priority thread on a dedicated core ensures the tick is never delayed
by task execution, I/O, or OS housekeeping. Priority 90 means only kernel
threads (priority 99) can preempt it.

**Core 1 -- TASK_EXECUTION (SCHED_FIFO, priority 80):** Runs scheduled tasks.
Priority lower than clock so the clock can signal new ticks without waiting
for task completion. This separation is essential for HARD_PERIOD_COMPLETE mode.

**Core 2 -- EXT_IO (SCHED_OTHER, priority 0):** Handles stdin commands and
UART serial communication. Uses CFS scheduler because I/O is inherently
blocking. Isolating I/O on its own core prevents serial port reads/writes
from interfering with RT threads.

**Core 3 -- STARTUP, SHUTDOWN, WATCHDOG (SCHED_OTHER, priority 0):** Three
non-RT threads share one core. None are performance-sensitive.

### Why Dedicated Cores

The Pi 4 has 4 cores: 2 for RT-critical threads, 2 for non-critical functions.
Pinning one function per core eliminates context-switch jitter and prevents
cache thrashing from thread migration. Result: **0 frame overruns** at both
100 Hz and 1 kHz.

### RT Mode: HARD_PERIOD_COMPLETE

- The clock thread defines the period (10 ms at 100 Hz, 1 ms at 1 kHz).
- The executor must complete all tasks within that period.
- Frame overruns increment a lag counter.
- `rtMaxLagTicks = 5` allows up to 5 frames of accumulated lag.

Requires `sudo` or `CAP_SYS_NICE` for SCHED_FIFO. Without elevated
privileges, the executive falls back to SCHED_OTHER with a warning.

## TPRM Configuration

All runtime configuration is packed into `master.tprm`:

| fullUid  | File                 | Contents                                                                     |
| -------- | -------------------- | ---------------------------------------------------------------------------- |
| 0x000000 | executive.tprm       | Clock, RT mode, thread config (6 threads)                                    |
| 0x000100 | scheduler.tprm       | Task schedule (10 tasks)                                                     |
| 0x000400 | interface.tprm       | C2 interface (host, port, framing, queue sizes)                              |
| 0x000500 | action.tprm          | Action engine (watchpoints, groups, sequences, notifications, timed actions) |
| 0x007800 | plant_model.tprm     | Plant tunable parameters (64 B)                                              |
| 0x007A00 | driver_real.tprm     | HilDriver #0 (devicePath=/dev/ttyUSB0, 128 B)                                |
| 0x007A01 | driver_emulated.tprm | HilDriver #1 (empty path, PTY auto, 128 B)                                   |
| 0x007B00 | comparator.tprm      | HilComparator (warnThreshold=0.1N, 8 B)                                      |
| 0x00C800 | system_monitor.tprm  | System monitor (Pi 4: 4 cores, no GPU)                                       |

## UART Protocol

Wire format: `[SLIP_END] [opcode:1] [payload:N] [CRC-16:2] [SLIP_END]`

| Direction   | Opcode            | Payload             | Rate  |
| ----------- | ----------------- | ------------------- | ----- |
| Host->STM32 | CMD_START 0x01    | none                | once  |
| Host->STM32 | CMD_STOP 0x02     | none                | once  |
| Host->STM32 | CMD_RESET 0x03    | none                | once  |
| Host->STM32 | STATE_UPDATE 0x10 | VehicleState (48B)  | 50 Hz |
| STM32->Host | CONTROL_CMD 0x20  | ControlCmd (16B)    | 50 Hz |
| STM32->Host | HEARTBEAT 0x30    | HeartbeatData (12B) | 1 Hz  |

## STM32 Task Schedule (LiteExecutive @ 100 Hz)

| Task          | Rate   | Priority | Description                             |
| ------------- | ------ | -------- | --------------------------------------- |
| profilerStart | 100 Hz | 127      | DWT cycle counter start                 |
| controlTask   | 50 Hz  | 0        | Receive state, run controller, send cmd |
| heartbeatTask | 1 Hz   | 0        | Send heartbeat over UART                |
| ledBlinkTask  | 2 Hz   | 0        | PA5 toggle                              |
| profilerEnd   | 100 Hz | -128     | DWT cycle counter end                   |

## Hardware

### Raspberry Pi 4 (POSIX Host)

- SoC: BCM2711, 4x Cortex-A72 @ 1.8 GHz
- Kernel: 6.12.62+rpt-rpi-v8 (PREEMPT, not PREEMPT_RT)
- IP: raspberrypi.local (SSH: kalex@)
- UART: /dev/ttyUSB0 (via FTDI FT232RL adapter)

### NUCLEO-L476RG (Flight Controller)

- MCU: STM32L476RG, Cortex-M4 @ 80 MHz
- Flash: 1 MB, RAM: 128 KB
- UART: USART1 PA9/PA10 (via FTDI adapter to Pi)
- LED: PA5 (heartbeat)

### FTDI Adapter

- DSD TECH SH-U09C5 (FTDI FT232RL) set to 3.3V
- Connected between Nucleo USART1 and Pi USB

### Wiring

NUCLEO-L476RG connects to Raspberry Pi via FTDI adapter (same pinout as
the STM32 encryptor demo):

| FTDI Pin | Nucleo Pin | Label | Header    |
| -------- | ---------- | ----- | --------- |
| TXD      | PA10 (RX)  | D2    | CN9 pin 3 |
| RXD      | PA9 (TX)   | D8    | CN5 pin 1 |
| GND      | GND        | GND   | CN6 pin 6 |

The Nucleo USB-C (ST-Link) also connects to the Pi for programming only.

## Runtime Update Capabilities

The HIL demo supports runtime updates over APROTO/TCP without stopping the
executive. Three tiers of update are available, from lightweight parameter
changes to full process replacement.

### Update Tiers

| Tier | Operation         | Impact                | Downtime                            |
| ---- | ----------------- | --------------------- | ----------------------------------- |
| 1    | TPRM Reload       | Parameter-only change | None                                |
| 2    | Library Reload    | Component code swap   | Target component paused during swap |
| 3    | Executive Restart | Full process restart  | Brief (execve, ~5-8 s reconnect)    |

### A/B Bank Architecture

All updatable assets (TPRMs, component .so files, executive binaries) use A/B
bank switching for safe, rollback-capable updates:

```
~/apex/                    <-- Deployment dir = filesystem root
  run.sh                   Launch script
  bank_a/                  <-- Active bank (populated by release package)
    bin/                   Executive binary
    libs/                  Component .so files
    tprm/                  TPRM binary files
  bank_b/                  <-- Inactive bank (upload target)
    bin/
    libs/
    tprm/
  active_bank              Marker file: "a" or "b"
  logs/, tlm/, db/, ...    Non-banked runtime directories
  stage/                   File transfer chunk assembly
```

- Active bank always reflects what is currently running
- Inactive bank is the upload target for C2 file transfers
- On successful reload, files swap between banks
- Rollback = reload again (previous version is in the inactive bank)
- `active_bank` marker persists across executive restarts

### File Transfer

Sequential chunked transfer with CRC32-C integrity verification. Chunk size
is 4096 bytes. Destination paths target the inactive bank.

```
C2 Client                          ApexHilDemo
    |--- FILE_BEGIN (path, size, crc) -->|  Creates staging file
    |<-- ACK                             |
    |--- FILE_CHUNK (idx=N, data) ----->|  Writes to staging
    |<-- ACK                             |
    |--- FILE_END ---------------------->|  Verifies CRC, atomic rename
    |<-- ACK + FileEndResponse           |
```

### TPRM Hot-Reload

Upload a new TPRM to the inactive bank, then issue RELOAD_TPRM with the
target component's fullUid. The component calls `loadTprm()` from the
inactive bank, and the file swaps between banks for rollback capability.

The scheduler uses a backup/restore pattern: if the new TPRM produces zero
resolved tasks, the backup is restored and the previous schedule continues.

### Component Hot-Swap

True on-target component replacement via dlopen/dlclose. The old component .so
is unloaded and a new one loaded in its place, with scheduler task pointers
re-wired and registry entries updated. No process restart required.

Workflow: lock component -> upload .so to inactive bank -> RELOAD_LIBRARY ->
framework validates componentId, transfers identity, re-wires tasks, swaps
bank files -> component running new code.

Safety guarantees:

- Component must be locked before RELOAD_LIBRARY
- componentId validated (new .so must match old)
- Identity preserved (instanceIndex transferred)
- Auto-unlock on both success and failure
- Bank file swap enables single-command rollback

### Executive Restart

Graceful restart via execve with CLI argument preservation. If a new binary
exists in the inactive bank, the binary swaps between banks before exec. If
no binary is found, the executive re-execs itself (restart without upgrade).
On execv failure, the bank swap is rolled back and the process continues.

## System Safety

### Safety Architecture

Three layers of defense protect against runtime failures:

```
Layer 3: Watchdog Supervisor (ApexWatchdog)
  - Separate process, no shared libraries with executive
  - Monitors heartbeat pipe, restarts on crash/hang
  - Enters degraded mode after threshold, stops after max crashes

Layer 2: Executive Safety (ApexExecutive)
  - Scheduler rollback on failed TPRM reload
  - Library reload validation (task pointer counts verified)
  - Bank swap rollback on execv failure
  - Sleep mode (clock/IO active, tasks suspended)

Layer 1: Component Isolation
  - Components locked before hot-swap (tasks paused)
  - componentId validated on reload
  - Identity preserved across swap
  - Auto-unlock on any failure
```

### Watchdog Supervisor

`ApexWatchdog` is a standalone process that monitors the executive via a
heartbeat pipe. It links only libc, so an executive crash cannot take down
the watchdog.

```
ApexWatchdog                              ApexHilDemo
  |                                            |
  +--[fork/exec]------------------------------>|
  |<--[heartbeat pipe: '.' every 1s]-----------|
  |                                            |
  | (no heartbeat for N seconds?)              |
  +--[SIGKILL]-------------------------------->|  X
  | (crashes < safe_threshold?)                |
  +--[restart with normal config]------------->|
  | (crashes >= safe_threshold?)               |
  +--[restart with safe config]--------------->|  (degraded: C2 only)
  | (crashes >= max_crashes?)                  |
  +--[FULL STOP, wait for operator]            |
```

| Option                  | Default | Description                                  |
| ----------------------- | ------- | -------------------------------------------- |
| `--max-crashes N`       | 5       | Stop restarting after N crashes              |
| `--safe-config PATH`    | (none)  | TPRM for degraded-mode restarts              |
| `--safe-threshold N`    | 2       | Switch to safe config after N crashes        |
| `--heartbeat-timeout S` | 10      | Kill child after S seconds without heartbeat |

### Degraded Mode

When crashes exceed `--safe-threshold`, the watchdog restarts with the safe
TPRM. Only core infrastructure and SystemMonitor are scheduled. Application
components (plant, drivers, comparator) are registered but not scheduled.
Ground can read health telemetry, upload fixes, and restart with the full
config once the root cause is resolved.

### Sleep Mode

SLEEP (opcode 0x0116) suspends scheduled task execution while the clock keeps
ticking for time awareness, the watchdog keeps monitoring, and C2 stays
responsive. WAKE resumes normal operation.

SLEEP vs PAUSE: PAUSE stops the clock entirely. SLEEP keeps the clock running
but skips task dispatch.

### Headless Operation

When running without a terminal, redirect stdin from `/dev/null`:

```bash
sudo ./run.sh ApexHilDemo --skip-cleanup </dev/null &
```

The external I/O thread interprets stdin characters (`p` = PAUSE, `q` = QUIT).
SSH disconnection can leave stale bytes that trigger unintended commands.
Redirecting from `/dev/null` disables stdin polling.

### UART RX Flush

HilDriver flushes the UART receive buffer during `doInit()` to discard stale
bytes from a previous process (e.g., after execve). Without the flush, partial
SLIP frames from the prior session cause CRC errors on the first receive cycles.

### Runtime Update Safety

| Operation         | Failure Mode               | Safety Measure                              |
| ----------------- | -------------------------- | ------------------------------------------- |
| TPRM Reload       | 0 tasks resolved           | Backup/restore: previous schedule preserved |
| TPRM Reload       | Bank swap fails            | Warning logged, reload succeeds in-memory   |
| Library Reload    | Task count mismatch        | Old component restored, auto-unlocked       |
| Library Reload    | Registry update fails      | Old task pointers restored, auto-unlocked   |
| Executive Restart | execv fails                | Bank swap rolled back, process continues    |
| Executive Restart | No binary in inactive bank | NAK returned, no action taken               |
