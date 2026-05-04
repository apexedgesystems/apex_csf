# TimeServer Customer Integration Guide

Practical setup for integrating TimeServer into a production Apex
application. Covers wiring, TPRM tuning, and three reference mission
profiles (imaging satellite, rover, industrial) with their specific
constraints.

---

## 1. Mental model

TimeServer turns an external 1PPS reference into a system-wide UTC
clock. Components consume the time via:

- **TNT broadcast** on the internal bus (opcode `0x0602`, 40-byte
  payload, fired once per PPS edge).
- **Registry lookup** — `(componentId=6, OUTPUT, "output")` for the
  full state block.
- **`actionComp.iface().timeProvider`** — already wired by
  `ApexExecutive`, so any ATS sequence with an `AT_TIME` step fires
  on UTC.

Compute UTC at any moment with:

```cpp
const TimeAtNextTone& tnt = /* most recent broadcast */;
struct timespec now{};
clock_gettime(CLOCK_MONOTONIC, &now);
const int64_t steadyNowNs = now.tv_sec * 1'000'000'000LL + now.tv_nsec;
const int64_t utcNs = tnt.epochNs + (steadyNowNs - tnt.localNs);
```

---

## 2. Minimum integration (any mission)

Six steps. Skip steps 4-6 in dev / SIL builds.

### 2.1 Open the PPS source

```cpp
// In application init, after ApexExecutive::init() succeeds:
static apex::hal::LinuxPps pps("/dev/pps0");
const auto status = pps.init({});
if (status != apex::hal::PpsStatus::OK) {
  // /dev/pps0 missing or permission denied. Decide whether to abort
  // the mission or run in degraded mode (TimeServer broadcasts
  // NONE/UNKNOWN and ATS AT_TIME triggers wait forever).
  log.warning("PPS device unavailable; UTC-dependent features disabled");
}
exec.timeServer().setPpsSource(&pps);
```

For MCU targets, use the matching platform impl:

| Target              | HAL class                    |
| ------------------- | ---------------------------- |
| Linux/Jetson/RPi    | `apex::hal::LinuxPps`        |
| STM32 (L4/G4/H7/F4) | `apex::hal::stm32::Stm32Pps` |
| RP2040 (Pico)       | `apex::hal::pico::PicoPps`   |
| ESP32-S3            | `apex::hal::esp32::Esp32Pps` |
| ATmega328P          | `apex::hal::avr::AvrPps`     |
| TI F28004x          | `apex::hal::c2000::C2000Pps` |

### 2.2 Wire the GPS reference (`SET_REFERENCE_TIME` command)

A separate component (typically `GpsDriver`) parses NMEA from the
GPS receiver and sends UTC to TimeServer once per fix:

```cpp
SetReferenceTime cmd{};
cmd.epochNs       = parsedUtcNs;
cmd.source        = static_cast<uint8_t>(TimeSource::GPS);
cmd.quality       = static_cast<uint8_t>(TimeQuality::FINE);
cmd.referencePpsCount = nextExpectedPpsCount;
internalBus()->postInternalCommand(myUid, timeServerUid,
                                   TimeServer::OP_SET_REFERENCE_TIME,
                                   asPayload(cmd));
```

TimeServer pairs the reference with the next PPS edge it sees.

### 2.3 Set TPRM

Override the defaults in your TPRM file if needed:

```toml
[time_server.tunables]
mode               = 0      # PRIMARY
ppsDeviceIndex     = 0      # /dev/pps0
primaryRefSource   = 0      # GPS
maxStalenessUs     = 1500000  # 1.5 s before STALE
driftFilterTaps    = 16     # ~16 s convergence
holdoverLimitS     = 60     # 1 min before forcing FREERUN
```

### 2.4 (Optional) Capture broadcast TNTs in the application

`ApexExecutive` already wires the broadcast through `IInternalBus`,
so other registered components receive opcode `0x0602` at 1 Hz. If
the application needs a custom hook (telemetry, debug), wire it:

