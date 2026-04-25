# Action Demo Application Design

## Purpose

ApexActionDemo demonstrates the action engine's telemetry monitoring, onboard
command sequencing, and runtime data mutation capabilities. It provides a
self-contained application that exercises watchpoints, event notifications,
RTS/ATS sequencing, fault injection, and data proxying against a deterministic
sensor model. No hardware dependencies -- runs as a pure SIL process on any
POSIX host or Raspberry Pi.

## Architecture

```
+------------------------------------------------------------------+
|                      ApexActionDemo (POSIX)                       |
|                                                                   |
|  +--------------------+   +--------------------+                  |
|  |  SensorModel       |   |  DataTransform     |                  |
|  |  (0xD200)          |   |  (0xCA00)          |                  |
|  |  Temp ramp 20-120  |   |  ByteMaskProxy     |                  |
|  |  Overtemp detect   |   |  per entry         |                  |
|  |  EndiannessProxy   |   |  Command-driven    |                  |
|  +--------+-----------+   +---------+----------+                  |
|           |                         ^                             |
|           | OUTPUT (temp,rate,ot)   | SET_TARGET / ARM / APPLY    |
|           v                         |                             |
|  +--------+-----------+             |                             |
|  |  ActionComponent   +-------------+                             |
|  |  (0x0500)          |                                           |
|  |  128 watchpoints   |   +--------------------+                  |
|  |  32 RTS slots      |   |  SystemMonitor     |                  |
|  |  Sequence catalog   |   |  (0xC800)          |                  |
|  |  Computed functions |   +--------------------+                  |
|  +--------------------+                                           |
|                                                                   |
|  +--------------------+                                           |
|  |  ApexInterface     |                                           |
|  |  TCP:9000          |                                           |
|  +--------------------+                                           |
+------------------------------------------------------------------+
         ^                                    |
         |          TCP + SLIP + APROTO       |
         +------------------------------------+
         |
   Zenith / checkout.py / AprotoClient
```

## Component Design

### SensorModel (componentId=210, SW_MODEL)

Deterministic temperature ramp for action engine testing:

- **sensorStep task (10 Hz):** Advances temperature linearly from 20 to 120
  at 0.5 deg/tick, wrapping back to 20 at the top. Sets overtemp flag when
  temperature exceeds the configured threshold. RT-safe: bounded float math,
  no allocation.

- **Three registered data blocks:**

  - OUTPUT (12B): temperature (float), rate (float), overtemp (uint8)
  - INPUT (24B): native SensorOutput + byte-swapped copy via EndiannessProxy
  - TUNABLE_PARAM: configurable thresholds

- **EndiannessProxy:** Publishes a byte-swapped copy of the OUTPUT block,
  allowing verification of endianness-aware data proxying without hardware.

### DataTransform (componentId=202, SUPPORT)

Command-driven byte-level data mutation engine:

- **ByteMaskProxy per entry:** Each of the 8 entries targets a specific data
  block (identified by fullUid + category + offset + length). Supports
  PUSH_ZERO, PUSH_HIGH, PUSH_FLIP mask operations that compose before APPLY.

- **Command interface:** SET_TARGET, ARM_ENTRY, DISARM_ENTRY, PUSH_ZERO_MASK,
  PUSH_HIGH_MASK, PUSH_FLIP_MASK, APPLY_ENTRY, CLEAR_ALL, SET_TARGET.

- **Stats:** Tracks applyCycles, masksApplied, resolveFailures, applyFailures,
  entriesArmed via INSPECT on STATE category.

### ActionComponent (componentId=5, CORE)

Central action engine with full sequencing capabilities:

- **128 watchpoints:** Monitor data blocks with GT, LT, EQ, NE, GE, LE
  predicates. Edge-triggered (fire once per transition). Support computed
  functions (rate-of-change, moving average).

- **32 RTS slots:** Relative time sequences loaded from binary files via
  the sequence catalog. Each step can route commands, apply ARM_CONTROL,
  wait on conditions with timeout/SKIP policy, chain to other sequences,
  and set delay cycles.

