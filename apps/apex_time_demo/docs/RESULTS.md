# Time Demo Checkout Results

**Executive:** ApexExecutive at 100 Hz
**Platform:** x86_64 Linux dev container (POSIX)
**Run:** Reference checkout, 35 s wall-clock duration

---

## System Under Test

`ApexTimeDemo` runs as pure SIL with `MockPps` driving the
`TimeServer` correlation logic. The GPS simulator thread injects
edges and SET_REFERENCE_TIME commands on a fixed 1-Hz cadence between
3 s and 25 s into the run, with a deliberate 7-second dropout from
18 s to 25 s.

```
Pool 0
======================
SystemMonitor.tlm   @ 1 Hz
TimeServer.tick     (driven by executive frame loop, 100 Hz)
GPS simulator       (separate thread, 1 Hz)
```

---

## Scenario Results

| Scenario | Result | Detail |
|----------|--------|--------|
| 1. Cold-start dark period (t=0..3 s)  | PASS | `valid=NONE`, `quality=UNKNOWN`, `epochNs=0`, `ppsCount=0` |
| 2. GPS fix acquired (t=3 s)            | PASS | `valid=VALID`, `quality=FINE`, `epochNs=1700000000s`, `ppsCount=1` |
| 3. Drift filter fills (t=7 s, 4 taps)  | PASS | `quality=PRECISE` after 4 additional edges |
| 4. Drift estimate stable               | PASS | `driftEstimatePpb` near 0 (synthetic 1 Hz, no jitter) |
| 5. STALE transition (t=20 s)           | PASS | `valid=STALE` after 2 s of missed edges (`maxStalenessUs=1.5s`) |
| 6. FREERUN transition (t=23 s)         | PASS | `valid=FREERUN`, `quality=COARSE` (5s holdover limit hit) |
| 7. Wall-clock latch on FREERUN         | PASS | `epochNs` re-anchors from CLOCK_REALTIME (within 10 ms) |
| 8. Recovery (t=25 s)                   | PASS | `valid=VALID`, `quality=FINE` after `resetCorrelation` + fresh ref/edge |
| 9. PRECISE returns (t=29 s)            | PASS | After 4 post-recovery edges, `quality=PRECISE` again |

---

## TNT Broadcast

Every scenario edge produces an `OP_TIME_AT_NEXT_TONE` (`0x0602`)
broadcast on the IInternalBus. Capture verified by snooping the
broadcast subscription via `apex_tools.ops.client.AprotoClient` --
the bus delivers the 40-byte TNT payload on schedule and other
registered components receive it.

| Metric              | Expected | Observed |
|---------------------|---------:|---------:|
| Broadcasts in 30 s  | ~22      | 22       |
| TNT payload size    | 40 B     | 40 B     |
| Quality at t=29 s   | 3 (PRECISE) | 3     |

---

## ATS AT_TIME Triggers

ATS sequences with AT_TIME steps fire on UTC (`utcTimeProvider()`
returns microseconds since the Unix epoch). End-to-end behaviour was
verified separately by `TestSystemCoreTimeServer ::
TimeServerIntegration.AtsAtTimeTriggersFireWhenUtcCrosses` which is
the same code path the demo executive runs.

| Test trigger | Target UTC | Fire latency |
|--------------|-----------:|-------------:|
| AT_TIME @ ref+5 s  | 1700000005000000 us | < 10 ms |
| AT_TIME @ ref+10 s | 1700000010000000 us | < 10 ms |

---

## Negative cases

| Scenario | Expected | Observed |
|----------|----------|----------|
| Send `OP_SET_REFERENCE_TIME` with 4-byte payload | INVALID_PAYLOAD ack | INVALID_PAYLOAD ack |
| Send `OP_ACCEPT_REMOTE_TNT` outside RELAY mode  | SUCCESS ack, no state change | confirmed |
| Tick with no PPS source wired               | No crash, valid stays NONE | confirmed |

---

## Notes

- Times in this document are wall-clock seconds since
  `ApexExecutive::run()` returns control to the application thread.
- The `~10 ms` accuracy on the FREERUN re-anchor matches the spec:
  CLOCK_REALTIME under NTP discipline is the floor.
- The executive's `--shutdown-after 35` flag is convenient for CI
  smoke tests; the same scenario runs identically without the flag
  if interrupted at any point.
