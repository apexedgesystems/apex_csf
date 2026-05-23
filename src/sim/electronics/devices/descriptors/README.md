# Descriptors Module

**Namespace:** `sim::electronics::devices::descriptors`
**Platform:** Linux-only
**C++ Standard:** C++23

Pure topology descriptions for circuit devices. Descriptors carry net IDs and
parameters only - no physics, no stamping, no simulation state.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [ResistorDescriptor](#resistordescriptor)
   - [CapacitorDescriptor](#capacitordescriptor)
   - [InductorDescriptor](#inductordescriptor)
   - [MosfetDescriptor](#mosfetdescriptor)
   - [Descriptors](#descriptors) - Aggregate registry header
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Topology-First Netlist Build](#8-example-topology-first-netlist-build)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                      | Descriptor            |
| --------------------------------------------- | --------------------- |
| How do I describe a resistor's connectivity?  | `ResistorDescriptor`  |
| How do I describe a capacitor's connectivity? | `CapacitorDescriptor` |
| How do I describe an inductor's connectivity? | `InductorDescriptor`  |
| How do I describe a MOSFET's connectivity?    | `MosfetDescriptor`    |
| How do I import every descriptor at once?     | `Descriptors.hpp`     |

| Concept        | Responsibility                 | Example                          |
| -------------- | ------------------------------ | -------------------------------- |
| **Descriptor** | Topology (NetIDs + parameters) | `ResistorDescriptor{1, 0, 10e3}` |
| **Model**      | Physics (I-V curves, stamping) | `ResistorModel::stamp(...)`      |

Pairing a descriptor with a model produces a stamped device. Descriptors are
deliberately model-agnostic so the same topology can drive different fidelity
physics without rewriting the circuit.

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"

using namespace sim::electronics::devices::descriptors;

ResistorDescriptor pullup   {/*pos=*/1, /*neg=*/2, /*R=*/10e3};
CapacitorDescriptor decap   {/*pos=*/2, /*neg=*/0, /*C=*/100e-12};
InductorDescriptor filter   {/*pos=*/1, /*neg=*/2, /*L=*/10e-6};
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

Descriptors **are**:

- Pure topology (NetIDs + scalar parameters).
- Stateless: no voltages, currents, or history.
- Trivial: aggregate-initializable structs, no allocations.
- RT-safe: usable from any context.
- Thread-safe: no shared mutable state.

Descriptors **are not**:

- Physics models. No I-V curves and no stamping code.
- Stateful devices. Voltages and currents belong to the solver workspace.
- PCB layout. No footprints or physical placement.
- Parameter wrappers. Values are stored as-is; runtime tuning is a model
  concern.

---

## 4. Module Reference

### ResistorDescriptor

**Header:** `ResistorDescriptor.hpp`
**Purpose:** Two-terminal resistor topology.

```cpp
struct ResistorDescriptor {
  NetID posNet;       ///< Positive terminal
  NetID negNet;       ///< Negative terminal
  double resistance;  ///< Ohms
};
```

### CapacitorDescriptor

**Header:** `CapacitorDescriptor.hpp`
**Purpose:** Two-terminal capacitor topology.

```cpp
struct CapacitorDescriptor {
  NetID posNet;
  NetID negNet;
  double capacitance; ///< Farads
};
```

### InductorDescriptor

**Header:** `InductorDescriptor.hpp`
**Purpose:** Two-terminal inductor topology.

```cpp
struct InductorDescriptor {
  NetID posNet;
  NetID negNet;
  double inductance; ///< Henries
};
```

### MosfetDescriptor

**Header:** `MosfetDescriptor.hpp`
**Purpose:** Four-terminal MOSFET topology (drain, gate, source, body).

```cpp
struct MosfetDescriptor {
  NetID drainNet;
  NetID gateNet;
  NetID sourceNet;
  NetID bodyNet;
  double width;    ///< Meters
  double length;   ///< Meters
};
```

### Descriptors

**Header:** `Descriptors.hpp`
**Purpose:** Single include that brings in every descriptor type.

---

## 5. Common Patterns

### Netlist-Driven Construction

```cpp
std::vector<ResistorDescriptor> resistors;
for (const auto& LINE : netlistLines) {
  if (LINE.starts_with("R")) {
    resistors.push_back(parseResistor(LINE));
  }
}
for (const auto& R : resistors) {
  ResistorModel::stamp(mna, R.posNet, R.negNet, R.resistance);
}
```

### Fidelity Switching

```cpp
const std::vector<MosfetDescriptor> TRANSISTORS = parseNetlist("circuit.spice");

for (const auto& M : TRANSISTORS) {
  MosfetBinarySwitch::stamp(mna, M); // fast verification
}
// ... or ...
for (const auto& M : TRANSISTORS) {
  MosfetLevel1::stamp(mna, M);       // analog accuracy
}
```

### Connectivity-Only Tests

```cpp
TEST(Netlist, Connectivity) {
  const auto DESCS = parseNetlist("test.spice");
  EXPECT_EQ(DESCS[0].posNet, VDD);
  EXPECT_EQ(DESCS[0].negNet, OUTPUT);
  EXPECT_DOUBLE_EQ(DESCS[0].resistance, 10e3);
}
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- All descriptor construction and copy operations (POD types).

### NOT RT-Safe Functions

- None within this module; allocation and I/O happen in the consumers
  (netlist parser, MNA system, transient solver).

---

## 7. CLI Tools

None.

---

## 8. Example: Topology-First Netlist Build

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics;

  const algorithms::mna::NetID VDD = 1, OUT = 2, GND = 0;

  devices::descriptors::ResistorDescriptor r1{VDD, OUT, 10e3};
  devices::descriptors::CapacitorDescriptor c1{OUT, GND, 100e-12};

  algorithms::mna::MnaSystem mna(/*nodeCount=*/3);
  devices::linear::ResistorModel::stamp(mna, r1.posNet, r1.negNet, r1.resistance);

  fmt::print("R1 spans nets {} - {} with {} Ohm\n",
             r1.posNet, r1.negNet, r1.resistance);
  fmt::print("C1 spans nets {} - {} with {} F\n",
             c1.posNet, c1.negNet, c1.capacitance);
  return 0;
}
```

---

## 9. See Also

- [Linear models](../linear/README.md) - Physics for resistors / capacitors / inductors
- [Companions](../../algorithms/companions/README.md) - Transient integration wrappers
- [MNA library](../../algorithms/mna/README.md) - Circuit solver
