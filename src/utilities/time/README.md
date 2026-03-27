# Time Utilities

**Namespace:** `apex::time`
**Platform:** POSIX (clock_gettime)
**C++ Standard:** C++17
**Type:** Header-only

Time standard definitions, system clock providers, and conversion utilities
for real-time and embedded systems. Provides a delegate-based time provider
interface that decouples consumers (sequencing engines, telemetry timestamps)
from specific clock sources.

---

## Quick Reference

| Header             | Purpose                                                              | RT-Safe |
| ------------------ | -------------------------------------------------------------------- | ------- |
| `TimeBase.hpp`     | TimeStandard enum, TimeProviderDelegate, Timestamp struct, constants | Yes     |
| `SystemClocks.hpp` | Concrete clock functions (MONOTONIC, UTC, TAI, GPS, MET)             | Yes     |
| `TimeConvert.hpp`  | Unit and standard conversions                                        | Yes     |

---

## Time Standards

| Standard  | Function                | Monotonic | Leap-Safe | Use Case                        |
| --------- | ----------------------- | --------- | --------- | ------------------------------- |
| CYCLE     | (none, default)         | Yes       | N/A       | Scheduler-driven timing         |
| MONOTONIC | `monotonicMicroseconds` | Yes       | Yes       | Mission elapsed time, HIL       |
| UTC       | `utcMicroseconds`       | No        | No        | Calendar scheduling             |
| TAI       | `taiMicroseconds`       | Yes       | Yes       | CCSDS, space operations         |
| GPS       | `gpsMicroseconds`       | Yes       | Yes       | Navigation systems              |
| MET       | `metMicroseconds`       | Yes       | Yes       | Mission elapsed time from epoch |

All clock functions return `uint64_t` microseconds. All use `clock_gettime()`
which is vDSO-accelerated on Linux (no syscall, ~20ns).

---

## API Reference

### TimeProviderDelegate

```cpp
using TimeProviderDelegate = apex::concurrency::Delegate<std::uint64_t>;
```

Delegate returning current time as microseconds. Wire into consumers:

```cpp
// Monotonic clock
iface.timeProvider = {apex::time::monotonicMicroseconds, nullptr};

// TAI (requires context for leap second offset)
static apex::time::TaiContext taiCtx{37};
iface.timeProvider = {apex::time::taiMicroseconds, &taiCtx};

// Mission Elapsed Time (requires epoch)
static apex::time::MetContext metCtx{bootTimeMicros};
iface.timeProvider = {apex::time::metMicroseconds, &metCtx};
```

### Clock Contexts

| Context      | Fields                      | Purpose                         |
| ------------ | --------------------------- | ------------------------------- |
| `TaiContext` | `taiUtcOffset` (default 37) | TAI-UTC leap second offset      |
| `GpsContext` | `taiUtcOffset` (default 37) | Same offset, GPS = TAI - 19s    |
| `MetContext` | `epochMicroseconds`         | Monotonic time at mission start |

### Conversions

```cpp
// Unit conversions
uint64_t us = secondsToMicroseconds(5.0);      // 5000000
uint32_t cy = secondsToCycles(5.0, 100);        // 500 (at 100 Hz)
uint64_t us = cyclesToMicroseconds(100, 100);   // 1000000

// Standard conversions
uint64_t tai = utcToTai(utcMicros);             // UTC + 37s
uint64_t gps = taiToGps(taiMicros);             // TAI - 19s - GPS epoch
uint64_t gps = utcToGps(utcMicros);             // Convenience
```

All conversions are O(1) noexcept.

### Constants

| Constant                 | Value     | Description                                        |
| ------------------------ | --------- | -------------------------------------------------- |
| `TAI_UTC_OFFSET_SECONDS` | 37        | Current TAI-UTC offset (update on new leap second) |
| `GPS_EPOCH_UNIX_SECONDS` | 315964800 | 1980-01-06T00:00:00 UTC                            |
| `GPS_TAI_OFFSET_SECONDS` | 19        | Fixed GPS-TAI offset                               |

---

## Timestamp

```cpp
struct Timestamp {
  uint64_t microseconds{0};
  TimeStandard standard{TimeStandard::CYCLE};
};
```

Pairs a value with its standard to prevent accidental cross-standard comparison.
Comparison operators only return true when standards match.

---

## Requirements

| Requirement  | Details                                                              |
| ------------ | -------------------------------------------------------------------- |
| C++ Standard | C++17                                                                |
| Dependencies | `utilities_concurrency` (Delegate)                                   |
| OS           | POSIX (clock_gettime)                                                |
| Leap seconds | Update `TAI_UTC_OFFSET_SECONDS` when IERS announces new leap seconds |

---

## Testing

| Directory | Type       | Tests | Runs with `make test` |
| --------- | ---------- | ----- | --------------------- |
| `utst/`   | Unit tests | 26    | Yes                   |

| Target              | Tests | Description                             |
| ------------------- | ----- | --------------------------------------- |
| `TestUtilitiesTime` | 26    | Enums, clocks, conversions, round-trips |

### Performance

Clock read and conversion throughput on x86-64 (15 repeats):

| Operation             | Median (ns) | CV%   | Throughput    |
| --------------------- | ----------- | ----- | ------------- |
| MONOTONIC read        | 21          | 2.5%  | 48.8M reads/s |
| UTC read              | 21          | 33.9% | 47.6M reads/s |
| UTC->TAI conversion   | 7           | 13.0% | 137M ops/s    |
| TAI->GPS conversion   | 10          | 14.4% | 103M ops/s    |
| secondsToMicroseconds | 8           | 5.4%  | 127M ops/s    |

Clock reads use vDSO-accelerated `clock_gettime`. Conversions are
single-instruction integer arithmetic. High CV% on sub-21-ns operations
is expected (measurement noise dominates at this scale).

---

## See Also

- [concurrency](../concurrency/) - Delegate used for TimeProviderDelegate
- [data](../../system/core/infrastructure/data/) - ActionInterface uses TimeProviderDelegate for ATS timing
- [action](../../system/core/components/action/) - ActionComponent wires time provider for sequencing
