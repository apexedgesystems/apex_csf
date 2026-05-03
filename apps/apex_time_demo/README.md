# Time Demo

Demonstrates `TimeServer` end-to-end: PPS edge ingest, UTC correlation,
TNT broadcast, ATS AT_TIME triggers, and graceful degradation /
recovery. No real GPS receiver or `/dev/pps[N]` needed -- a `MockPps`
backs a built-in GPS simulator thread that drives the scenarios.

## Components

| Component       | Type     | Purpose                                          |
| --------------- | -------- | ------------------------------------------------ |
| TimeServer      | CORE     | Sole UTC authority (auto-registered by executive)|
| MockPps         | HAL      | Software-driven PPS source (wired to TimeServer) |
| SystemMonitor   | SUPPORT  | CPU / memory / FD health monitoring              |

## Scenarios (driven by built-in GPS simulator)

| t (s)   | Behavior                                               |
| ------- | ------------------------------------------------------ |
| 0 - 3   | Cold-start dark period: no edges, no reference, valid=NONE |
| 3       | GPS fix acquired: SET_REFERENCE_TIME delivered, first edge |
| 3 - 18  | 1 Hz edges; quality climbs NONE -> FINE -> PRECISE      |
| 18 - 25 | Simulated PPS dropout; valid -> STALE -> FREERUN        |
| 25      | resetCorrelation + fresh reference; valid -> FINE       |
| 25+     | Resumed 1 Hz edges; PRECISE returns                     |

## Building

```bash
make compose-debug
```

Produces `build/hosted-x86_64-debug/bin/ApexTimeDemo`.

## See Also

- [docs/DESIGN.md](docs/DESIGN.md) -- internals of the simulator and
  what each scenario exercises.
- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide
  with ops-client commands.
- [docs/RESULTS.md](docs/RESULTS.md) -- expected scenario timeline and
  values to verify against.
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- packaging /
  on-target deployment notes.
- [../../src/system/core/components/time_server/README.md](../../src/system/core/components/time_server/README.md)
  -- TimeServer component reference.
- [../../src/system/core/components/time_server/CUSTOMER_INTEGRATION.md](../../src/system/core/components/time_server/CUSTOMER_INTEGRATION.md)
  -- production integration playbook.
