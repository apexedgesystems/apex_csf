# Time Server Component

**Namespace:** `system_core::time_server`
**Platform:** POSIX hosted (Linux, Jetson, RPi, RISC-V SoCs)
**C++ Standard:** C++23

Sole authority for wall-clock time in an Apex application. Ingests an
external 1PPS edge through the IPps HAL, optionally pairs it with a UTC
reference (from GPS, ground command, manual entry, or onboard
oscillator), correlates the local steady clock to UTC, and broadcasts
the `TimeAtNextTone` (TNT) message every PPS edge. Components compute
current UTC by interpolating against the most recent TNT.

---

## 1. Quick Reference

| Module                    | Purpose                                                | RT-Safe |
| ------------------------- | ------------------------------------------------------ | ------- |
| `TimeServer`              | Core component owning correlation, drift, state machine | Yes     |
| `TimeAtNextTone`          | 40-byte broadcast message (1 Hz)                       | N/A     |
| `SetReferenceTime`        | Provider → TimeServer command (16 bytes)               | N/A     |
| `SetTimeManual`           | No-PPS UTC fallback (8 bytes)                          | N/A     |
| `TimeServerTunableParams` | TPRM block (16 bytes)                                  | N/A     |
| `TimeServerOutput`        | Registry OUTPUT block (56 bytes)                       | N/A     |
| `IPps` (HAL)              | Abstract PPS edge capture interface                    | Yes     |
| `LinuxPps`                | `/dev/pps[N]` PPS_FETCH ioctl impl                     | Yes     |
| `Stm32Pps`                | EXTI + DWT cycle counter impl                          | Yes     |
| `MockPps`                 | Software-injected edges for tests                      | Yes     |

### Question-to-Answer

| Question                                              | Answer                                                          |
| ----------------------------------------------------- | --------------------------------------------------------------- |
| How do I get current UTC?                             | Read `TimeAtNextTone.epochNs` and interpolate (see §4).         |
| How accurate is the correlation?                      | Sub-microsecond on a hosted Linux box with kernel PPS support.  |
| What if the GPS receiver loses fix?                   | Quality drops to `COARSE`; UTC continues advancing on local clock. |
| What if PPS stops entirely?                           | Goes to `STALE` then `FREERUN`; valid bit indicates trust level. |
| Can ATS sequences trigger on UTC?                     | Yes, `actionComp.iface().timeProvider = ts.utcTimeProvider()`.   |
| How do I supply the reference time from a GPS driver? | Send `SET_REFERENCE_TIME` command with the UTC at the PPS edge. |

---

## 2. Design Principles

- **Sole authority.** TimeServer is the only place in the system that
  owns the local-to-UTC correlation. Components never read GPS NMEA
  directly — they consume TNT or the OUTPUT block.
- **Steady-clock domain.** All correlation math happens in
  CLOCK_MONOTONIC nanoseconds (or its MCU equivalent). UTC is only
  introduced via reference-time commands.
- **No allocation.** Drift estimator is a fixed-size ring buffer
  (capped at `MAX_DRIFT_TAPS = 64`). All paths are RT-safe.
- **Graceful degradation.** TNT broadcast continues with explicit
  `valid` and `quality` indicators when correlation is degraded;
  consumers decide their own policy.

---

## 3. Architecture

```
GPS Receiver / Ground / Onboard          APEX Application
-----------------------------            ----------------

  NMEA / Ground UTC ----[command]----+
                                     |
                                     v
                              SET_REFERENCE_TIME
                                     |
                                     v
  PPS edge --[IPps.readCapture]----> TimeServer
                                     |
                                     | - Pair edge + reference
                                     | - Run drift estimator
                                     | - Broadcast TNT
                                     | - Update OUTPUT registry
                                     |
                                     v
                            (broadcast / registry)
                                     |
              +----------------------+--------------------+
              |                      |                    |
        TNT consumers          ATS engine            Telemetry
        (component bus)        (AT_TIME triggers)    (downlink)
```

---

## 4. Computing UTC

```cpp
// Read most recent TNT (from broadcast subscription or registry).
const TimeAtNextTone& tnt = /* ... */;

// Interpolate at the moment of interest.
struct timespec now{};
clock_gettime(CLOCK_MONOTONIC, &now);
const int64_t steadyNowNs = now.tv_sec * 1'000'000'000LL + now.tv_nsec;
const int64_t utcNs = tnt.epochNs + (steadyNowNs - tnt.localNs);

// Or use TimeServer directly via the OUTPUT accessor:
const int64_t utc = timeServer.currentUtcNs();
```

