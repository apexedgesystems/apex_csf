# Nonlinear Module

**Namespace:** `sim::electronics::algorithms::nonlinear`
**Platform:** Linux-only (CUDA optional)
**C++ Standard:** C++23

Newton-Raphson DC operating-point solver for circuits with nonlinear devices.
Iteratively linearizes each device around the current voltage estimate and
solves the resulting linear system with the MNA library.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [NonlinearDevice](#nonlineardevice) - Device interface
   - [NonlinearDeviceSet](#nonlineardeviceset) - Device collection
   - [NewtonRaphsonSolver](#newtonraphsonsolver) - Iterative solver
   - [NonlinearConfig](#nonlinearconfig) - Solver knobs
   - [NonlinearStatus](#nonlinearstatus) - Result codes
   - [NonlinearDeviceCuda](#nonlineardevicecuda) - GPU batch evaluation
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Diode Rectifier DC Point](#8-example-diode-rectifier-dc-point)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                                              | API                                           |
| --------------------------------------------------------------------- | --------------------------------------------- |
| How do I solve a circuit's DC operating point with nonlinear devices? | `NewtonRaphsonSolver::solve`                  |
| How do I plug in a new device model?                                  | Implement `NonlinearDevice`                   |
| How do I add many devices at once?                                    | `NonlinearDeviceSet::addDevice`               |
| How do I stamp the linear part of the circuit?                        | `NewtonRaphsonSolver::setLinearStampCallback` |
| How do I tune convergence?                                            | `NonlinearConfig`                             |
| How do I read the iteration result?                                   | `NonlinearResult`                             |
| What does a non-success status mean?                                  | `NonlinearStatus`                             |
| How do I batch-evaluate I(V) on a GPU?                                | `cuda::evaluateDevicesCuda`                   |

---

## 2. Quick Reference

**Header:** `src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp`

```cpp
using namespace sim::electronics::algorithms::nonlinear;

NewtonRaphsonSolver solver(/*netCount=*/3);
solver.devices().addDevice(std::make_shared<DiodeModel>(/*anode=*/2, /*cathode=*/0));
solver.setLinearStampCallback([](MnaSystem& mna) {
  mna.addVoltageSource(1, 0, 5.0);
  mna.addConductance(1, 2, 1.0 / 1000.0);
});

NonlinearConfig config;
config.maxIterations = 20;
NonlinearResult result = solver.solve(config);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Device abstraction.** Any model that exposes `current(V)` and
  `conductance(V)` plugs into the solver without recompilation of the core.
- **Linearization is local.** Each iteration replaces every nonlinear device
  with its Norton equivalent (`g(V0), I(V0) - g(V0) * V0`), then asks the MNA
  library to solve the linear system.
- **Damping is opt-in but on by default.** Halving the first five Newton
  steps prevents oscillation when the initial guess is far from the answer.
- **Three convergence criteria.** Any of absolute voltage, absolute current
  residual, or relative voltage change terminates the loop, which catches
  both well-scaled and badly-scaled circuits.
- **Divergence is detectable.** Voltages above 1e6 V short-circuit the
  iteration with `ERROR_VOLTAGE_DIVERGENCE` rather than running to
  `maxIterations`.

---

## 4. Module Reference

### NonlinearDevice

**Header:** `NonlinearDevice.hpp`
**Purpose:** Polymorphic interface every nonlinear device implements.

#### API

```cpp
class NonlinearDevice {
public:
  [[nodiscard]] virtual NetID posNet() const noexcept = 0;
  [[nodiscard]] virtual NetID negNet() const noexcept = 0;

  [[nodiscard]] virtual double current(double vTerminal) const noexcept = 0;
  [[nodiscard]] virtual double conductance(double vTerminal) const noexcept = 0;

  void stampLinearized(MnaSystem& mna, double vTerminal) const;
};
```

The Norton equivalent stamped by `stampLinearized` is:

```
g_eq = g(V0)
i_eq = I(V0) - g(V0) * V0
```

### NonlinearDeviceSet

**Header:** `NonlinearDevice.hpp`
**Purpose:** Owns the list of devices for a circuit and stamps them in bulk.

#### API

```cpp
void addDevice(std::shared_ptr<NonlinearDevice> device);
void clear();

void stampAllLinearized(MnaSystem& mna,
                        const std::vector<double>& nodeVoltages) const;

[[nodiscard]] const std::vector<std::shared_ptr<NonlinearDevice>>& devices() const;
```

### NewtonRaphsonSolver

**Header:** `NewtonRaphson.hpp`
**Purpose:** Newton-Raphson iteration driver.

#### Key Types

```cpp
struct NonlinearResult {
  std::vector<double> nodeVoltages;
  NonlinearStatus status = NonlinearStatus::ERROR_INVALID_CONFIG;
  std::size_t iterations = 0;
  double finalError = 0.0;
  std::string errorMessage;
  [[nodiscard]] bool success() const noexcept;
};
```

#### API

```cpp
explicit NewtonRaphsonSolver(std::size_t netCount);

NonlinearDeviceSet& devices() noexcept;

void setLinearStampCallback(std::function<void(MnaSystem&)> cb);
void setInitialGuess(std::vector<double> v) noexcept;

[[nodiscard]] NonlinearResult solve(const NonlinearConfig& config);
void reset() noexcept;
```

`solve` is NOT RT-safe on its first call (constructs an MNA system).
Subsequent calls reuse the same workspace.

### NonlinearConfig

**Header:** `NonlinearConfig.hpp`
**Purpose:** Convergence and damping knobs.

```cpp
struct NonlinearConfig {
  std::size_t maxIterations    = 20;
  double      voltageTolerance = 1e-6;  // Volts
  double      currentTolerance = 1e-9;  // Amps
  double      relativeTolerance = 1e-3;

  bool        enableDamping    = true;
  double      dampingFactor    = 0.5;
  std::size_t dampingIterations = 5;
};
```

| Knob               | Tight        | Relaxed      |
| ------------------ | ------------ | ------------ |
| `voltageTolerance` | `1e-9` V     | `1e-3` V     |
| `currentTolerance` | `1e-12` A    | `1e-6` A     |
| `maxIterations`    | 50           | 10           |
| `dampingFactor`    | 0.3 (stable) | 0.7 (faster) |

### NonlinearStatus

**Header:** `NonlinearConfig.hpp`
**Purpose:** Result code for `NonlinearResult`.

```cpp
enum class NonlinearStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_MAX_ITERATIONS,
  ERROR_SINGULAR_MATRIX,
  ERROR_VOLTAGE_DIVERGENCE,
  ERROR_INVALID_CONFIG,
};
```

| Status                     | Cause                                    | Mitigation                                                 |
| -------------------------- | ---------------------------------------- | ---------------------------------------------------------- |
| `ERROR_MAX_ITERATIONS`     | Poor initial guess, tolerances too tight | Increase `maxIterations`, relax tolerances, enable damping |
| `ERROR_SINGULAR_MATRIX`    | Floating net or all-zero conductance     | Add ground connection, check device models                 |
| `ERROR_VOLTAGE_DIVERGENCE` | Negative-resistance or unstable model    | Check device polarity, reduce supply magnitudes            |
| `ERROR_INVALID_CONFIG`     | `maxIterations == 0`                     | Set `maxIterations > 0`                                    |

### NonlinearDeviceCuda

**Header:** `NonlinearDeviceCuda.cuh`
**Purpose:** GPU batch evaluation of `I(V)` and `g(V)` plus parallel
Norton-equivalent stamping.

#### API

```cpp
void evaluateDevicesCuda(const DeviceParams* d_deviceParams,
                         const double* d_nodeVoltages,
                         double* d_currents,
                         double* d_conductances,
                         int deviceCount);

void stampDevicesCuda(const DeviceParams* d_deviceParams,
                      const double* d_currents,
                      const double* d_conductances,
                      const double* d_nodeVoltages,
                      double* d_G_matrix,
                      double* d_I_vector,
                      int netCount,
                      int deviceCount);
```

Both functions operate on device pointers and require the caller to manage
host-device transfers. NOT RT-safe.

---

## 5. Common Patterns

### One-Shot DC Point

```cpp
NewtonRaphsonSolver solver(netCount);
solver.devices().addDevice(diode);
solver.setLinearStampCallback(stampLinear);
const NonlinearResult RESULT = solver.solve(config);
if (RESULT.success()) {
  for (double v : RESULT.nodeVoltages) { /* ... */ }
}
```

### Warm-Started Re-Solve

```cpp
const NonlinearResult INITIAL = solver.solve(config);
solver.setInitialGuess(INITIAL.nodeVoltages);
solver.devices().addDevice(secondDiode);
const NonlinearResult REFINED = solver.solve(config);
```

### Coupling with Transient Integration

```cpp
TransientSolver transient(netCount);
NewtonRaphsonSolver nonlinear(netCount);
transient.companions().addCapacitor(/* ... */);
nonlinear.devices().addDevice(/* ... */);

for (double t = 0.0; t < tEnd; t += dt) {
  nonlinear.setLinearStampCallback([&](MnaSystem& mna) {
    transient.companions().stampAll(mna, dt, method);
    stampResistors(mna);
  });
  const auto RESULT = nonlinear.solve(config);
  transient.companions().updateAll(RESULT.nodeVoltages, dt);
}
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `NonlinearDevice::current`, `conductance` (pure arithmetic)
- `NonlinearDeviceSet::stampAllLinearized`
- `NewtonRaphsonSolver::solve` after the first call (workspace cached)

### NOT RT-Safe Functions

- `NewtonRaphsonSolver` construction and first `solve` (allocates MNA workspace)
- `NonlinearDeviceSet::addDevice` (grows the device vector)
- `cuda::evaluateDevicesCuda`, `cuda::stampDevicesCuda` (device alloc + copy)

### Recommended Configuration

- Run a warmup `solve` outside the RT loop to allocate the MNA workspace.
- Pre-size the device set; do not add or remove devices during the loop.
- For stiff devices (tunnel diodes, SCRs), enable damping and raise
  `maxIterations`.

---

## 7. CLI Tools

None.

---

## 8. Example: Diode Rectifier DC Point

```cpp
#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <fmt/core.h>
#include <memory>

using namespace sim::electronics::algorithms;

class DiodeModel : public nonlinear::NonlinearDevice {
public:
  DiodeModel(mna::NetID anode, mna::NetID cathode)
      : anode_(anode), cathode_(cathode) {}

  [[nodiscard]] mna::NetID posNet() const noexcept override { return anode_; }
  [[nodiscard]] mna::NetID negNet() const noexcept override { return cathode_; }

  [[nodiscard]] double current(double v) const noexcept override {
    const double EXP_ARG = std::min(v / VT_, 40.0);
    return IS_ * (std::exp(EXP_ARG) - 1.0);
  }
  [[nodiscard]] double conductance(double v) const noexcept override {
    const double EXP_ARG = std::min(v / VT_, 40.0);
    return (IS_ / VT_) * std::exp(EXP_ARG);
  }

private:
  mna::NetID anode_, cathode_;
  static constexpr double IS_ = 1e-12;
  static constexpr double VT_ = 0.026;
};

int main() {
  nonlinear::NewtonRaphsonSolver solver(/*netCount=*/3);
  solver.devices().addDevice(std::make_shared<DiodeModel>(/*anode=*/2, /*cathode=*/0));

  solver.setLinearStampCallback([](mna::MnaSystem& m) {
    m.addVoltageSource(1, 0, 5.0);
    m.addConductance(1, 2, 1.0 / 1000.0);
  });

  nonlinear::NonlinearConfig config;
  config.maxIterations = 20;

  const nonlinear::NonlinearResult RESULT = solver.solve(config);
  if (RESULT.success()) {
    fmt::print("converged in {} iterations\n", RESULT.iterations);
    fmt::print("v(diode anode) = {:.6f} V\n", RESULT.nodeVoltages[2]);
  }
  return 0;
}
```

---

## 9. See Also

- [MNA library](../mna/README.md) - Linear system construction and solve
- [Transient solver](../transient/README.md) - Time-stepping with reactive elements
- [Nonlinear device models](../../devices/nonlinear/README.md) - Concrete device implementations
