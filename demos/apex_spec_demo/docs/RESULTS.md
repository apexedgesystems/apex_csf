# Spec Demo Checkout Results

**Executive:** ApexExecutive at 100 Hz, HARD_PERIOD_COMPLETE mode
**Platform:** x86_64 SIL (build-base container, Intel Core Ultra 7 165H)
**Run:** Initial baseline checkout, 2026-08-08

Raw capture: [results/checkout_baseline_x86_64.txt](results/checkout_baseline_x86_64.txt)

---

## System Under Test

ApexSpecDemo running as pure SIL: one spec-born drift sensor plus the
system health monitor under the standard executive. Every command in
`sensor/apex_data.toml` is driven live; effects verified through
INSPECT of the spec-generated data blocks.

```
Pool 0
========================
SpecSensor.step       @ 50 Hz (priority 127)
SystemMonitor.tlm     @  1 Hz (priority -128, offset 25)
```

---

## Checkout Results

| Test                              | Result   | Detail                                                        |
| --------------------------------- | -------- | ------------------------------------------------------------- |
| 1. Connectivity (6 components)    | 6/6 PASS | Executive + all registered fullUids                           |
| 2. Boot TPRM (spec tunables)      | 4/4 PASS | driftRate=0.5, ref=25.0, mode=MEASURE                         |
| 3. Model running (50 Hz)          | 5/5 PASS | 51 samples/s, sequence advancing                              |
| 4. SetMode IDLE / MEASURE         | 6/6 PASS | Sampling frozen in IDLE, resumed in MEASURE                   |
| 5. Mode guard (FAULT_INJECT)      | 4/4 PASS | Rejected from IDLE (rejects++), bias +50 visible from MEASURE |
| 6. Recalibrate                    | 4/4 PASS | ref 25.0 -> 30.0 live, drift 1.43 -> 0.15                     |
| 7. GetStats / Reset               | 6/6 PASS | Counters zeroed, mode to tunable default                      |
| 8. Malformed payload (2B SetMode) | 2/2 PASS | Mode AND rejects unchanged -- user code never ran             |
| 9. Unknown opcode (0x02FF)        | 3/3 PASS | Tier-base fallthrough, model healthy after                    |
| 10. Post-test health              | PASS     | Clock 99 Hz measured                                          |

**Total: 41 passed, 0 failed**

---

## Cross-checks

- Regression: `apex_ops_demo` checkout 143/143 on the same build.
- `check-cdef`: committed `.auto` headers match the spec byte-exact.
- Stub refusal: a second `cdef_gen --stub` run exits 1 and leaves the
  user-owned component file untouched.
