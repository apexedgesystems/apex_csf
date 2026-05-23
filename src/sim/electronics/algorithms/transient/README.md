# Transient Module

**Namespace:** `sim::electronics::algorithms::transient`
**Platform:** Linux-only (CUDA optional)
**C++ Standard:** C++23

Time-domain circuit simulation engine. Wraps the MNA library with companion
models for reactive elements and a configurable integration scheme.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [TransientSolver](#transientsolver) - Top-level driver
   - [TransientConfig](#transientconfig) - Run knobs
   - [TransientState / TransientResult / TransientStatus](#transientstate--transientresult--transientstatus)
   - [IntegrationMethod](#integrationmethod) - Discretization choice
   - [TransientCuda](#transientcuda) - Free-function GPU step
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: RC Step Response](#8-example-rc-step-response)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                        | API                                                                    |
| ----------------------------------------------- | ---------------------------------------------------------------------- |
| How do I drive a full transient run?            | `TransientSolver::run`                                                 |
| How do I take one step at a time?               | `TransientSolver::step`                                                |
| How do I compute the DC operating point only?   | `TransientSolver::computeDC`                                           |
| How do I add a capacitor or inductor?           | `TransientSolver::companions().addCapacitor` / `addInductor`           |
| How do I stamp resistors and sources?           | `TransientSolver::setStampCallback`                                    |
| How do I keep cached LU across steps?           | `TransientSolver::setCachedLU` / `setDualLU`                           |
| How do I choose between BE, trapezoidal, GEAR2? | `TransientSolver::setIntegrationMethod` (or `TransientConfig::method`) |
| How do I step on the GPU?                       | `cuda::stepCuda`                                                       |
| How do I batch-evaluate companions on the GPU?  | `cuda::evaluateCapacitorsCuda` (see companions module)                 |

---

## 2. Quick Reference

**Header:** `src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp`

```cpp
using namespace sim::electronics::algorithms::transient;

TransientSolver solver(netCount);
solver.companions().addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-6);
solver.setStampCallback([](MnaSystem& mna, double t) {
  mna.addVoltageSource(2, 0, 5.0);
  mna.addConductance(2, 1, 1.0 / 1000.0);
});

TransientConfig config;
config.tStart = 0.0;
config.tEnd   = 1e-3;
config.tStep  = 1e-6;
config.method = IntegrationMethod::TRAPEZOIDAL;

const auto RESULT = solver.run(config, /*recordHistory=*/true);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Construct once, step many.** Setup (`addCapacitor`, `addInductor`,
  `setStampCallback`, `setCachedLU`) is performed before the run; `step`
  reuses pre-sized workspaces.
- **LU caching.** Constant-topology circuits enable `setCachedLU` to keep
  the same LU factors across steps; alternating-topology circuits enable
  `setDualLU` and call `stepDual` with a 0 / 1 state index.
- **Sparse path.** `setSparse(true)` switches to a sparse LU for circuits
  with low matrix fill.
- **Three discretizations.** Backward Euler (first-order, dissipative),
  Trapezoidal (second-order, energy-conserving), GEAR2 / BDF2 (second-order,
  L-stable for stiff systems). All share the same per-step cost.
- **Optional GPU step.** `cuda::stepCuda` runs a single step on the GPU
  when a CUDA workspace is prepared. Callers handle fallback.

---

## 4. Module Reference

### TransientSolver

**Header:** `TransientSolver.hpp`
**Purpose:** Top-level driver for time-stepping a circuit.

#### Key Types

```cpp
using StampCallback        = std::function<void(MnaSystem&, double time)>;
using StatefulStampCallback =
    std::function<void(MnaSystem&, double time, const std::vector<double>&)>;
```

#### API

```cpp
explicit TransientSolver(std::size_t netCount);

CompanionSet&       companions() noexcept;
const CompanionSet& companions() const noexcept;

void setStampCallback(StampCallback cb);
void setStatefulStampCallback(StatefulStampCallback cb);

void setCachedLU(bool enabled) noexcept;
void setDualLU(bool enabled) noexcept;
void setSparse(bool enabled) noexcept;
void invalidateCache() noexcept;

void setIntegrationMethod(IntegrationMethod method) noexcept;
IntegrationMethod integrationMethod() const noexcept;

TransientResult run(const TransientConfig& config, bool recordHistory = false);
TransientStatus step(double dt, TransientState& state) noexcept;
TransientStatus stepDual(double dt, int stateIdx, TransientState& state) noexcept;
TransientStatus computeDC(TransientState& state);

[[nodiscard]] double time() const noexcept;
void reset() noexcept;
```

### TransientConfig

**Header:** `TransientConfig.hpp`
**Purpose:** Per-run knobs.

```cpp
struct TransientConfig {
  double tStart = 0.0;
  double tEnd   = 1e-3;
  double tStep  = 1e-6;
  IntegrationMethod method = IntegrationMethod::BACKWARD_EULER;
};
```

### TransientState / TransientResult / TransientStatus

```cpp
struct TransientState {
  double               time = 0.0;
  std::vector<double>  nodeVoltages;
  std::vector<double>  branchCurrents;
};

struct TransientResult {
  TransientStatus            status = TransientStatus::ERROR_INVALID_CONFIG;
  std::vector<TransientState> history;     // populated when recordHistory=true
  std::size_t                stepCount = 0;
};

enum class TransientStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_STEP_FAILED,
  ERROR_DC_FAILED,
  ERROR_INVALID_CONFIG,
};
```

### IntegrationMethod

**Header:** `TransientConfig.hpp`

```cpp
enum class IntegrationMethod : std::uint8_t {
  BACKWARD_EULER, ///< 1st order, A-stable, dissipative
  TRAPEZOIDAL,    ///< 2nd order, A-stable, energy-conserving
  GEAR2,          ///< 2nd order BDF2, L-stable, damps high frequencies
};
```

| Method           | Order | Stability | Energy      | Use Case                       |
| ---------------- | ----- | --------- | ----------- | ------------------------------ |
| `BACKWARD_EULER` | 1st   | A-stable  | Dissipative | Digital, fast settling         |
| `TRAPEZOIDAL`    | 2nd   | A-stable  | Conserving  | LC oscillators, analog filters |
| `GEAR2`          | 2nd   | L-stable  | Damping     | Stiff systems                  |

### TransientCuda

**Header:** `TransientCuda.cuh`
**Purpose:** Free-function GPU step using a `MnaCudaWorkspace`.

```cpp
[[nodiscard]] bool available() noexcept;

TransientStatus stepCuda(mna::cuda::MnaCudaWorkspace& ws,
                         MnaSystem& mna,
                         CompanionSet& companions,
                         const StampCallback& stamp,
                         double dt, double time,
                         std::vector<double>& prevVoltages,
                         mna::MnaSolveWorkspace& workspace,
                         TransientState& state) noexcept;
```

NOT RT-safe: device allocation + host-device transfers per call.

---

## 5. Common Patterns

### One-Shot Full Run with History

```cpp
TransientConfig config;
config.tEnd  = 1e-3;
config.tStep = 1e-6;
const auto RESULT = solver.run(config, /*recordHistory=*/true);
for (const auto& STATE : RESULT.history) {
  fmt::print("t={:.6e} v[1]={:.6f}\n", STATE.time, STATE.nodeVoltages[1]);
}
```

### Cached LU for Constant Topology

```cpp
solver.setCachedLU(true);
for (double t = 0.0; t < tEnd; t += dt) {
  solver.step(dt, state);
}
```

### Dual LU for Clocked Topologies

```cpp
solver.setDualLU(true);
for (double t = 0.0; t < tEnd; t += dt) {
  const int STATE_IDX = clockHigh(t) ? 0 : 1;
  solver.stepDual(dt, STATE_IDX, state);
}
```

### Stateful Stamp (Voltage-Dependent Element)

```cpp
solver.setStatefulStampCallback(
  [](MnaSystem& mna, double /*t*/, const std::vector<double>& prevV) {
    if (prevV[5] > 2.5) {
      mna.addConductance(5, 0, 1e-3);
    } else {
      mna.addConductance(5, 3, 1e-3);
    }
  });
```

### GPU Step with CPU Fallback

```cpp
mna::cuda::MnaCudaWorkspace gpuWs;
gpuWs.prepare(netCount + 16);

for (double t = 0.0; t < tEnd; t += dt) {
  auto status = cuda::stepCuda(gpuWs, mna, companions, stamp, dt, t,
                               prevVoltages, workspace, state);
  if (status != TransientStatus::SUCCESS) {
    solver.step(dt, state);
  }
}
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `TransientSolver::step` (after pre-allocation)
- `TransientSolver::stepDual` (after pre-allocation)
- `TransientSolver::computeDC` (uses pre-allocated workspace)
- `TransientSolver::time`, `reset`, `invalidateCache`,
  `setIntegrationMethod`, `integrationMethod`
- `setStampCallback` (small `std::function` copy)

### NOT RT-Safe Functions

- `TransientSolver` construction (allocates workspace)
- `CompanionSet::addCapacitor`, `addInductor` (grow internal vectors)
- `TransientSolver::run` (may allocate `history`)
- `cuda::stepCuda`, `cuda::evaluateCapacitorsCuda` (device alloc + transfers)

### Recommended Configuration

- Build the solver, add reactive elements, and call `run` once outside the
  RT loop to warm up workspaces.
- Inside the RT loop, only call `step` or `stepDual` with a reused
  `TransientState`.
- Enable `setCachedLU(true)` for any topology that does not change.

---

## 7. CLI Tools

None.

---

## 8. Example: RC Step Response

```cpp
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics::algorithms;

  transient::TransientSolver solver(/*netCount=*/3);
  solver.companions().addCapacitor(/*pos=*/2, /*neg=*/0, /*C=*/1e-6);
  solver.setStampCallback([](mna::MnaSystem& mna, double /*t*/) {
    mna.addVoltageSource(1, 0, 5.0);
    mna.addConductance(1, 2, 1.0 / 1000.0);
  });

  transient::TransientConfig config;
  config.tStart = 0.0;
  config.tEnd   = 5e-3;
  config.tStep  = 1e-5;
  config.method = transient::IntegrationMethod::BACKWARD_EULER;

  const auto RESULT = solver.run(config, /*recordHistory=*/true);
  for (const auto& STATE : RESULT.history) {
    fmt::print("t={:.6e}  v(cap)={:.6f}\n", STATE.time, STATE.nodeVoltages[2]);
  }
  return 0;
}
```

---

## 9. See Also

- [Companions](../companions/README.md) - Reactive-element discretization
- [MNA library](../mna/README.md) - Linear system construction and solve
- [Nonlinear solver](../nonlinear/README.md) - Newton-Raphson for nonlinear devices
