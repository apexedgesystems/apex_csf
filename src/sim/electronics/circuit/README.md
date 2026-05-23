# Circuit Module

**Namespace:** `sim::electronics::circuit`
**Platform:** Linux-only
**C++ Standard:** C++23

Unified circuit construction and simulation API. Bridges the MNA solver,
companion models, and transient integrator into a single builder
interface that scales from a one-resistor RC filter to a 2,242-transistor
microprocessor.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [Circuit](#circuit) - Net allocation + element registration + run
   - [CircuitNet](#circuitnet) - Net identifier struct
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: RC Filter via Circuit](#8-example-rc-filter-via-circuit)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                           | API                            |
| -------------------------------------------------- | ------------------------------ |
| How do I allocate a net?                           | `Circuit::addNet`              |
| How do I get the ground net?                       | `Circuit::ground()` (always 0) |
| How do I register a stamp for resistors / sources? | `Circuit::addStamp`            |
| How do I add a capacitor?                          | `Circuit::addCapacitor`        |
| How do I add an inductor?                          | `Circuit::addInductor`         |
| How do I run a full transient?                     | `Circuit::simulate`            |
| How do I step manually?                            | `Circuit::step`                |
| How do I compute a DC operating point?             | `Circuit::computeDC`           |
| How do I configure the solver directly?            | `Circuit::solver()`            |

| Built on Circuit                                | Construction                         |
| ----------------------------------------------- | ------------------------------------ |
| [filters](../topologies/filters/README.md)      | RC low-pass via 1 resistor + 1 cap   |
| [gates](../topologies/gates/README.md)          | Logic-gate truth tables              |
| [chips/intel4004](../chips/intel4004/README.md) | 2,242-transistor PMOS microprocessor |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/circuit/inc/Circuit.hpp"

using sim::electronics::circuit::Circuit;

Circuit circuit;
const auto IN  = circuit.addNet("IN").id;
const auto OUT = circuit.addNet("OUT").id;

circuit.addStamp([=](auto& mna, double /*t*/, const auto& /*prevV*/) {
  mna.addConductance(IN, OUT, 1.0 / 1e3);
  mna.addVoltageSource(IN, Circuit::ground(), 5.0);
});
circuit.addCapacitor(OUT, Circuit::ground(), 1e-6);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Builder pattern.** Construction is staged: allocate nets, register
  stamp callbacks, add reactive elements, then `build()` to materialize
  the underlying solver. Topology changes after `build()` invalidate
  cached LU.
- **Stamps are closures.** Stamp callbacks capture parameters (resistor
  values, source voltages, MOSFET parameters) and run inside the NR
  iteration. The `prevVoltages` argument exposes the previous step for
  nonlinear linearization.
- **Lazy build.** `simulate`, `step`, `computeDC`, and `solver()` call
  `build()` if the user has not done so.
- **Zero-overhead facade.** The Circuit API only routes calls into the
  MNA / transient stack; there is no copy of the matrix or RHS.

---

## 4. Module Reference

### Circuit

**Header:** `Circuit.hpp`
**Purpose:** Top-level builder for circuit models.

#### Key Types

```cpp
using StampFn = std::function<void(MnaSystem&, double time,
                                   const std::vector<double>& prevVoltages)>;

struct CircuitNet {
  NetID id;
  std::string_view name;
};
```

#### API

| Function                                                                              | Purpose                                         |
| ------------------------------------------------------------------------------------- | ----------------------------------------------- |
| `CircuitNet addNet()`                                                                 | Allocate an unnamed net                         |
| `CircuitNet addNet(std::string_view name)`                                            | Allocate a named net                            |
| `static NetID ground() noexcept`                                                      | Ground net (always 0)                           |
| `std::size_t netCount() const noexcept`                                               | Number of nets allocated                        |
| `std::string_view netName(NetID) const noexcept`                                      | Look up a net's name                            |
| `void addStamp(StampFn fn)`                                                           | Register a stamp callback                       |
| `std::size_t addCapacitor(NetID pos, NetID neg, double F)`                            | Add a capacitor companion                       |
| `std::size_t addInductor(NetID pos, NetID neg, double H)`                             | Add an inductor companion                       |
| `CompanionSet& companions() noexcept`                                                 | Direct access for initial conditions            |
| `void build()`                                                                        | Finalize and create the solver                  |
| `bool isBuilt() const noexcept`                                                       | Check build state                               |
| `TransientResult simulate(const TransientConfig& config, bool recordHistory = false)` | Run a transient simulation                      |
| `TransientStatus step(double dt, TransientState& state) noexcept`                     | Execute a single time step                      |
| `TransientStatus computeDC(TransientState& state)`                                    | Compute DC operating point                      |
| `TransientSolver& solver() noexcept`                                                  | Access underlying solver                        |
| `void resetSolver() noexcept`                                                         | Destroy solver; keep nets / stamps / companions |

`build()` is NOT RT-safe (allocates). After `build()`, `step()` is
RT-safe when the underlying solver runs in cached-LU mode.

### CircuitNet

`CircuitNet { NetID id; std::string_view name; }` is the value returned
from `addNet`. Tests and diagnostics use `name`; runtime stamps use
`id`.

---

## 5. Common Patterns

### Linear Stamp

```cpp
circuit.addStamp([=](auto& mna, double /*t*/, const auto& /*prevV*/) {
  mna.addConductance(a, b, 1.0 / 1000.0);
});
```

### Nonlinear Stamp (NR linearization)

```cpp
circuit.addStamp([=, &params](auto& mna, double /*t*/, const auto& prevV) {
  const double VSG = prevV[s] - prevV[g];
  const double VSD = prevV[s] - prevV[d];
  const double ID  = MosfetLevel1::current(VSG, VSD, params);
  const double GM  = MosfetLevel1::transconductance(VSG, VSD, params);
  const double GDS = MosfetLevel1::outputConductance(VSG, VSD, params);
  // ... stamp GM, GDS, IEQ into mna ...
});
```

### DC Operating Point

```cpp
TransientState state;
state.resize(circuit.netCount(), 0);
circuit.computeDC(state);
```

### Manual Time Stepping

```cpp
circuit.build();
TransientState state;
state.resize(circuit.netCount(), 0);
circuit.computeDC(state);

const double DT = 1e-6;
for (double t = 0.0; t < tEnd; t += DT) {
  circuit.step(DT, state);
}
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `Circuit::step` (after `build()`, with cached LU configured).
- `Circuit::computeDC` (uses pre-sized workspace).
- All accessors (`netCount`, `netName`, `isBuilt`, `ground`).

### NOT RT-Safe Functions

- Constructor / `build()` / `simulate()` (allocate workspace; `simulate`
  may also allocate history).
- `addNet`, `addStamp`, `addCapacitor`, `addInductor` (grow internal
  containers).
- `resetSolver` (destroys workspace).

### Recommended Configuration

- Build the circuit during setup, run `computeDC` once, then enter the
  RT loop and only call `step`.
- Toggle `solver().setCachedLU(true)` (or `setDualLU(true)` for clocked
  topologies) before the loop.

---

## 7. CLI Tools

- `apex_circuit_demo` exercises the Circuit API end-to-end with all
  topology models. See
  [apps/apex_circuit_demo](../../../apps/apex_circuit_demo/).

---

## 8. Example: RC Filter via Circuit

```cpp
#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics;

  circuit::Circuit circuit;
  const auto IN  = circuit.addNet("IN").id;
  const auto OUT = circuit.addNet("OUT").id;

  circuit.addStamp([=](auto& mna, double /*t*/, const auto& /*prevV*/) {
    mna.addConductance(IN, OUT, 1.0 / 1e3);
    mna.addVoltageSource(IN, circuit::Circuit::ground(), 5.0);
  });
  circuit.addCapacitor(OUT, circuit::Circuit::ground(), 1e-6);

  algorithms::transient::TransientConfig config;
  config.tStart = 0.0;
  config.tEnd   = 5e-3;
  config.tStep  = 10e-6;

  const auto RESULT = circuit.simulate(config, /*recordHistory=*/true);
  for (const auto& STATE : RESULT.history) {
    fmt::print("t={:.3e}  v(out)={:.6f}\n", STATE.time, STATE.nodeVoltages[OUT]);
  }
  return 0;
}
```

---

## 9. See Also

- [topologies/filters](../topologies/filters/README.md) - Analog filter models built on Circuit
- [topologies/gates](../topologies/gates/README.md) - Digital logic models
- [chips/intel4004](../chips/intel4004/README.md) - Microprocessor model
- [algorithms/mna](../algorithms/mna/README.md) - Underlying linear solver
- [algorithms/transient](../algorithms/transient/README.md) - Time stepping
- [algorithms/companions](../algorithms/companions/README.md) - Reactive-element discretization
