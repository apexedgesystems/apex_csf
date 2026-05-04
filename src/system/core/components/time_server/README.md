# Time Server Component

**Namespace:** `system_core::time_server`
**Platform:** POSIX hosted (Linux, Jetson, RPi, RISC-V SoCs); Cortex-M MCU via header-only impls
**C++ Standard:** C++23

Sole authority for wall-clock time in an Apex application. Ingests an
external 1PPS edge through the `IPps` HAL, optionally pairs it with a UTC
reference (from GPS, ground command, manual entry, or onboard
oscillator), correlates the local steady clock to UTC, and broadcasts
the `TimeAtNextTone` (TNT) message every PPS edge. Components compute
current UTC by interpolating against the most recent TNT.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Key Features](#3-key-features)
4. [API Reference](#4-api-reference)
5. [Performance](#5-performance)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Reference

| Module                    | Purpose                                                 | RT-Safe |
| ------------------------- | ------------------------------------------------------- | ------- |
| `TimeServer`              | Core component owning correlation, drift, state machine | Yes     |
| `TimeAtNextTone`          | 40-byte broadcast message (1 Hz on PPS edge)            | N/A     |
| `SetReferenceTime`        | GPS / ground / sim -> TimeServer command (16 bytes)     | N/A     |
| `SetTimeManual`           | No-PPS UTC fallback command (8 bytes)                   | N/A     |
| `TimeServerTunableParams` | TPRM configuration block (16 bytes)                     | N/A     |
| `TimeServerOutput`        | Registry OUTPUT block (56 bytes)                        | N/A     |
| `IPps` (HAL)              | Abstract PPS edge capture interface                     | Yes     |
| `LinuxPps`                | `/dev/pps[N]` PPS_FETCH ioctl                           | Yes     |
| `Stm32Pps`                | EXTI + DWT cycle counter                                | Yes     |
| `PicoPps`                 | RP2040 GPIO IRQ + `time_us_64()`                        | Yes     |
| `Esp32Pps`                | ESP32-S3 GPIO ISR + `esp_timer_get_time()`              | Yes     |
| `AvrPps`                  | ATmega328P Timer1 ICP1 input capture                    | Yes     |
| `C2000Pps`                | TI F28004x eCAP module                                  | Yes     |
| `MockPps`                 | Software-injected edges for tests                       | Yes     |

### Question-to-Answer

| Question                                                | Answer                                                                                 |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| How do I get current UTC?                               | Read `TimeAtNextTone.epochNs` and interpolate (see [API section 4](#4-api-reference)). |
| How do I subscribe via the registry instead of the bus? | Look up `(componentId=6, OUTPUT, "output")` for the OUTPUT block.                      |
| How accurate is the correlation?                        | Sub-microsecond on a hosted Linux box with kernel PPS support.                         |
| What if the GPS receiver loses fix?                     | Quality drops to `COARSE`; UTC continues advancing on local clock.                     |
| What if PPS stops entirely?                             | `STALE`, then `FREERUN` re-anchored from `CLOCK_REALTIME`.                             |
| Can ATS sequences trigger on UTC?                       | Yes; the executive wires `actionComp.iface().timeProvider` automatically.              |
| How do I supply the reference time from a GPS driver?   | Send `SET_REFERENCE_TIME` (opcode `0x0601`) with a `SetReferenceTime` payload.         |
| Which mode for a follower node sharing master PPS?      | `SECONDARY` (own PPS + remote TNT as reference).                                       |
| Which mode for a node with no PPS at all?               | `RELAY` (latch on remote TNT receipt) or `PTP_SYNC` (NTP/PTP-disciplined wall clock).  |

---

## 2. Design Principles

| Principle                 | Implementation                                                                          |
| ------------------------- | --------------------------------------------------------------------------------------- |
| Sole authority            | One `TimeServer` per node; consumers read TNT or OUTPUT, never NMEA.                    |
| Steady-clock domain       | All correlation math in monotonic ns; UTC enters only via reference cmds.               |
| No allocation             | Drift filter is a fixed `int32_t[64]` ring buffer; all paths noexcept.                  |
| Graceful degradation      | TNT keeps broadcasting with explicit `valid` and `quality` indicators.                  |
| Delegate-wired clocks     | `SteadyClock`, `WallClock`, broadcast bus all injected via Delegate<>.                  |
| Mode dispatched in `tick` | `PRIMARY` / `SECONDARY` poll IPps; `RELAY` / `PTP_SYNC` / `CAN_SYNC` use other sources. |

---

## 3. Key Features

### 3.1 State machine

| `valid`   | `quality` | Meaning                                                            |
| --------- | --------- | ------------------------------------------------------------------ |
| `NONE`    | `UNKNOWN` | Boot. No edges, no reference.                                      |
| `VALID`   | `COARSE`  | PPS ticking, no fresh reference (HOLDOVER).                        |
| `VALID`   | `FINE`    | PPS + reference paired; drift not yet stable.                      |
| `VALID`   | `PRECISE` | PPS + reference + drift filter has filled.                         |
| `STALE`   | (kept)    | No edge for `> maxStalenessUs`.                                    |
| `FREERUN` | `COARSE`  | No edge for `> holdoverLimitS`; re-anchored from `CLOCK_REALTIME`. |

Transitions are observable through the broadcast TNT; components decide
their own trust policy.

### 3.2 Correlation math

```cpp
const TimeAtNextTone& tnt = /* most recent broadcast */;
struct timespec now{};
clock_gettime(CLOCK_MONOTONIC, &now);
const int64_t steadyNowNs = now.tv_sec * 1'000'000'000LL + now.tv_nsec;
const int64_t utcNs = tnt.epochNs + (steadyNowNs - tnt.localNs);
```

Exact at the moment of the most recent edge. Drift accumulates between
edges; `tnt.driftPpb` lets consumers apply correction for sub-millisecond
needs.

### 3.3 Drift estimation

Moving average of `(intervalNs - 1e9)` samples. `driftFilterTaps` (TPRM)
sets the window size, capped at `MAX_DRIFT_TAPS = 64`. Quality auto-promotes
from `FINE` to `PRECISE` once the window fills.

### 3.4 Glitch rejection

Edges with interval outside `[500 ms, 1500 ms]` are counted as glitches
(`glitchCount`) and not used to update the correlation. Doubled edges
from electrical noise and slipped polls are caught here.

### 3.5 Operating modes (TPRM `mode` field)

| Mode        | Sync source                   | Reference source                    | Accuracy class            |
| ----------- | ----------------------------- | ----------------------------------- | ------------------------- |
| `PRIMARY`   | Local PPS via IPps            | GPS / ground / onboard              | < 1 us                    |
| `SECONDARY` | Local PPS via IPps            | Remote TNT relayed in               | < 1 us                    |
| `RELAY`     | (none)                        | Remote TNT (`OP_ACCEPT_REMOTE_TNT`) | 0.1 - 5 ms (link latency) |
| `PTP_SYNC`  | Wall clock (`CLOCK_REALTIME`) | PTP / NTP daemon discipline         | 50 ns - 50 us             |
| `CAN_SYNC`  | CAN HW timestamp delegate     | CAN sync frame                      | 1 - 10 us                 |

Mode is a TPRM setting; components on every node see the same TNT struct.

---

## 4. API Reference

### 4.1 Lifecycle

```cpp
TimeServer ts;
ts.setSteadyClock(TimeServer::defaultSteadyClock());     // CLOCK_MONOTONIC
ts.setWallClock(TimeServer::defaultWallClock());         // CLOCK_REALTIME, FREERUN anchor
ts.setPpsSource(&pps);                                   // application-supplied IPps
ts.setBroadcastDelegate({broadcastFn, ctx});             // optional bus hook
ts.loadTprm(tprm);                                        // configuration
const std::uint8_t INIT = ts.init();                     // registers OUTPUT and TUNABLE_PARAM
```

`ApexExecutive` wires steady clock, wall clock, internal-bus broadcast,
and `actionComp.iface().timeProvider` automatically. Applications only
need to provide the platform-specific `IPps` source.

### 4.2 Per-frame entry

```cpp
ts.tick(currentSchedulerCycle);   // executive calls every frame
```

Polls `IPps`, processes edges, advances the state machine, broadcasts
TNT. Mode-gated: `PRIMARY` / `SECONDARY` poll IPps; `PTP_SYNC` reads the
wall clock; `CAN_SYNC` polls the CAN delegate; `RELAY` does no per-frame
work.

### 4.3 Bus opcodes

| Opcode   | Constant                | Direction          | Payload            | Response           |
| -------- | ----------------------- | ------------------ | ------------------ | ------------------ |
| `0x0601` | `OP_SET_REFERENCE_TIME` | inbound            | `SetReferenceTime` | none               |
| `0x0602` | `OP_TIME_AT_NEXT_TONE`  | outbound broadcast | `TimeAtNextTone`   | n/a                |
| `0x0603` | `OP_GET_TIME_STATUS`    | inbound            | none               | `TimeServerOutput` |
| `0x0604` | `OP_SET_TIME_MANUAL`    | inbound            | `SetTimeManual`    | none               |
| `0x0605` | `OP_RESET_CORRELATION`  | inbound            | none               | none               |
| `0x0606` | `OP_ACCEPT_REMOTE_TNT`  | inbound (RELAY)    | `TimeAtNextTone`   | none               |

`handleCommand(opcode, payload, response)` dispatches to the matching
handler; unknown opcodes delegate to `SystemComponentBase::handleCommand`.

### 4.4 Direct accessors

```cpp
[[nodiscard]] const TimeServerOutput&    output()        const noexcept;  // RT-safe
[[nodiscard]] const TimeAtNextTone&      currentTnt()    const noexcept;  // RT-safe
[[nodiscard]] std::int64_t               currentUtcNs()  const noexcept;  // RT-safe; uses configured steady clock
[[nodiscard]] std::int64_t               computeUtcNs(std::int64_t steadyNowNs) const noexcept; // RT-safe
[[nodiscard]] apex::time::TimeProviderDelegate utcTimeProvider() noexcept; // RT-safe
```

### 4.5 OUTPUT block fields

```cpp
struct TimeServerOutput {
  std::int64_t  utcEpochNs;          // UTC at most recent confirmed edge
  std::uint64_t metCycles;           // mission elapsed time (scheduler cycles)
  std::int64_t  lastPpsLocalNs;      // local steady_clock at that edge
  std::int64_t  correlationOffsetNs; // utcEpochNs - lastPpsLocalNs
  std::int64_t  nextToneEpochNs;     // predicted UTC at next edge (drift-adjusted)
  std::int32_t  driftEstimatePpb;    // local oscillator drift, parts per billion
  std::uint32_t ppsCount;            // total valid edges since boot
  std::uint8_t  correlationValid;    // TimeValid enum
  std::uint8_t  timeSource;          // TimeSource enum (active reference)
  std::uint8_t  timeQuality;         // TimeQuality enum
  std::uint8_t  flags;               // TNT_FLAG_* bitfield
};
```

---

## 5. Performance

Measured on x86_64 (atom + core hybrid), `--repeats 15`, 8 ptests in
`TimeServer_PTEST`. CSV at
`docs/optimization/0503/time_server/final/results.csv`.

| Test                     | Path                                        | Median |  Calls/s |   CV% |
| ------------------------ | ------------------------------------------- | -----: | -------: | ----: |
| `ComputeUtcNs`           | Pure interpolation arithmetic               |   8 ns | ~127 M/s | 11.5% |
| `HandleSetReferenceTime` | Store pending reference                     |   8 ns | ~128 M/s |  8.8% |
| `UtcTimeProvider`        | ATS time-provider delegate (per cycle)      |  15 ns |  ~68 M/s |  4.7% |
| `TickNoSource`           | `tick()` with no PPS wired                  |  16 ns |  ~64 M/s | 12.4% |
| `TickNoEdge`             | `tick()` polling MockPps, no edge           |  19 ns |  ~53 M/s | 11.3% |
| `HandleAcceptRemoteTnt`  | RELAY mode anchor                           |  22 ns |  ~46 M/s |  1.7% |
| `TickGlitch`             | `tick()` rejecting glitched edges           |  29 ns |  ~35 M/s |  5.7% |
| `TickWithEdge`           | `tick()` full correlation + drift + publish |  43 ns |  ~23 M/s | 13.5% |

Microarchitecture (perf, `TickWithEdge`): 3.06-3.32 instructions per
cycle, 0.10% branch miss rate, ~0.06% cache miss rate. No microarchitectural
slack to exploit; baseline analysis in
`docs/optimization/0503/time_server/baseline_analysis.md` documents the
diminishing-returns recommendation.

**Memory footprint:** `TimeServer` instance ~600 bytes (256 B drift
ring buffer + 56 B OUTPUT + 40 B TNT + delegates + correlation state).
Per-broadcast: 40 B TNT message.

---

## 6. Requirements

### 6.1 Build dependencies

- `system_core_system_component` (CoreComponentBase)
- `utilities_concurrency` (Delegate)
- `utilities_time` (TimeStandard, TimeProviderDelegate)
- `hal_interface` (IPps)

### 6.2 TPRM defaults

| Parameter          | Default   | Purpose                                        |
| ------------------ | --------- | ---------------------------------------------- |
| `mode`             | `PRIMARY` | Single-node deployment with local GPS PPS.     |
| `ppsDeviceIndex`   | 0         | `/dev/pps0`.                                   |
| `primaryRefSource` | `GPS`     | Most common deployment.                        |
| `maxStalenessUs`   | 1.5 s     | Tolerates one missed PPS edge before STALE.    |
| `driftFilterTaps`  | 16        | Convergence after ~16 s.                       |
| `holdoverLimitS`   | 60 s      | Operator policy: 1 min before forcing FREERUN. |

Each is overridable via TPRM at deployment without recompiling.

### 6.3 Out of scope (handled elsewhere or by separate components)

- **`GpsDriver`** parses NMEA and posts `SET_REFERENCE_TIME`; TimeServer
  does not parse NMEA itself.
- **Leap-second insertion.** TNT reserves
  `TNT_FLAG_LEAP_SECOND_PENDING` so consumers can warn; the actual
  schedule comes from the ground command path.
- **Hardware PHC `/dev/ptp0` direct read** for PTP_SYNC. Current
  implementation uses the lightweight CLOCK_REALTIME interpretation
  and relies on `ptp4l` (or chrony+PTP) discipline.
- **`ICan` hardware-timestamp extension** for CAN_SYNC activation.
  TimeServer-side support is in via `setCanSyncSource`; a separate
  HAL extension is needed to surface CAN frame timestamps.
- **Configurable PPS frequency** (5 Hz, 10 Hz). Glitch bounds are
  `constexpr` 500 ms / 1500 ms; TPRM tunability is a planned
  extension point.

---

## 7. Testing

```bash
# Build
make compose-debug

# Run all TimeServer + HAL + integration tests
docker compose run --rm -T dev-cuda \
  ctest --test-dir build/hosted-x86_64-debug \
  -L 'time_server|hal|action|core'

# Run only TimeServer unit + integration tests
docker compose run --rm -T dev-cuda \
  ctest --test-dir build/hosted-x86_64-debug -R 'TimeServer'

# Run performance tests
docker compose run --rm -T dev-cuda \
  ./build/hosted-x86_64-debug/bin/ptests/TimeServer_PTEST --csv results.csv
```

### Test organization

| Module                       | Test file                         |    Tests |
| ---------------------------- | --------------------------------- | -------: |
| `TimeServerData` (POD types) | `TimeServerData_uTest.cpp`        |       19 |
| `TimeServer` logic           | `TimeServer_uTest.cpp`            |       44 |
| Executive integration        | `TimeServerIntegration_uTest.cpp` |        7 |
| Performance                  | `TimeServer_pTest.cpp`            | 8 (perf) |

### Expected output

```
100% tests passed, 0 tests failed out of 70
```

HAL coverage: `TestHalInterface` (8), `TestHalMock` (18),
`TestHalLinux` (15), `TestHalStm32` (10), `TestHalPico` (7),
`TestHalEsp32` (7), `TestHalAvr` (8), `TestHalC2000` (7).

---

## 8. See Also

- [`CUSTOMER_INTEGRATION.md`](CUSTOMER_INTEGRATION.md) -- mission-profile setup guide.
- [`apps/apex_time_demo/README.md`](../../../../apps/apex_time_demo/README.md) -- standalone demo with GPS-simulator thread.
- [`src/system/core/hal/base/IPps.hpp`](../../hal/base/IPps.hpp) -- HAL contract.
- [`src/system/core/hal/linux/inc/LinuxPps.hpp`](../../hal/linux/inc/LinuxPps.hpp) -- `/dev/pps[N]` impl.
- [`src/system/core/hal/stm32/inc/Stm32Pps.hpp`](../../hal/stm32/inc/Stm32Pps.hpp) -- STM32 EXTI/DWT impl.
- [`src/system/core/hal/mock/inc/MockPps.hpp`](../../hal/mock/inc/MockPps.hpp) -- test substrate.
- [`src/utilities/time/inc/TimeBase.hpp`](../../../../utilities/time/inc/TimeBase.hpp) -- `TimeStandard` and `TimeProviderDelegate` foundations.
