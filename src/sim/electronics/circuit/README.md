# Circuit

**Namespace:** `sim::electronics::circuit`
**Platform:** Linux-only
**C++ Standard:** C++23

Unified circuit construction and simulation API. Bridges the MNA solver,
companion models, and transient integrator into a single builder interface
that scales from a one-resistor RC filter to a 2,242-transistor microprocessor.

---

## Overview

`Circuit` is the user-facing entry point for building circuits in this library.
You allocate nets, register stamp callbacks for primitive elements (resistors,
sources, transistors), add reactive elements (capacitors, inductors), then call
`build()` and `simulate()` (or `step()` for manual time-stepping).

The same API is used by every circuit model in this library:

| Model                               | Construction                         |
| ----------------------------------- | ------------------------------------ |
| [filters](../topologies/filters/README.md)     | RC low-pass via 1 resistor + 1 cap   |
| [gates](../topologies/gates/README.md)         | Logic gate truth tables              |
| [intel4004](../intel4004/README.md) | 2,242-transistor PMOS microprocessor |

**RT-safety:** Construction is NOT RT-safe (allocates). After `build()`, time
stepping is RT-safe in steady state (no allocation per step) provided the
underlying solver is configured with cached LU.

---

## Quick Start

```cpp
#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

using sim::electronics::circuit::Circuit;
using sim::electronics::algorithms::transient::TransientConfig;

Circuit circuit;

// 1. Allocate nets (id 0 is always ground)
auto in  = circuit.addNet("IN").id;
auto out = circuit.addNet("OUT").id;

// 2. Register stamps for primitive elements
const double R = 1e3;
circuit.addStamp([=](auto& mna, double, const auto&) {
  mna.addConductance(in, out, 1.0 / R);
  mna.addVoltageSource(in, Circuit::ground(), 5.0);
});

// 3. Add reactive elements
circuit.addCapacitor(out, Circuit::ground(), 1e-6);

// 4. Build and simulate
TransientConfig config;
config.tStart = 0.0;
config.tEnd   = 5e-3;
config.tStep  = 10e-6;

auto result = circuit.simulate(config, /*recordHistory=*/true);
```

---

## API Reference

### Net allocation

| Function                              | Purpose                                |
| ------------------------------------- | -------------------------------------- |
| `CircuitNet addNet()`                 | Allocate an unnamed net                |
| `CircuitNet addNet(string_view name)` | Allocate a named net (for diagnostics) |
| `static NetID ground()`               | Ground net (always 0)                  |
| `size_t netCount() const`             | Number of nets allocated               |
| `string_view netName(NetID) const`    | Look up a net's name                   |

### Element registration

| Function                           | Purpose                                          |
| ---------------------------------- | ------------------------------------------------ |
| `void addStamp(StampFn)`           | Register a stamp callback for primitive elements |
| `size_t addCapacitor(pos, neg, F)` | Add a capacitor companion (returns index)        |
| `size_t addInductor(pos, neg, H)`  | Add an inductor companion (returns index)        |
| `CompanionSet& companions()`       | Direct access for setting initial conditions     |

The `StampFn` signature is `void(MnaSystem&, double time, const vector<double>& prevVoltages)`.
Stamp callbacks run in registration order at every NR iteration of every time step.

### Build and run

| Function                                             | Purpose                                             |
| ---------------------------------------------------- | --------------------------------------------------- |
| `void build()`                                       | Finalize circuit, create solver                     |
| `bool isBuilt() const`                               | Check build state                                   |
| `TransientResult simulate(config, recordHist=false)` | Run a transient simulation                          |
| `TransientStatus step(dt, state)`                    | Execute a single time step                          |
| `TransientStatus computeDC(state)`                   | Compute DC operating point                          |
| `TransientSolver& solver()`                          | Access underlying solver for advanced configuration |
| `void resetSolver()`                                 | Destroy solver, keep nets/stamps/companions         |

`build()` is called automatically by `simulate()`, `step()`, `computeDC()`, and
`solver()` if not already done.

---

## Stamp callback patterns

A stamp callback receives the MNA system and the previous time step's node
voltages. For linear elements, ignore `prevVoltages`. For nonlinear elements
(transistors), use `prevVoltages` to linearize the device around its current
operating point and stamp the Newton-Raphson Jacobian + RHS.

```cpp
// Linear: 1 kohm resistor between two nets
circuit.addStamp([=](auto& mna, double, const auto&) {
  mna.addConductance(a, b, 1.0 / 1000.0);
});

// Nonlinear: PMOS transistor (Level 1 NR linearization)
circuit.addStamp([=, &params](auto& mna, double, const auto& prev) {
  double vsg = prev[s] - prev[g];
  double vsd = prev[s] - prev[d];
  double id  = MosfetLevel1::current(vsg, vsd, params);
  double gm  = MosfetLevel1::transconductance(vsg, vsd, params);
  double gds = MosfetLevel1::outputConductance(vsg, vsd, params);
  // ... stamp gm, gds, ieq into mna ...
});
```

---

## Dependencies

| Library                                | Why                           |
| -------------------------------------- | ----------------------------- |
| `sim_electronics_algorithms_mna`                  | MNA matrix assembly + solve   |
| `sim_electronics_algorithms_transient` | Time stepping + integration   |
| `sim_electronics_algorithms_companions`   | Capacitor/inductor companions |

Header-only library. No source files. All consumers link
`sim_electronics_circuit` as an INTERFACE dependency.

---

## Performance

End-to-end build + solve throughput (debug build, clang-21, 15-repeat median):

| Circuit                  | Median (us) | Throughput | CV%  |
| ------------------------ | ----------- | ---------- | ---- |
| Build + DC, 3-net        | 3.2         | 312K/s     | var  |
| Build + DC, 50-net chain | 29.1        | 34.3K/s    | 3.9% |
| RC transient, 10 steps   | 10.4        | 96.4K/s    | var  |
| RC transient, 100 steps  | 58.7        | 17.0K/s    | var  |

Pipeline: 4.06 IPC, 0.16% branch miss. The Circuit API is zero-overhead --
100% of time is in LAPACK/BLAS via the underlying TransientSolver. Per-step
cost amortizes setup: 1.0 us/step at 10 steps, 0.59 us/step at 100 steps.

## Testing

The Circuit API is exercised end-to-end by every model that builds on it:

```bash
make compose-debug   # Build
make compose-testp   # Run all tests including filters, gates, intel4004
```

Direct unit tests live in the consumers:

| Consumer  | Test target                    |
| --------- | ------------------------------ |
| filters   | `TestSimElectronicsFilters`    |
| gates     | `TestSimElectronicsGates`      |
| intel4004 | `TestSimElectronicsIntel4004*` |

---

## See Also

- [filters](../topologies/filters/README.md) -- analog filter models built on Circuit
- [gates](../topologies/gates/README.md) -- digital logic models
- [intel4004](../intel4004/README.md) -- microprocessor model
- [algorithms/mna](../algorithms/mna/README.md) -- underlying linear solver
- [algorithms/transient](../algorithms/transient/README.md) -- time-stepping
- [algorithms/companions](../algorithms/companions/inc/CompanionModels.hpp) -- reactive element discretization
