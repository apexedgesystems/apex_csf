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
9. [Migration](#9-migration)
10. [See Also](#10-see-also)

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
const auto RESULT = mna.solve();
companions.updateAll(RESULT.nodeVoltages, dt, IntegrationMethod::BACKWARD_EULER);
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
- **One definition per method.** `update` takes an element's new current
  from the `geq` and `ieq` that were stamped, so the history a method reads
  on the next step is the one that method produced. The CUDA evaluators call
  the same `geq` and `ieq`.
- **Steps of any length.** GEAR2 weighs its two steps of history by their
  lengths (`gear2Weights`), so a step of another length, such as a last step
  shortened to land on the end time, keeps the method's order.
- **Starting.** An element as constructed, after `reset()`, after
  `setInitialVoltage()` / `setInitialCurrent()`, or after
  `initializeFromDC()` has one value behind it (the capacitor's voltage or
  the inductor's current), the other quantity zero and no step taken. One
  value supports a first-order formula: GEAR2 takes its first step as
  backward Euler (`gear2Weights(dt, 0)` is 1, 1, 0) and is Gear's formula
  from the second step on, which keeps the run second order whatever the
  slope it starts with. An inductor's
  resting current is not in the node voltages; `initializeFromDC` takes it
  as what the operating-point solve passed through the conductance it stood
  in for the inductor with.

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
  double prevVoltage;    ///< Voltage at the end of the last step
  double prev2Voltage;   ///< Voltage at the end of the step before it (GEAR2)
  double current;        ///< Current at the end of the last step (TRAPEZOIDAL, output)
  double prevStep;       ///< Length of the last step; 0 before any (GEAR2)
};
```

#### API

```cpp
[[nodiscard]] double geq(double dt, IntegrationMethod method) const noexcept;
[[nodiscard]] double ieq(double dt, IntegrationMethod method) const noexcept;

void stamp(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stamp(MnaSystemSparse& mna, double dt, IntegrationMethod method) const;

void update(double newVoltage, double dt, IntegrationMethod method) noexcept;
void setInitialVoltage(double volts) noexcept;
```

`geq` and `ieq` are RT-safe (constant time, no allocation). `stamp` is RT-safe
once the MNA system is sized. `update` takes the method the step was stamped
with.

#### Usage

```cpp
CapacitorCompanion cap{.posNet = 1, .negNet = 0, .capacitance = 1e-9};
cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);

const double V_CAP = solution[1] - solution[0];
cap.update(V_CAP, dt, IntegrationMethod::BACKWARD_EULER);
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
  double prevCurrent;    ///< Current at the end of the last step
  double prev2Current;   ///< Current at the end of the step before it (GEAR2)
  double voltage;        ///< Voltage at the end of the last step (TRAPEZOIDAL, output)
  double prevStep;       ///< Length of the last step; 0 before any (GEAR2)
};
```

#### API

```cpp
[[nodiscard]] double geq(double dt, IntegrationMethod method) const noexcept;
[[nodiscard]] double ieq(double dt, IntegrationMethod method) const noexcept;

void stamp(MnaSystem& mna, double dt, IntegrationMethod method) const;
void stamp(MnaSystemSparse& mna, double dt, IntegrationMethod method) const;

void update(double newVoltage, double dt, IntegrationMethod method) noexcept;
void setInitialCurrent(double amperes) noexcept;
```

The trapezoidal rule reads the inductor's last voltage: an inductor that
starts with a voltage across it (a tank with a charged capacitor) has
`voltage` set to it.

#### Usage

```cpp
InductorCompanion ind{.posNet = 1, .negNet = 0, .inductance = 10e-6};
ind.stamp(mna, dt, IntegrationMethod::TRAPEZOIDAL);

const double V_IND = solution[1] - solution[0];
ind.update(V_IND, dt, IntegrationMethod::TRAPEZOIDAL);
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

void updateAll(const std::vector<double>& nodeVoltages, double dt, IntegrationMethod method);
void reset() noexcept;
void initializeFromDC(const std::vector<double>& nodeVoltages, double inductorSiemens = 0.0);

[[nodiscard]] double conductanceFactor(double dt, IntegrationMethod method) const noexcept;

[[nodiscard]] std::size_t capacitorCount() const noexcept;
[[nodiscard]] std::size_t inductorCount() const noexcept;
```

`addCapacitor` / `addInductor` are NOT RT-safe (they grow internal vectors).
`stampAll`, `stampConductanceAll`, `stampCurrentAll`, `updateAll`, and `reset`
are RT-safe once the set is populated.

`conductanceFactor` is the factor the method puts on C/dt for a step taken
now: 1, 2, and for GEAR2 3/2 unless the step before had another length.
Steps of equal length, method and factor stamp equal conductances, which is
what a caller that keeps factors of the matrix compares.

#### Usage

```cpp
CompanionSet companions;
companions.addCapacitor(1, 0, 100e-12);
companions.addInductor(2, 0, 10e-6);

companions.stampAll(mna, dt, IntegrationMethod::BACKWARD_EULER);
// solve ...
companions.updateAll(solution, dt, IntegrationMethod::BACKWARD_EULER);
```

### CompanionSetCuda

**Header:** `CompanionSetCuda.hpp`
**Purpose:** GPU batch evaluation of Geq / Ieq for many capacitors or
inductors in parallel.

#### API

```cpp
[[nodiscard]] bool evaluateCapacitorsCuda(const CapacitorCompanion* capacitors, int n, double dt,
                                          IntegrationMethod method,
                                          double* geqOut, double* ieqOut);

[[nodiscard]] bool evaluateInductorsCuda(const InductorCompanion* inductors, int n, double dt,
                                         IntegrationMethod method,
                                         double* geqOut, double* ieqOut);
```

Both functions copy host arrays to the device, launch one CUDA thread per
element, and copy results back. NOT RT-safe (device allocation + transfer).
They return false, and the outputs are not to be read, when there is no
device, a CUDA call fails, a pointer is null or `n` is negative. The values
are those `geq()` and `ieq()` return on the host.

#### Usage

```cpp
std::vector<CapacitorCompanion> capacitors = /* ... */;
const int N = static_cast<int>(capacitors.size());
std::vector<double> geq(capacitors.size()), ieq(capacitors.size());
if (!cuda::evaluateCapacitorsCuda(capacitors.data(), N, dt, IntegrationMethod::TRAPEZOIDAL,
                                  geq.data(), ieq.data())) {
  // No device, or the device refused: evaluate on the host instead.
}
```

---

## 5. Common Patterns

### One-Shot Stamp + Solve

```cpp
companions.stampAll(mna, dt, method);
const auto RESULT = mna.solve();
companions.updateAll(RESULT.nodeVoltages, dt, method);
```

### Cached LU (factorize once, RHS each step)

```cpp
MnaFactorizedWorkspace factors;
factors.prepare(mna.netCount() + sourceCount);
double factoredStep = 0.0;    // step length the factors hold
double factoredFactor = 0.0;  // conductanceFactor the factors hold

for (double t = 0.0; t < tEnd; t += dt) {
  // Factors serve a step of the same length, method and factor.
  const double FACTOR = companions.conductanceFactor(dt, method);
  if (!factors.isFactorized() || dt != factoredStep || FACTOR != factoredFactor) {
    mna.clear();
    stampResistors(mna);
    companions.stampConductanceAll(mna, dt, method);
    stampSources(mna, t + dt);  // a voltage source's row and column are part of the matrix
    if (!mna.factorize(factors)) {
      break;  // singular, or a build without LAPACKE
    }
    factoredStep = dt;
    factoredFactor = FACTOR;
  }
  mna.clearRHS();             // currents and source values; the matrix stays
  stampSources(mna, t + dt);  // the same sources, in the same order
  companions.stampCurrentAll(mna, dt, method);
  if (!mna.solveFactorized(factors, voltages.data(), currents.data())) {
    break;  // history advances only by a step that was solved
  }
  companions.updateAll(voltages, dt, method);
}
```

The voltage sources are stamped before the factorization: each adds a row
and a column to the matrix that is factorized, and the right-hand side built
afterwards must name the same sources in the same order. GEAR2 factorizes
twice at a fixed step, once for its first step and once for the rest.

### Resetting State

```cpp
companions.reset();              // every element at rest at zero

// Seed from an operating point in which each inductor was solved as a
// conductance of 1000 S: capacitors take their voltages, inductors the
// current that conductance carried.
companions.initializeFromDC(dc, 1000.0);

// Or one element by hand.
companions.capacitor(0).setInitialVoltage(5.0);
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
    solution = mna.solve().nodeVoltages;
    companions.updateAll(solution, DT, IntegrationMethod::BACKWARD_EULER);
    fmt::print("t={:.3e}  vCap={:.6f}\n", t, solution[1]);
  }
  return 0;
}
```

---

## 9. Migration

`CapacitorCompanion` and `InductorCompanion` gained a member (`prevStep`; 40
to 48 bytes on x86-64) and `TransientSolver` changed layout. The CUDA
evaluators keep their names while the records they copy to the device grew.
Rebuild everything that uses the companions or the transient solver, host
objects and CUDA libraries together, from one revision.

| Earlier                                                           | Now                                                                                                                                                                                        |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `update(volts, dt)`, `updateAll(nodeVoltages, dt)`                | A third argument, with no default: the method the step was stamped with. Stamping with one method and updating with another leaves a history neither method computed.                      |
| `evaluateCapacitorsCuda`, `evaluateInductorsCuda` returned `void` | Return `bool`, `[[nodiscard]]`. On false neither output array holds a result: discard both and evaluate on the host.                                                                       |
| History set field by field (`prevVoltage = v`)                    | `setInitialVoltage(v)` / `setInitialCurrent(i)`: the value, the one before it and the step taken, together. A field set alone leaves the others from the last run.                         |
| `initializeFromDC(nodeVoltages)`                                  | `initializeFromDC(nodeVoltages, inductorSiemens)`: pass the conductance each inductor stood as in the operating-point solve to recover its current; with the default 0 they start at none. |
| GEAR2's inductor `geq` 3dt/(2L), `ieq` 2 I1 - I2/2                | 2dt/(3L) and (4 I1 - I2)/3 after an equal step; values computed from the old ones change.                                                                                                  |
| GEAR2's first step used Gear's weights on a repeated start value  | The first step is backward Euler's (`conductanceFactor` 1), the second onward Gear's. A caller that keeps factors sees one more change of factor at the start.                             |
| Factors kept across steps keyed on `dt` alone                     | Key on `dt` and `conductanceFactor(dt, method)` together.                                                                                                                                  |

---

## 10. See Also

- [Linear devices](../../devices/linear/README.md) - Resistor / capacitor / inductor primitives
- [Transient solver](../transient/README.md) - Integration methods and time stepping
- [MNA library](../mna/README.md) - Circuit matrix assembly and solve
