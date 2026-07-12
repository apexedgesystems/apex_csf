# Ops Demo Checkout Results

**Executive:** ApexExecutive at 100 Hz, HARD_PERIOD_COMPLETE mode
**Platform:** Raspberry Pi 4 (4x Cortex-A72 @ 1.8 GHz, Debian 13 trixie)
**Kernel:** 6.12.62+rpt-rpi-v8, PREEMPT
**Run:** Initial baseline checkout, 2026-04-04

---

## System Under Test

ApexOpsDemo running as pure SIL with two configurable waveform generators,
system health monitor, and hot-swap test plugin. Exercises every APROTO C2
capability from the Python SDK.

```
Pool 0 (2 workers)
========================
WaveGenerator#0.step    @ 100 Hz (priority 127)
WaveGenerator#1.step    @ 100 Hz (priority 126)
WaveGenerator#0.tlm     @   1 Hz (priority -128, offset 51)
WaveGenerator#1.tlm     @   1 Hz (priority -128, offset 52)
SystemMonitor.tlm       @   1 Hz (priority -128, offset 53)
TestPlugin.tick          @  10 Hz (priority -64, offset 5)
```

---

## Checkout Results

| Test                                   | Result   | Detail                                 |
| -------------------------------------- | -------- | -------------------------------------- |
| 1. Connectivity (NOOP)                 | PASS     |                                        |
| 2. Component Addressing (8 components) | 8/8 PASS | All fullUids reachable                 |
| 3. Clock Rate                          | PASS     | Measured 101 Hz (expected ~100)        |
| 4. Executive Health                    | 6/6 PASS | 0 overruns, 0 watchdog warnings        |
| 5. Scheduler Health                    | 4/4 PASS | tickCount=10856, 6 tasks, 0 violations |
| 6. Interface Stats                     | PASS     | GET_STATS accepted                     |
| 7. Action Engine Stats                 | PASS     | GET_STATS accepted                     |
| 8. SystemMonitor Health                | PASS     | NOOP reachable                         |
| 9. WaveGen INSPECT State               | PASS     | stepCount=10860                        |
| 10. WaveGen INSPECT Output             | PASS     | output=-0.637, phase=0.610             |
| 11. WaveGen INSPECT Tunable            | PASS     | freq=1.0 Hz, amp=1.0, type=SINE        |
| 12. WaveGen GET_STATS                  | PASS     | output=-0.771, phase=0.640             |
| 13. Sleep / Wake                       | 3/3 PASS | Flag set/cleared, clock resumes        |
| 14. Lock / Unlock                      | 4/4 PASS | Including error case                   |
| 15. File Transfer (8KB)                | 3/3 PASS | CRC-32C verified                       |
| 16. File Transfer Abort                | 2/2 PASS | Recovery verified                      |
| 17. TPRM Reload                        | 2/2 PASS | Frequency changed 1.0 -> 10.0 Hz       |
| 19. C2 Latency (20 samples)            | PASS     | min=5.2ms, median=10.1ms, p99=20.8ms   |
| 20. Post-Test Health                   | 2/2 PASS | 100 Hz, 0 warnings                     |

**Total: 58 passed, 0 failed**

---

## C2 Latency Profile

| Metric | Value   |
| ------ | ------- |
| Min    | 5.2 ms  |
| Median | 10.1 ms |
| P99    | 20.8 ms |
| Max    | 20.8 ms |

Network: Ethernet (1 Gbps), dev machine to Pi 4.

---

## Skipped Tests

- **Library Hot-Swap** (--skip-reload-lib): Requires TestPlugin_v2.so deployed
- **Executive Restart** (--skip-restart): Destructive test, run separately

---

## Archived

- `results/checkout_baseline_rpi4.txt` -- Full checkout output
