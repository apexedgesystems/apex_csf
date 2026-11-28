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
  L-stable for stiff systems).
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
void setStatefulStampCallbackSparse(StatefulStampCallbackSparse cb);

void setCachedLU(bool enabled) noexcept;
void setDualLU(bool enabled) noexcept;
void setSparse(bool enabled) noexcept;
void invalidateCache() noexcept;  // dense, both two-state factorizations, sparse

void setIntegrationMethod(IntegrationMethod method) noexcept;
IntegrationMethod integrationMethod() const noexcept;

// Sparse steps solved by Newton iteration
void setAlwaysReanalyze(bool enable) noexcept;
void setNewtonIterationLimit(std::size_t iterations) noexcept;                // 10
[[nodiscard]] bool setNewtonTolerances(double absoluteVolts,
                                       double relative) noexcept;             // 1e-6, 1e-3
void setRequireConvergence(bool require) noexcept;                            // off
[[nodiscard]] std::size_t newtonIterationCount() const noexcept;
[[nodiscard]] std::size_t unconvergedStepCount() const noexcept;

TransientResult run(const TransientConfig& config, bool recordHistory = false);
TransientStatus step(double dt, TransientState& state);
TransientStatus stepDual(double dt, int stateIdx, TransientState& state);
TransientStatus computeDC(TransientState& state);

[[nodiscard]] double time() const noexcept;
void reset() noexcept;  // time, companions, kept factors, Newton counters
```

`run()` divides the span into steps of `tStep` and, when the span is not a
whole number of them, one shorter step at the end. It takes the steps by
number and sets each step's end time from its number, the last to `tEnd`:
a thousand steps for 1 us by 1 ns, with no extra step for what a sum of
steps leaves over. What the span leaves within the tolerance of a whole
count is not a step: 1e-9 of a step, or the count's own rounding where
that is larger. A count that is a whole number is taken as it stands.

A Newton step that reaches its iteration limit without converging is
accepted and counted by `unconvergedStepCount()`. With
`setRequireConvergence(true)` it is refused: `ERROR_CONVERGENCE`, with time,
companions and node voltages as before the step, and `run()` ends there.
`setNewtonTolerances()` returns true and sets both tolerances, or returns
false and changes neither when one of them is negative or not finite.

The dense workspaces have room for sixteen voltage sources. A circuit with
more is given the room at its first step, and the workspaces do not grow
again. That is about the matrix and factor storage only: a dense step
allocates at every call, because it builds the system it stamps into anew.

A stamp callback can be called more than once for a step: the step that
finds the workspaces too small stamps again, a kept factorization is
stamped for as well as the step it serves, and a step that falls back is
stamped again. It stamps the same circuit each time and advances nothing
of its own per call.

### TransientConfig

**Header:** `TransientConfig.hpp`
**Purpose:** Per-run knobs.

```cpp
struct TransientConfig {
  double tStart = 0.0;
  double tEnd   = 1e-6;
  double tStep  = 1e-9;
  IntegrationMethod method = IntegrationMethod::BACKWARD_EULER;
  bool dcOpPoint = true;  // computeDC() before the first step

  std::size_t wholeSteps() const noexcept;  // steps of tStep in the span
  double      lastStep() const noexcept;    // the shorter step that ends it; 0 if none
  std::size_t stepCount() const noexcept;   // what run() takes; 0 for a span it refuses
};
```

`run()` refuses a configuration whose `stepCount()` is 0: a step or a span
that is not above zero or not a number, or 2^53 steps and more.

### TransientState / TransientResult / TransientStatus

```cpp
struct TransientState {
  double               time = 0.0;
  std::vector<double>  nodeVoltages;
  std::vector<double>  branchCurrents;
};

struct TransientResult {
  bool                        success = false;
  std::string                 errorMessage;
  std::size_t                 stepsTaken = 0;
  std::size_t                 nonlinearIters = 0;    // Newton iterations of this run
  std::size_t                 unconvergedSteps = 0;  // steps of this run that did not converge
  double                      finalTime = 0.0;
  TransientState              finalState;
  std::vector<TransientState> history;               // populated when recordHistory=true
};

enum class TransientStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_CONFIG = 1,
  ERROR_DC_FAILED = 2,
  ERROR_STEP_FAILED = 3,
  ERROR_CONVERGENCE = 4,  // a Newton step did not converge and convergence is required
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