```cpp
exec.timeServer().setBroadcastDelegate({myTntCallback, &myCtx});
```

### 2.5 (Optional) Use ATS AT_TIME triggers

Already wired. Just author an ATS sequence with steps in
microseconds-since-epoch:

```toml
[[sequences]]
type = "ATS"
[[sequences.steps]]
delayCycles = 1730000000000000  # absolute UTC microseconds
action = { type = "COMMAND", commandOpcode = 0x0301, ... }
```

The step fires when `actionComp.iface().timeProvider() >= delayCycles`.

### 2.6 (Optional) Health monitoring

Subscribe to the OUTPUT block via the registry and watch
`correlationValid` for transitions. STALE / FREERUN should be
surfaced to mission operators.

---

## 3. Mission profiles

### 3.1 Imaging satellite

**Constraints**: geolocation accuracy depends on image timestamp
precision. ATS-driven imaging campaigns triggered at specific UTC
times. Multi-week missions; PPS must survive eclipse and warm-start.

**Recommended config**:

| Parameter         | Value     | Why                                                           |
| ----------------- | --------- | ------------------------------------------------------------- |
| `mode`            | PRIMARY   | Single GPS receiver per spacecraft.                           |
| `maxStalenessUs`  | 1'200'000 | Tighter than default — image timestamps are mission-critical. |
| `driftFilterTaps` | 32        | Smoother estimate over the long mission.                      |
| `holdoverLimitS`  | 600       | 10 min — allow GPS occlusion (eclipse) without going FREERUN. |

**Component policy**:

- Camera Driver: re-stamp each frame with `tnt.epochNs +
(frame_steady - tnt.localNs)` at exposure-end interrupt.
- Reject imaging operations below `quality=FINE`.
- Log `quality` alongside every image timestamp for ground-side
  reconstruction quality assessment.

**Wiring**:

```cpp
// Battery-backed GPS module recommended for warm-start (1-5 s vs
// 30-60 s cold acquisition).
```

### 3.2 Rover (mobile, intermittent GPS)

**Constraints**: GPS may drop in tunnels, urban canyons, indoor
ops. Operations continue on local clock; UTC accuracy degrades
gracefully. Ground commands may carry UTC timestamps.

**Recommended config**:

| Parameter           | Value                       | Why                                                                      |
| ------------------- | --------------------------- | ------------------------------------------------------------------------ |
| `mode`              | PRIMARY (or RELAY in fleet) | Single rover: PRIMARY. Multi-rover: relay TNT to followers.              |
| `holdoverLimitS`    | 30                          | Most coverage gaps resolve within ~30 s.                                 |
| Wall-clock fallback | wired                       | FREERUN re-anchors from CLOCK_REALTIME (NTP-disciplined when available). |

**Component policy**:

- Navigation: tolerate `quality=COARSE` — relative position math works
  on local clock; only absolute waypoints require FINE+.
- Telemetry downlink: stamp packets with quality bit. Ground-side
  filter rejects data below COARSE for time-critical analysis.

### 3.3 Industrial / fixed installation

**Constraints**: stationary, often Ethernet-connected. PTP available
on the LAN. May not have direct GPS at all.

**Recommended config**:

| Parameter        | Value    | Why                                                             |
| ---------------- | -------- | --------------------------------------------------------------- |
| `mode`           | PTP_SYNC | Trust ptp4l / chrony+PTP discipline of CLOCK_REALTIME.          |
| `maxStalenessUs` | (n/a)    | PTP_SYNC re-anchors every tick from the disciplined wall clock. |

**Wiring**:

```cpp
// PTP_SYNC mode: no PPS source needed. Just ensure ptp4l (or
// equivalent) is running on the host so CLOCK_REALTIME stays
// disciplined to the LAN PTP grandmaster.
TimeServerTunableParams tprm;
tprm.mode = static_cast<uint8_t>(TimeServerMode::PTP_SYNC);
exec.timeServer().loadTprm(tprm);
```

