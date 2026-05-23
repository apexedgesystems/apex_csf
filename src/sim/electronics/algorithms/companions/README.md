# Companions Module

**Namespace:** `sim::electronics::algorithms::companions`
**Platform:** Linux-only
**C++ Standard:** C++23

Numerical-integration companion models for reactive elements (capacitors,
inductors) used by the transient solver.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [CapacitorCompanion](#capacitorcompanion) - Discretized capacitor
   - [InductorCompanion](#inductorcompanion) - Discretized inductor
   - [CompanionSet](#companionset) - Bulk container + stamp/update
   - [CompanionSetCuda](#companionsetcuda) - GPU batch evaluation
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Transient RC Step](#8-example-transient-rc-step)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                                   | Module                                                     |
| ---------------------------------------------------------- | ---------------------------------------------------------- |
| How do I stamp a capacitor for transient simulation?       | `CapacitorCompanion`                                       |
| How do I stamp an inductor for transient simulation?       | `InductorCompanion`                                        |
| How do I manage all reactive elements of a circuit?        | `CompanionSet`                                             |
| How do I split conductance vs. RHS stamping for cached LU? | `CompanionSet::stampConductanceAll` / `stampCurrentAll`    |
| How do I advance state after the solver returns?           | `CompanionSet::updateAll`                                  |
| How do I seed initial state from a DC operating point?     | `CompanionSet::initializeFromDC`                           |
| How do I batch-evaluate Geq + Ieq on a GPU?                | `cuda::evaluateCapacitorsCuda` / `evaluateInductorsCuda`   |
| Which integration methods are supported?                   | `IntegrationMethod` (Backward Euler / Trapezoidal / GEAR2) |

---

## 2. Quick Reference

**Header:** `src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp`

```cpp
using namespace sim::electronics::algorithms::companions;
using IntegrationMethod = sim::electronics::algorithms::transient::IntegrationMethod;

CompanionSet companions;
companions.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-9);
companions.addInductor(/*pos=*/2, /*neg=*/0, /*L=*/10e-6);

companions.stampAll(mna, dt, IntegrationMethod::BACKWARD_EULER);
// ... solve mna ...
companions.updateAll(mna.solution(), dt);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Value semantics.** `CapacitorCompanion` and `InductorCompanion` are POD-like
  structs; `geq()` / `ieq()` are pure functions of the struct fields and the
  time step. Safe to copy, batch, or hand to a GPU kernel.
- **Separable conductance / RHS.** `stampConductanceAll` and `stampCurrentAll`
  let callers reuse a factorized MNA matrix and only update the RHS each step.
- **No fallbacks.** Inductor steady-state current cannot be derived from node
  voltages alone, so `initializeFromDC` leaves inductor current at zero
  rather than guessing.

---

## 4. Module Reference

### CapacitorCompanion

**Header:** `CompanionModels.hpp`
**Purpose:** Companion model for a capacitor (`I = C * dV/dt`).

#### Key Types

```cpp
struct CapacitorCompanion {
  NetID posNet;          ///< Positive terminal net
  NetID negNet;          ///< Negative terminal net
  double capacitance;    ///< Farads
  double prevVoltage;    ///< V(t - dt)
  double prev2Voltage;   ///< V(t - 2*dt), used by GEAR2
  double current;        ///< Current through the device, for output
};
```

#### API

```cpp
[[nodiscard]] double geq(double dt, IntegrationMethod method) const noexcept;
[[nodiscard]] double ieq(double dt, IntegrationMethod method) const noexcept;

void stamp(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stamp(MnaSystemSparse& mna, double dt, IntegrationMethod method) const;

void update(double newVoltage, double dt) noexcept;
```

`geq` and `ieq` are RT-safe (constant time, no allocation). `stamp` is RT-safe
once the MNA system is sized.

#### Usage

```cpp
CapacitorCompanion cap{.posNet = 1, .negNet = 0, .capacitance = 1e-9};
cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);

const double V_CAP = solution[1] - solution[0];
cap.update(V_CAP, dt);
```

### InductorCompanion

**Header:** `CompanionModels.hpp`
**Purpose:** Companion model for an inductor (`V = L * dI/dt`).

#### Key Types

```cpp
struct InductorCompanion {
  NetID posNet;          ///< Positive terminal net
  NetID negNet;          ///< Negative terminal net
  double inductance;     ///< Henries
  double prevCurrent;    ///< I(t - dt)
  double prev2Current;   ///< I(t - 2*dt), used by GEAR2
  double voltage;        ///< Voltage across the device, for output
};
```

#### API

```cpp
[[nodiscard]] double geq(double dt, IntegrationMethod method) const noexcept;
[[nodiscard]] double ieq(double dt, IntegrationMethod method) const noexcept;

void stamp(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stamp(MnaSystemSparse& mna, double dt, IntegrationMethod method) const;

void update(double newVoltage, double dt) noexcept;
```

#### Usage

```cpp
InductorCompanion ind{.posNet = 1, .negNet = 0, .inductance = 10e-6};
ind.stamp(mna, dt, IntegrationMethod::TRAPEZOIDAL);

const double V_IND = solution[1] - solution[0];
ind.update(V_IND, dt);
```

### CompanionSet

**Header:** `CompanionModels.hpp`
**Purpose:** Bulk container that owns every reactive element in a circuit and
applies stamp / update / reset in one call.

#### API

```cpp
std::size_t addCapacitor(NetID posNet, NetID negNet, double capacitance);
std::size_t addInductor(NetID posNet, NetID negNet, double inductance);

void stampAll(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stampConductanceAll(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stampCurrentAll(MnaSystem& mna, double dt, IntegrationMethod method) const;

void updateAll(const std::vector<double>& nodeVoltages, double dt);
void reset() noexcept;
void initializeFromDC(const std::vector<double>& nodeVoltages);

[[nodiscard]] std::size_t capacitorCount() const noexcept;
[[nodiscard]] std::size_t inductorCount() const noexcept;
```

`addCapacitor` / `addInductor` are NOT RT-safe (they grow internal vectors).
`stampAll`, `stampConductanceAll`, `stampCurrentAll`, `updateAll`, and `reset`
are RT-safe once the set is populated.

#### Usage

```cpp
CompanionSet companions;
companions.addCapacitor(1, 0, 100e-12);
companions.addInductor(2, 0, 10e-6);

companions.stampAll(mna, dt, IntegrationMethod::BACKWARD_EULER);
// solve ...
companions.updateAll(solution, dt);
```

### CompanionSetCuda

**Header:** `CompanionSetCuda.hpp`
**Purpose:** GPU batch evaluation of Geq / Ieq for many capacitors or
inductors in parallel.

#### API

```cpp
void evaluateCapacitorsCuda(const CapacitorCompanion* capacitors, int n, double dt,
                            IntegrationMethod method,
                            double* geqOut, double* ieqOut);

void evaluateInductorsCuda(const InductorCompanion* inductors, int n, double dt,
                           IntegrationMethod method,
                           double* geqOut, double* ieqOut);
```

Both functions copy host arrays to the device, launch one CUDA thread per
element, and copy results back. NOT RT-safe (device allocation + transfer).

#### Usage

```cpp
const std::size_t N = companions.capacitorCount();
std::vector<double> geq(N), ieq(N);
cuda::evaluateCapacitorsCuda(companions.capacitors().data(), N, dt,
                             IntegrationMethod::TRAPEZOIDAL,
                             geq.data(), ieq.data());
```

---

## 5. Common Patterns

### One-Shot Stamp + Solve

```cpp
companions.stampAll(mna, dt, method);
mna.solve(solution);
companions.updateAll(solution, dt);
```

### Cached LU (factorize once, RHS each step)

```cpp
mna.clear();
companions.stampConductanceAll(mna, dt, method);
auto factor = mna.factorize();

for (double t = 0.0; t < tEnd; t += dt) {
  factor.rhs().setZero();
  companions.stampCurrentAll(factor.rhs(), dt, method);
  factor.solve(solution);
  companions.updateAll(solution, dt);
}
```

### Resetting State

```cpp
companions.reset();              // history -> 0
companions.initializeFromDC(dc); // seed from DC operating point
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `CapacitorCompanion::geq`, `ieq`, `update`
- `InductorCompanion::geq`, `ieq`, `update`
- `CompanionSet::stampAll`, `stampConductanceAll`, `stampCurrentAll`,
  `updateAll`, `reset`, `initializeFromDC`

### NOT RT-Safe Functions

- `CompanionSet::addCapacitor`, `addInductor` (grow internal vectors)
- `cuda::evaluateCapacitorsCuda`, `evaluateInductorsCuda` (device alloc + copy)

### Recommended Configuration

- Build the `CompanionSet` outside the RT loop, then use the stamp / update
  methods inside it.
- Pick `BACKWARD_EULER` for digital circuits (stable, dissipative),
  `TRAPEZOIDAL` for analog oscillators / filters (energy-conserving),
  `GEAR2` for stiff systems.

---

## 7. CLI Tools

None. Companion models are a library module with no standalone CLI.

---

## 8. Example: Transient RC Step

```cpp
#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <fmt/core.h>
#include <vector>

int main() {
  using namespace sim::electronics::algorithms;
  using transient::IntegrationMethod;

  const std::size_t NET_COUNT = 2;
  mna::MnaSystem mna(NET_COUNT);

  companions::CompanionSet companions;
  companions.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-9);

  const double R = 1e3;
  const double DT = 1e-9;
  const double T_END = 1e-6;
  const double V_IN = 5.0;

  std::vector<double> solution(NET_COUNT);
  for (double t = 0.0; t < T_END; t += DT) {
    mna.clear();
    mna.addConductance(0, 1, 1.0 / R);
    mna.addCurrent(1, 0, V_IN / R);
    companions.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);
    mna.solve(solution);
    companions.updateAll(solution, DT);
    fmt::print("t={:.3e}  vCap={:.6f}\n", t, solution[1]);
  }
  return 0;
}
```

---

## 9. See Also

- [Linear devices](../../devices/linear/README.md) - Resistor / capacitor / inductor primitives
- [Transient solver](../transient/README.md) - Integration methods and time stepping
- [MNA library](../mna/README.md) - Circuit matrix assembly and solve