The backward-Euler and trapezoidal companions take the steps of
`BackwardEuler` and `TrapezoidalRule` in `src/utilities/math/integration`
on the circuit's differential equation, held step for step in the unit
tests. GEAR2 takes its first step as backward Euler and Gear's two-step
formula from the second; it is held to that first step, to the recurrence
written out, and to second-order convergence on RC and RL discharges. It
does not follow that library's `BDF2` step for step, which starts from a
repeated value. A run with `dcOpPoint` starts at rest: each capacitor at its
operating-point voltage, each inductor with the current the operating point
passed through it. GEAR2 weighs its history by the lengths of the two steps,
so a last step shortened to land on `tEnd` keeps the method's order.
`cuda::stepCuda` steps with the method it is given.

### TransientCuda

**Header:** `TransientCuda.cuh`
**Purpose:** Free-function GPU step using a `MnaCudaWorkspace`.

```cpp
[[nodiscard]] bool available() noexcept;

TransientStatus stepCuda(mna::cuda::MnaCudaWorkspace& ws,
                         MnaSystem& mna,
                         CompanionSet& companions,
                         const StampCallback& stamp,  // or a StatefulStampCallback
                         double dt, IntegrationMethod method, double& time,
                         std::vector<double>& prevVoltages,
                         mna::MnaSolveWorkspace& workspace,
                         TransientState& state);
```

The system is cleared and stamped by the call, so one `MnaSystem` serves
every step. `method` is the method the companions are stamped and updated
with. A step the GPU does not take (no CUDA, fewer than 100 nets, a
workspace too small, a failed solve) returns `ERROR_STEP_FAILED` with
`time`, the companions and the state untouched.

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

### GPU Step, or the CPU Solver

`stepCuda` works on pieces the caller holds: the system, the companions,
the time and the previous voltages. A run is driven by it from start to
end, or by a `TransientSolver` with its own companions; the two are not
mixed step by step.

```cpp
mna::cuda::MnaCudaWorkspace gpuWs;
if (cuda::available() && gpuWs.prepare(netCount + sourceCount)) {
  MnaSystem mna(netCount);  // cleared and stamped by every step
  double t = 0.0;
  for (std::size_t k = 0; k < steps; ++k) {
    if (cuda::stepCuda(gpuWs, mna, companions, stamp, dt, method, t, prevVoltages,
                       workspace, state) != TransientStatus::SUCCESS) {
      break;  // Not taken: time, companions and state are as before the step
    }
  }
} else {
  solver.setIntegrationMethod(method);
  for (std::size_t k = 0; k < steps; ++k) {
    if (solver.step(dt, state) != TransientStatus::SUCCESS) {
      break;
    }
  }
}
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `TransientSolver::time`, `reset`, `invalidateCache`,
  `setIntegrationMethod`, `integrationMethod`, the Newton settings and
  counters

### NOT RT-Safe Functions

- `TransientSolver` construction (allocates workspace)
- `TransientSolver::computeDC` (builds its own system)
- `TransientSolver::step` on the dense paths, with or without kept
  factors: each call builds the system it stamps into. Room in the
  workspaces keeps the matrix and factor storage from growing and no more
- `TransientSolver::step` on the sparse paths (KLU analysis and factors
  allocate)
- `TransientSolver::stepDual`: a call that factorizes allocates, and one
  whose factors cannot serve falls back to the dense step. A call served
  by kept factors reuses the state's system and workspace (it is served
  while the state's step length, the method's conductance factor and the
  number of voltage sources are those the factors were built with); no
  test counts what such a call allocates, so it is not offered as
  allocation-free
- `setStampCallback` and the other callback setters (a `std::function` is
  moved in and may allocate)
- `CompanionSet::addCapacitor`, `addInductor` (grow internal vectors)
- `TransientSolver::run` (may allocate `history`)
- `cuda::stepCuda`, `cuda::evaluateCapacitorsCuda` (device alloc + transfers)

### Recommended Configuration

- No stepping path of this solver is allocation-free as it stands, so
  none is offered for a hard real-time loop. Kept factors
  (`setCachedLU(true)`, `setDualLU(true)`) cut the work of a step for a
  topology that does not change; they do not remove its allocations.
- A loop that must not allocate solves a prepared
  [MNA system](../mna/README.md#7-rt-safety) directly.

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
