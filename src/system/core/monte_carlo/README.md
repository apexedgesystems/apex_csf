# Monte Carlo

**Namespace:** `apex::monte_carlo`
**Platform:** Linux (POSIX)
**C++ Standard:** C++23

Header-only batch Monte Carlo execution infrastructure. Runs N independent simulation
runs across a thread pool for maximum throughput. No real-time constraints, no frame
boundaries, no scheduling intelligence.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Architecture](#2-architecture)
3. [Module Reference](#3-module-reference)
4. [Common Patterns](#4-common-patterns)
5. [Performance](#5-performance)
6. [Building](#6-building)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Start

```cpp
#include "src/system/core/monte_carlo/inc/MonteCarloDriver.hpp"
#include "src/system/core/monte_carlo/inc/SweepGenerator.hpp"

using namespace apex::monte_carlo;

// Define parameter and result types
struct Params { double resistance; double capacitance; };
struct Result { double tau; bool converged; };

// Generate 10,000 parameter sets with random variation
Params base{100.0, 1e-6};
auto params = generateSweep<Params>(
    base, 10000,
    [](Params& p, std::uint32_t, std::mt19937_64& rng) {
        p.resistance = std::normal_distribution<>(100.0, 5.0)(rng);
        p.capacitance = std::uniform_real_distribution<>(1e-9, 1e-6)(rng);
    },
    42);  // seed for reproducibility

// Run Monte Carlo (uses all available cores)
MonteCarloDriver<Params, Result> driver(
    [](const Params& p, std::uint32_t) -> Result {
        return {p.resistance * p.capacitance, true};
    });

auto results = driver.execute(params);

// Extract statistics on a specific output field
auto tauStats = extractAndCompute<Result>(
    results.runs, [](const Result& r) { return r.tau; });

fmt::print("tau: mean={:.3e} stddev={:.3e} p05={:.3e} p95={:.3e}\n",
           tauStats.mean, tauStats.stddev, tauStats.p05, tauStats.p95);
fmt::print("Throughput: {:.0f} runs/sec ({} threads, {:.2f}s)\n",
           results.runsPerSecond(), results.threadCount,
           results.wallTimeSeconds);
```

---

## 2. Architecture

### Not an Executive

MonteCarloDriver is NOT an executive variant. It does not implement `IExecutive`
and does not inherit from any executive class. It is a batch orchestrator that
_uses_ McuExecutive instances (or raw model calls) as workers.

| Concern     | ApexExecutive             | McuExecutive  | MonteCarloDriver  |
| ----------- | ------------------------- | ------------- | ----------------- |
| Threading   | Multi-thread pools        | Single-thread | Thread-per-worker |
| Timing      | RT clock sync             | ITickSource   | None (max speed)  |
| Parallelism | Within frame              | None          | Across runs       |
| Scheduling  | Priority, sequence groups | Rate-based    | Atomic counter    |
| Use case    | RT embedded               | MCU           | Parameter sweeps  |

### Work Distribution

Workers pull from an atomic counter. No locks, no condition variables, no
work queue contention:

```
Thread 0: run[0] -> run[4] -> run[8]  -> ...
Thread 1: run[1] -> run[5] -> run[9]  -> ...
Thread 2: run[2] -> run[6] -> run[10] -> ...
Thread 3: run[3] -> run[7] -> run[11] -> ...
```

Each thread writes to `results[runIndex]` (pre-allocated, no contention).

---

## 3. Module Reference

### MonteCarloDriver

**RT-safe:** No (allocates, spawns threads, blocks)

Batch executor. Takes a run function and configuration, executes all parameter
sets across a thread pool, returns aggregate results.

| Method                       | Description                                        |
| ---------------------------- | -------------------------------------------------- |
| `execute(params)`            | Run all parameter sets, return `MonteCarloResults` |
| `executeSingle(params, idx)` | Run one parameter set (for debugging)              |
| `config()`                   | Get driver configuration                           |

### MonteCarloResults

**RT-safe:** No (contains std::vector)

Container for per-run results and execution metadata.

| Field             | Description                            |
| ----------------- | -------------------------------------- |
| `runs`            | Per-run results (indexed by run index) |
| `totalRuns`       | Total runs requested                   |
| `completedRuns`   | Runs that succeeded                    |
| `failedRuns`      | Runs that threw exceptions             |
| `wallTimeSeconds` | Execution wall-clock time              |
| `threadCount`     | Worker threads used                    |
| `runsPerSecond()` | Computed throughput                    |

### ScalarStats

**RT-safe:** No (computed via sort)

Descriptive statistics for a scalar output field.

| Field                      | Description                 |
| -------------------------- | --------------------------- |
| `mean`, `stddev`           | Central tendency and spread |
| `min`, `max`               | Range                       |
| `median`                   | 50th percentile             |
| `p05`, `p25`, `p75`, `p95` | Percentile distribution     |
| `count`                    | Number of samples           |

### SweepGenerator

**RT-safe:** No (allocates vectors)

Parameter variation utilities for generating sweep inputs.

| Function                                                 | Description                          |
| -------------------------------------------------------- | ------------------------------------ |
| `generateSweep(base, count, mutator, seed)`              | Apply mutator to base config N times |
| `gridSweep(min, max, steps)`                             | Uniform grid of values               |
| `uniformSweep(min, max, count, seed)`                    | Random uniform values                |
| `latinHypercubeSweep(min, max, count, seed)`             | Latin Hypercube Sampling             |
| `cartesianProduct(base, sweepA, sweepB, applyA, applyB)` | 2D grid of all combinations          |

---

## 4. Common Patterns

### With McuExecutive

Run a full simulation per MC iteration:

```cpp
MonteCarloDriver<Params, Result> driver(
    [](const Params& p, std::uint32_t) -> Result {
        // Each worker gets its own executive + model
        FreeRunningSource tick(100);
        McuExecutive<> exec(&tick, 100, 500);  // 500 cycles

        MyModel model(p);
        exec.addTask({TaskBuilder::bindMember<MyModel, &MyModel::step>(model),
                       &model, 1, 1, 0, 0, 1});
        exec.init();
        exec.setFastForward(true);
        exec.run();

        return model.extractResult();
    });
```

### Grid Sweep (2D)

Systematic exploration of two parameters:

```cpp
auto resistances = gridSweep(50.0, 200.0, 20);
auto capacitances = gridSweep(1e-9, 1e-6, 20);

auto params = cartesianProduct<Params>(
    base, resistances, capacitances,
    [](Params& p, double v) { p.resistance = v; },
    [](Params& p, double v) { p.capacitance = v; });
// 400 parameter sets
```

### Statistical Analysis

Extract multiple fields:

```cpp
auto voltStats = extractAndCompute<Result>(
    results.runs, [](const Result& r) { return r.peakVoltage; });
auto timeStats = extractAndCompute<Result>(
    results.runs, [](const Result& r) { return r.settlingTime; });

// Check for outliers
fmt::print("Voltage 95th percentile: {:.3f}\n", voltStats.p95);
```

---

## 5. Performance

Throughput scales linearly with core count for CPU-bound simulations.
The atomic work counter adds negligible overhead (single CAS per run).

Key factors:

- **Run duration** dominates. Short runs (~1us) may see thread overhead.
- **Thread count** defaults to `hardware_concurrency()`.
- **False sharing** avoided: each thread writes to its own result slot.
- **GPU workloads** are orthogonal: batch inside the run function.

---

## 6. Building

```cmake
# Header-only dependency
target_link_libraries(my_target PRIVATE system_core_monte_carlo)
```

---

## 7. Testing

```bash
# Run unit tests
make compose-testp
# Tests are labeled: system, monte_carlo
```

---

## 8. See Also

- `src/system/core/executive/mcu/` - McuExecutive (fast simulation worker)
- `src/system/core/executive/mcu/inc/FreeRunningSource.hpp` - Max-speed tick source
- `src/utilities/concurrency/` - Thread pool, lock-free queues
