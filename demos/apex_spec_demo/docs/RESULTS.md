# Spec Demo Checkout Results

**Executive:** ApexExecutive at 100 Hz, HARD_PERIOD_COMPLETE mode
**Platform:** x86_64 SIL (build-base container, Intel Core Ultra 7 165H)
**Run:** Phase-3 fleet baseline checkout, 2026-08-16

Raw capture: [results/checkout_baseline_x86_64.txt](results/checkout_baseline_x86_64.txt)

---

## System Under Test

ApexSpecDemo running as pure SIL: eight spec-born component instances
spanning all three tiers (SW_MODEL, DRIVER, SUPPORT), both authoring
formats, the full type vocabulary, every constraint kind, and
multi-instance configuration -- plus the system health monitor. Every
spec command is driven live; effects verified through INSPECT of the
spec-generated data blocks.

```
Pool 0
========================
SpecSensor.step       @ 50 Hz (priority 127)
SpecActuator.step     @ 50 Hz (priority 126)
SystemMonitor.tlm     @  1 Hz (priority -128, offset 25)
```

---

## Checkout Results

| Test                              | Result     | Detail                                                                                        |
| --------------------------------- | ---------- | --------------------------------------------------------------------------------------------- |
| 1. Connectivity (6 components)    | 6/6 PASS   | Executive + all registered fullUids                                                           |
| 2. Boot TPRM (spec tunables)      | 4/4 PASS   | driftRate=0.5, ref=25.0, mode=MEASURE                                                         |
| 3. Model running (50 Hz)          | 5/5 PASS   | 51 samples/s, sequence advancing                                                              |
| 4. SetMode IDLE / MEASURE         | 6/6 PASS   | Sampling frozen in IDLE, resumed in MEASURE                                                   |
| 5. Mode guard (FAULT_INJECT)      | 4/4 PASS   | Rejected from IDLE (rejects++), bias +50 visible from MEASURE                                 |
| 6. Recalibrate                    | 4/4 PASS   | ref 25.0 -> 30.0 live, drift 1.43 -> 0.15                                                     |
| 7. GetStats / Reset               | 6/6 PASS   | Counters zeroed, mode to tunable default                                                      |
| 8. Malformed payload (2B SetMode) | 2/2 PASS   | Mode AND rejects unchanged -- user code never ran                                             |
| 9. Unknown opcode (0x02FF)        | 3/3 PASS   | Tier-base fallthrough, model healthy after                                                    |
| 10. Actuator boot TPRM            | 4/4 PASS   | Proto-authored tunables live (rateLimit 8.0, holdBand 0.1, bounded string axisLabel "X-AXIS") |
| 11. Actuator slew                 | 6/6 PASS   | Target set, moves++, ramp at rate limit, settles in hold band                                 |
| 12. Halt / GetPosition            | 4/4 PASS   | Target frozen at position, holds; GetPosition accepted                                        |
| 13. Actuator negatives            | 4/4 PASS   | Out-of-range Move rejected (rejects++); malformed Move never reaches user code                |
| 14. Bus driver (DRIVER)           | 10/10 PASS | Loopback tx==rx, byte counters, oversize frame rejected                                       |
| 15. Matrix (SUPPORT, full vocab)  | 14/14 PASS | All 15 type lanes intact incl. strings/bytes; independent checksum cross-proof                |
| 16. Limits (constraint rails)     | 10/10 PASS | Every rail kind at edge values; in-band nudge applies, out-of-band rejects                    |
| 17. ProtoMax (maximal profile)    | 9/9 PASS   | Every proto feature live; checksum cross-proof                                                |
| 18. Channels (multi-instance)     | 9/9 PASS   | One spec, two configs; opposite ramps; instance-isolated Zero                                 |
| 19. Post-test health              | PASS       | Clock 99 Hz measured                                                                          |

**Total: 124 passed, 0 failed**

---

## Cross-checks

- Regression: `apex_ops_demo` checkout 143/143 on the same build.
- `check-cdef`: committed `.auto` trees -- headers, dispatch, and the
  emitted `.proto` interfaces -- match their specs byte-exact.
- Fixpoint: emit->ingest->emit stable with identical layout hashes
  (rust suite); the actuator's boot TPRM verifies the proto-derived
  hash at load.
- Stub refusal: a second `cdef_gen --stub` run exits 1 and leaves the
  user-owned component file untouched.
- Zenith target: the actuator's command panel (Move/Halt/GetPosition
  with typed fields) derives from the proto-authored dictionary.
