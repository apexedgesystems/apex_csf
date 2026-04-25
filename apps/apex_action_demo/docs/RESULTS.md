# Action Demo Checkout Results

**Executive:** ApexExecutive at 10 Hz
**Platform:** Raspberry Pi 4 (4x Cortex-A72 @ 1.8 GHz, Debian 13 trixie)
**Kernel:** 6.8.0-107-generic, PREEMPT
**Run:** Initial baseline checkout, 2026-04-19

---

## System Under Test

ApexActionDemo running as pure SIL with temperature sensor model, action
engine (128 watchpoints, sequence catalog, computed functions), DataTransform
fault injection, and system health monitor. Exercises the full action engine
pipeline from telemetry monitoring through command sequencing.

```
Pool 0
========================
SensorModel.step       @ 10 Hz (priority 127)
SensorModel.tlm        @  1 Hz (priority -128)
DataTransform.tlm      @  1 Hz (priority -128)
SystemMonitor.tlm      @  1 Hz (priority -128)
```

---

## Checkout Results

| Test | Result | Detail |
|------|--------|--------|
| 1. Connectivity | PASS | NOOP to executive |
| 2. Component Addressing (7) | 7/7 PASS | All fullUids reachable |
| 3. Clock Rate | PASS | 10 cycles/s |
| 4. Executive Health | 3/3 PASS | Clock running, not paused |
| 5. Sensor Output | 2/2 PASS | Temperature, rate, overtemp |
| 6. Action Engine Stats | 3/3 PASS | Cycling, RTS loaded |
| 7. DataTransform Stats | PASS | Masks applied |
| 8. Autonomous Campaign | PASS | TPRM-driven WP/RTS/ATS |
| 9. Watchpoint Verification | 2/2 PASS | WP0 fired, multiple WPs |
| 10. Event Notifications | PASS | Invocations counted |
| 11. RTS Fault Campaign | 3/3 PASS | Steps, commands, masks |
| 12. RTS Chaining | 2/2 PASS | CLEAR_ALL deterministic |
| 13. ATS Fault Campaign | 4/4 PASS | Timed faults from boot |
| 14. Watchpoint Groups | 2/2 PASS | AND + OR |
| 15. Nested Triggering | 2/2 PASS | Fault -> detect -> respond |
| 16. Direct Ground Fault | 6/6 PASS | SET_TARGET through CLEAR_ALL |
| 17. Wait Condition | 4/4 PASS | Embedded WP, timeout/SKIP, ARM_CONTROL |
| 18. Endianness Proxy | PASS | Byte-swap verified |
| 19. Complex Scenarios | 5/5 PASS | Chaining, preemption, blocking |
| 20. Abort Events | 2/2 PASS | Preemption fires cleanup |
| 21. Exclusion Groups | 2/2 PASS | Mutual exclusion stops RTS |
| 22. ABORT_ALL_RTS | 2/2 PASS | Slots freed |
| 23. GET_CATALOG | 2/2 PASS | Catalog lookup works |
| 24. GET_STATUS | 3/3 PASS | Sequence steps fire |
| 25. Sleep / Wake | 4/4 PASS | Flag set/cleared |
| 26. Resource Catalogs | 7/7 PASS | WP/group/notification activate/deactivate |
| 27. C2 Latency | PASS | avg=10.3ms |
| 28. Post-Test Health | PASS | Clock advancing |

**Total: 76 passed, 0 failed**

---

## C2 Latency Profile

| Metric | Value |
|--------|-------|
| Min | 7.0 ms |
| Avg | 10.3 ms |
| Max | 13.2 ms |

Network: Ethernet (1 Gbps), dev machine to Pi 4.

---

## Archived

- `results/checkout_baseline_rpi4.txt` -- Full checkout output