- **Sequence catalog:** O(log N) lookup by sequence ID. Caches loaded
  binaries. Supports hot-add via filesystem scan (RESCAN_CATALOG command).

- **Watchpoint groups:** AND/OR combinators over watchpoint sets. Fire
  composite events when all (AND) or any (OR) member watchpoints are active.

- **Event notifications:** Map event IDs to WARNING/ERR log messages.
  Runtime activate/deactivate via resource catalog commands.

- **Priority preemption:** Higher-priority sequences can preempt lower-priority
  slots. Preempted sequences fire abort events for cleanup notification.

- **Exclusion groups:** Mutual exclusion enforces one-at-a-time within a group.

- **Blocking:** Sequences can declare blockers that prevent execution while
  the blocker is running.

## TPRM Campaign

The boot configuration (master.tprm) loads a complete autonomous campaign:

| Resource | Configuration | Trigger |
| -------- | ------------- | ------- |
| WP0 | temp > 50, eventId=1 | Edge fires event 1 -> starts RTS 000 |
| WP1 | temp > 80, eventId=2 | Notification: "TEMP >80 WARNING" |
| WP2 | overtemp flag, eventId=3 | Notification: "OVERTEMP DETECTED" |
| WP3 | temp > 100, eventId=4 | Edge fires event 4 |
| WP4 | temp == 0.0, eventId=5 | Detects fault-injected zeroed data |
| GP0 | WP1 AND WP2, eventId=10 | Notification: "TEMP+OVERTEMP GROUP FIRE" |
| GP1 | WP0 OR WP3, eventId=11 | Composite OR trigger |
| RTS 000 | 8-step fault campaign | SET_TARGET, ARM, masks, APPLY, DISARM |
| RTS 001 | CLEAR_ALL cleanup | Chained from RTS 000 |
| ATS | 5 timed faults | Cycles 100, 200, 350, 400, 550 |

The sensor ramp crosses each threshold deterministically, triggering the full
watchpoint -> event -> notification -> sequence -> command chain autonomously.

## Checkout Coverage

The checkout script (`scripts/checkout.py`) executes 77 tests across 28
sections covering:

| Section | Coverage |
| ------- | -------- |
| 1. Connectivity | NOOP to executive |
| 2. Component addressing | NOOP to all 7 components |
| 3. Clock rate | Verify execution rate |
| 4. Executive health | Health packet parsing |
| 5. Sensor output | Temperature, rate, overtemp via INSPECT |
| 6. Action engine stats | Cycling, RTS loaded at boot |
| 7. DataTransform stats | Mask application tracking |
| 8. Autonomous campaign | TPRM-driven WP/RTS/ATS execution |
| 9. Watchpoint fired | WP0 edge detection |
| 10. Notifications fired | Event notification invocations |
| 11. RTS fault campaign | Sequencer steps, command routing, masks |
| 12. RTS chaining | CLEAR_ALL cleanup via chained RTS |
| 13. ATS fault campaign | Timed faults from boot-generated ATS |
| 14. Watchpoint groups | AND/OR group fire verification |
| 15. Nested triggering | Fault -> detect -> respond chain |
| 16. Direct ground fault | SET_TARGET, ARM, PUSH_ZERO, APPLY, CLEAR_ALL |
| 17. Wait condition | Embedded watchpoint with timeout/SKIP |
| 18. Endianness proxy | Byte-swapped sensor output |
| 19. Complex scenarios | Chaining, priority preemption, blocking |
| 20. Abort events | Preemption fires cleanup event |
| 21. Exclusion groups | Mutual exclusion stops conflicting RTS |
| 22. ABORT_ALL_RTS | Stop all running RTS, verify slots freed |
| 23. GET_CATALOG | Catalog command + lookup verification |
| 24. GET_STATUS | Status command + sequence step firing |
| 25. Sleep / Wake | Pause clock, verify stall, resume |
| 26. Resource catalogs | Deactivate/reactivate WP, group, notification |
| 27. C2 latency | 20-ping round-trip measurement |
| 28. Post-test health | Clock advancing, final statistics |