The interpolation formula is exact at the moment of the most recent
edge. Drift accumulates between edges; the published `driftPpb` field
lets consumers apply correction if they care about sub-millisecond
accuracy.

---

## 5. State Machine

| `valid`   | `quality` | Meaning                                           |
| --------- | --------- | ------------------------------------------------- |
| `NONE`    | `UNKNOWN` | Boot. No edges, no reference.                     |
| `VALID`   | `COARSE`  | PPS ticking, no fresh reference (HOLDOVER).       |
| `VALID`   | `FINE`    | PPS + reference paired; drift not yet stable.     |
| `VALID`   | `PRECISE` | PPS + reference + drift filter has filled.       |
| `STALE`   | (kept)    | No edge for `> maxStalenessUs`.                   |
| `FREERUN` | `COARSE`  | No edge for `> holdoverLimitS`; running on monotonic only. |

Transitions are observable in real time through the broadcast TNT.
Components decide their own policy on what trust level is acceptable.

---

## 6. Wiring

```cpp
// In an application that uses ApexExecutive:

// 1. Open the PPS source.
static apex::hal::LinuxPps pps("/dev/pps0");
if (pps.init({}) != apex::hal::PpsStatus::OK) {
  // /dev/pps0 missing -- system runs without UTC. Decide whether to abort.
}

// 2. Wire it into the executive's TimeServer.
exec.timeServer().setPpsSource(&pps);

// 3. Optional: wire the broadcast delegate to your bus.
exec.timeServer().setBroadcastDelegate({myTntPublisherFn, &myBus});

// 4. Reference time comes from a separate component (e.g. GpsDriver)
//    that sends SET_REFERENCE_TIME commands as it parses NMEA.
```

The executive registers TimeServer automatically and ticks it each
frame; the application only needs to wire the platform-specific PPS
source and (optionally) the broadcast.

---

## 7. TPRM Defaults

| Parameter           | Default     | Purpose                                                |
| ------------------- | ----------- | ------------------------------------------------------ |
| `mode`              | `PRIMARY`   | Sole authority. Other modes are reserved for Phase 4. |
| `ppsDeviceIndex`    | 0           | `/dev/pps0`.                                           |
| `primaryRefSource`  | `GPS`       | Most common deployment.                                |
| `maxStalenessUs`    | 1.5 s       | Tolerates one missed PPS edge before STALE.            |
| `driftFilterTaps`   | 16          | Convergence after ~16 s.                               |
| `holdoverLimitS`    | 60 s        | Operator policy: 1 min before forcing FREERUN.         |

Each is overridable via TPRM at deployment time without recompiling.

---

## 8. Limitations / Out of Scope

- **Distributed timing** — SECONDARY mode (cross-node sync via
  shared PPS), RELAY mode (TNT over network), PTP (IEEE 1588), and
  CAN HW timestamping are reserved as TPRM enum values but not
  implemented in this branch. Tracked as Phase 4 of the originating
  ticket.
- **MCU platforms beyond STM32** — Pico, ESP32, AVR, and C2000
  PPS implementations follow the same `IPps` contract but require
  per-platform headers. Tracked as Phase 5.
- **GpsDriver** — TimeServer accepts `SET_REFERENCE_TIME` commands;
  the GPS NMEA parser that produces them is a separate component.
- **Leap-second handling** — TNT reserves a flag bit
  (`TNT_FLAG_LEAP_SECOND_PENDING`) but TimeServer does not currently
  schedule the leap second insertion itself; that's expected to come
  from the ground command path.
- **Non-1Hz PPS rates** — glitch rejection bounds (500ms / 1500ms)
  are constexpr. A 5 Hz or 10 Hz source would require these to
  become TPRM-tunable.

---

## 9. Testing

44 unit tests + 7 integration tests in
`apex/utst/TestSystemCoreTimeServer`:

```
TimeServerData (19)   layout, defaults, enum encoding, toString, flags, memcpy
TimeServer     (25)   correlation, drift, glitch rejection, state machine
TimeServerIntegration (7) ATS time provider, boot convergence
```

HAL coverage in `TestHalInterface`, `TestHalMock`, `TestHalLinux`,
`TestHalStm32` (51 tests).

---

## 10. See Also

- `src/system/core/hal/base/IPps.hpp` — HAL contract.
- `src/system/core/hal/linux/inc/LinuxPps.hpp` — `/dev/pps[N]` impl.
- `src/system/core/hal/stm32/inc/Stm32Pps.hpp` — STM32 EXTI/DWT impl.
- `src/system/core/hal/mock/inc/MockPps.hpp` — Test substrate.
- `src/utilities/time/inc/TimeBase.hpp` — `TimeStandard` and
  `TimeProviderDelegate` foundations.
