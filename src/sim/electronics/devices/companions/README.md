# Companion Device Models

Numerical integration wrappers for reactive circuit elements (capacitors, inductors).
Part of Layer 2 (Device Models) in the electronics simulation library.

---

## Overview

Companion models convert continuous-time differential equations into discrete-time
equivalent circuits (resistor + current source) that can be stamped into the MNA
matrix for time-stepping simulation.

- **CapacitorCompanion:** I = C \* dV/dt discretized as Geq + Ieq
- **InductorCompanion:** V = L \* dI/dt discretized as Geq + Ieq
- **CompanionSet:** Bulk management of all reactive companions in a circuit

**Integration methods:** Backward Euler (first-order, A-stable), Trapezoidal
(second-order, energy-conserving), GEAR2/BDF2 (second-order, L-stable for stiff
systems).

**RT-safety:** All stamp/update methods are RT-safe (no allocations after setup).

---

## Models

### CapacitorCompanion

Discretized capacitor stamped as equivalent conductance + current source.

```cpp
#include "src/sim/electronics/devices/companions/inc/CompanionModels.hpp"

using namespace sim::electronics::devices::companions;

CapacitorCompanion cap{.posNet = 1, .negNet = 0, .capacitance = 1e-9};

// Stamp into MNA for backward Euler
cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);

// After solve: update state for next timestep
double vCap = result.voltages[1] - result.voltages[0];
cap.update(vCap, dt);
```

### InductorCompanion

Discretized inductor stamped as equivalent conductance + current source.

```cpp
InductorCompanion ind{.posNet = 1, .negNet = 0, .inductance = 1e-6};

// Stamp into MNA for trapezoidal integration
ind.stamp(mna, dt, IntegrationMethod::TRAPEZOIDAL);

// After solve: update state for next timestep
double vInd = result.voltages[1] - result.voltages[0];
ind.update(vInd, dt);
```

### CompanionSet

Bulk container for all reactive companions in a circuit. Provides batch stamp
and update operations.

```cpp
CompanionSet companions;
companions.addCapacitor(1, 0, 100e-12);  // 100pF
companions.addCapacitor(3, 2, 1e-9);     // 1nF
companions.addInductor(4, 0, 10e-6);     // 10uH

// Stamp all companions at once
companions.stampAll(mna, dt, IntegrationMethod::BACKWARD_EULER);

// After solve: update all states
companions.updateAll(result.voltages, dt);
```

---

## Dependencies

- `sim_electronics_algorithms_mna` - MNA system (dense and sparse)
- `sim_electronics_algorithms_transient` - Integration method definitions
- `utilities_compatibility` - C++17/20/23 compatibility

---

## Performance

| Operation                   | Latency          | Scale          |
| --------------------------- | ---------------- | -------------- |
| Capacitor companion geq+ieq | 8.3 ns/capacitor | Backward Euler |
| CompanionSet stampAll       | 53.6 ns/element  | 50 elements    |

Pipeline efficiency is 3.79 IPC. Companion evaluation is pure arithmetic
(division + multiply for geq/ieq), with stamp cost dominated by MNA matrix
writes.

## Testing

Run unit tests:

```bash
make compose-testp
# Tests: sim_electronics_devices_companions_uTest
```

---

## See Also

- **Linear Devices:** `../linear/README.md` - Resistor, capacitor, inductor models
- **Transient Solver:** `../../algorithms/transient/README.md` - Integration methods
- **MNA Library:** `../../algorithms/mna/README.md` - Circuit solver
