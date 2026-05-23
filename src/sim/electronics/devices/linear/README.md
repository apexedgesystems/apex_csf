# Linear Devices Module

**Namespace:** `sim::electronics::devices::linear`
**Platform:** Linux-only
**C++ Standard:** C++23

Linear device physics (resistor, capacitor, inductor) for the circuit
solver. Static stamps for resistors; companion-model wrappers for
capacitors and inductors.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [ResistorModel](#resistormodel)
   - [CapacitorModel](#capacitormodel)
   - [InductorModel](#inductormodel)
   - [LinearDevices](#lineardevices) - Registry header
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Voltage Divider DC Solve](#8-example-voltage-divider-dc-solve)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                               | API                                                                          |
| ------------------------------------------------------ | ---------------------------------------------------------------------------- |
| How do I stamp a resistor into an MNA system?          | `ResistorModel::stamp`                                                       |
| What conductance does a resistor present?              | `ResistorModel::conductance`                                                 |
| What current does a resistor carry at a given voltage? | `ResistorModel::current`                                                     |
| What is a capacitor's reactance at a given frequency?  | `CapacitorModel::reactance`                                                  |
| What is an inductor's reactance at a given frequency?  | `InductorModel::reactance`                                                   |
| How do I stamp C / L in transient simulation?          | `CapacitorCompanion::stamp` / `InductorCompanion::stamp` (companions module) |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"

using namespace sim::electronics::devices::linear;
using sim::electronics::algorithms::companions::CapacitorCompanion;
using sim::electronics::algorithms::companions::InductorCompanion;

ResistorModel::stamp(mna, /*a=*/1, /*b=*/0, /*R=*/10e3);

CapacitorCompanion cap{/*pos=*/2, /*neg=*/0, /*C=*/1e-9};
cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Linear == exact.** All three devices have closed-form behavior. No
  Newton-Raphson iterations or convergence concerns.
- **Static helpers.** `ResistorModel`, `CapacitorModel`, and
  `InductorModel` are stateless utility namespaces. The dynamic state
  (Vprev, Iprev) lives on the companion structs defined in the companions
  module.
- **Stamping is RT-safe.** `stamp` only writes into the MNA system; the
  underlying matrix is pre-sized.

---

## 4. Module Reference

### ResistorModel

**Header:** `ResistorModel.hpp`
**Purpose:** Ohm's law helpers and MNA stamping.

#### API

```cpp
[[nodiscard]] static double conductance(double resistance) noexcept;
[[nodiscard]] static double current(double voltage, double resistance) noexcept;

static void stamp(MnaSystem& mna, NetID a, NetID b, double resistance);
static void stamp(MnaSystemSparse& mna, NetID a, NetID b, double resistance);
```

#### Usage

```cpp
const double G = ResistorModel::conductance(10e3); // 1e-4 S
ResistorModel::stamp(mna, /*a=*/1, /*b=*/0, /*R=*/10e3);
```

### CapacitorModel

**Header:** `CapacitorModel.hpp`
**Purpose:** Reactance helper for AC analysis. Transient stamping is
delegated to `CapacitorCompanion` in the companions module.

#### API

```cpp
[[nodiscard]] static double reactance(double capacitance, double frequencyHz) noexcept;
```

#### Usage

```cpp
const double X_C = CapacitorModel::reactance(/*C=*/1e-6, /*f=*/1e3);
```

### InductorModel

**Header:** `InductorModel.hpp`
**Purpose:** Reactance helper for AC analysis. Transient stamping is
delegated to `InductorCompanion` in the companions module.

#### API

```cpp
[[nodiscard]] static double reactance(double inductance, double frequencyHz) noexcept;
```

#### Usage

```cpp
const double X_L = InductorModel::reactance(/*L=*/1e-3, /*f=*/1e3);
```

### LinearDevices

**Header:** `LinearDevices.hpp`
**Purpose:** Single include that brings in all three models plus the
companion-model types from `algorithms/companions`.

---

## 5. Common Patterns

### Static Voltage Divider

```cpp
ResistorModel::stamp(mna, VDD, OUT, 10e3);
ResistorModel::stamp(mna, OUT, GND, 10e3);
mna.addVoltageSource(VDD, GND, 5.0);
```

### RC Low-Pass in a Transient Loop

```cpp
CapacitorCompanion cap{OUT, GND, 1e-9};
for (double t = 0.0; t < tEnd; t += dt) {
  mna.clear();
  ResistorModel::stamp(mna, IN, OUT, 1e3);
  cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
  mna.addVoltageSource(IN, GND, vIn(t));
  mna.solve(solution);
  cap.update(solution[OUT], dt);
}
```

### Frequency-Domain Magnitude

```cpp
const double F     = 1e3;
const double X_C   = CapacitorModel::reactance(1e-6, F);
const double X_L   = InductorModel::reactance(1e-3, F);
const double MAG_Z = std::hypot(X_L - X_C, /*R=*/100.0);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- All static helpers (`conductance`, `current`, `reactance`).
- `ResistorModel::stamp` once the MNA system has been sized.

### NOT RT-Safe Functions

- None within this module. Capacitor and inductor companion updates may
  resize history vectors when `addCapacitor` / `addInductor` is called
  on the companions module's `CompanionSet`; that should happen outside
  the RT loop.

---

## 7. CLI Tools

None.

---

## 8. Example: Voltage Divider DC Solve

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics;

  const algorithms::mna::NetID VDD = 1, OUT = 2, GND = 0;

  algorithms::mna::MnaSystem mna(/*nodeCount=*/3);
  devices::linear::ResistorModel::stamp(mna, VDD, OUT, 10e3);
  devices::linear::ResistorModel::stamp(mna, OUT, GND, 10e3);
  mna.addVoltageSource(VDD, GND, 5.0);

  const auto RESULT = mna.solve();
  fmt::print("V(out) = {:.6f} V\n", RESULT.nodeVoltages[OUT]);
  return 0;
}
```

---

## 9. See Also

- [Descriptors](../descriptors/README.md) - Topology-only descriptions of the same primitives
- [Companions](../../algorithms/companions/README.md) - Transient integration wrappers for C / L
- [MNA library](../../algorithms/mna/README.md) - Circuit solver