### 3.4 Distributed multi-node (RELAY)

**Constraints**: multiple processors, only the master has GPS.
Followers receive TNT over UART / Ethernet / serial bus.

**Recommended config**:

| Node               | Mode      | Notes                                                                                            |
| ------------------ | --------- | ------------------------------------------------------------------------------------------------ |
| Master             | PRIMARY   | Has GPS + PPS in. Broadcasts TNT.                                                                |
| Follower (own PPS) | SECONDARY | Has its own PPS from a fan-out; uses master's TNT as the reference. < 1 us accuracy.             |
| Follower (no PPS)  | RELAY     | Receives full TNT over network; latches local steady_clock. 0.1-5 ms accuracy depending on link. |

**Wiring (RELAY)**:

```cpp
// Bus consumer that receives the master's TNT forwards it to the
// local TimeServer via OP_ACCEPT_REMOTE_TNT.
TimeAtNextTone remoteTnt = decodeFromBus(...);
internalBus()->postInternalCommand(myUid, timeServerUid,
                                   TimeServer::OP_ACCEPT_REMOTE_TNT,
                                   asPayload(remoteTnt));
```

---

## 4. Integration checklist

Before flight / deployment:

- [ ] PPS device opens successfully on target hardware (verify
      `/dev/pps0` present and readable, or HAL impl init returns OK).
- [ ] TPRM mode matches deployment topology (PRIMARY for standalone,
      SECONDARY/RELAY/PTP_SYNC for distributed).
- [ ] GPS receiver wired and producing NMEA + PPS within spec'd
      acquisition time.
- [ ] First TNT broadcast observed within `holdoverLimitS` of init.
- [ ] Quality reaches `FINE` after first edge + reference, `PRECISE`
      after `driftFilterTaps` edges (~16 s default).
- [ ] STALE transition observed when PPS held off for
      `maxStalenessUs`.
- [ ] FREERUN transition observed when PPS held off for
      `holdoverLimitS`. Wall-clock re-anchor confirmed.
- [ ] Recovery via `resetCorrelation` + fresh reference returns
      to `VALID`.
- [ ] ATS AT_TIME steps fire within one frame of their target UTC.
- [ ] Telemetry includes `quality` and `valid` so ground operations
      can assess timestamp trustworthiness post-mission.

---

## 5. Common issues

| Symptom                       | Likely cause                                      | Fix                                                                                                              |
| ----------------------------- | ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `valid=NONE` after init()     | No PPS source wired or `/dev/pps0` missing        | Check `setPpsSource()` call; verify device tree / kernel module                                                  |
| `quality=COARSE` indefinitely | PPS arriving but no `SET_REFERENCE_TIME` from GPS | Verify GpsDriver is parsing NMEA and posting the command                                                         |
| Spurious `STALE` transitions  | `maxStalenessUs` too tight                        | Bump to 1.5-2 s; PPS jitter on Linux can spike to ~10 ms                                                         |
| Drift estimate stuck          | Filter taps not yet filled                        | Wait `driftFilterTaps` × 1 s; quality auto-promotes to PRECISE                                                   |
| ATS AT_TIME fires late        | Time provider returns 0 (no correlation)          | Check `valid` field on the latest TNT                                                                            |
| FREERUN UTC wildly wrong      | Wall-clock not wired                              | Call `setWallClock(defaultWallClock())` — `ApexExecutive` does this by default; custom executives must replicate |

---

## 6. Further reading

- [README.md](README.md) — Component reference (state machine, TPRM
  defaults, RT-safety, testing summary).
- [src/system/core/hal/base/IPps.hpp](../../../hal/base/IPps.hpp) —
  HAL contract for PPS sources.
- [apps/apex_time_demo](../../../../../apps/apex_time_demo) —
  Standalone demo of the four canonical scenarios.
