# Linear Device Models

Linear device physics models (resistor, capacitor, inductor) for circuit simulation.
Part of Layer 2 (Device Models) in the electronics simulation library.

---

## Overview

Linear devices are exact (no approximations, no Newton-Raphson iterations) with a
single fidelity level that covers all simulation needs:

- **Resistor:** Ohm's law (V = I\*R), static stamping
- **Capacitor:** Reactive element, companion model for transient simulation
- **Inductor:** Reactive element, companion model for transient simulation

**RT-safety:** All models are RT-safe (static functions, no allocations).

---

## Models

### ResistorModel

Static conductance stamping (Ohm's law).

```cpp
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

using namespace sim::electronics::devices::linear;

// Calculate conductance
double g = ResistorModel::conductance(1000.0);  // 1kΩ → 1mS

// Calculate current
double i = ResistorModel::current(5.0, 1000.0);  // 5V / 1kΩ = 5mA

// Stamp into MNA system
ResistorModel::stamp(mna, VDD, OUTPUT, 10e3);  // 10kΩ resistor
```

### CapacitorModel

Reactive element using companion model from companions library.

```cpp
#include "src/sim/electronics/devices/linear/inc/CapacitorModel.hpp"

using namespace sim::electronics::devices::linear;

// Calculate reactance (for AC analysis)
double xc = CapacitorModel::reactance(1e-6, 1000.0);  // 1µF at 1kHz

// Transient simulation (uses companion model)
CapacitorCompanion cap{OUTPUT, GND, 1e-6};  // 1µF capacitor
cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
auto result = mna.solve();
double voltage = result.voltages[OUTPUT] - result.voltages[GND];
cap.update(voltage, dt);  // Update state for next timestep
```

### InductorModel

Reactive element using companion model from companions library.

```cpp
#include "src/sim/electronics/devices/linear/inc/InductorModel.hpp"

using namespace sim::electronics::devices::linear;

// Calculate reactance (for AC analysis)
double xl = InductorModel::reactance(1e-3, 1000.0);  // 1mH at 1kHz

// Transient simulation (uses companion model)
InductorCompanion ind{VDD, OUTPUT, 1e-3};  // 1mH inductor
ind.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
auto result = mna.solve();
double voltage = result.voltages[VDD] - result.voltages[OUTPUT];
ind.update(voltage, dt);  // Update state for next timestep
```

---

## Registry Header

Import all linear models at once:

```cpp
#include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"

using namespace sim::electronics::devices::linear;

// All models available:
ResistorModel::stamp(mna, a, b, R);
CapacitorCompanion cap{a, b, C};
InductorCompanion ind{a, b, L};
```

---

## Usage Examples

### Voltage Divider (DC)

```cpp
#include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using namespace sim::electronics::devices::linear;

void simulateVoltageDivider() {
  constexpr NetID VDD = 1, OUTPUT = 2, GND = 0;

  MnaSystem mna(3);

  // Voltage divider: VDD --(R1)-- OUTPUT --(R2)-- GND
  ResistorModel::stamp(mna, VDD, OUTPUT, 10e3);  // R1 = 10kΩ
  ResistorModel::stamp(mna, OUTPUT, GND, 10e3);  // R2 = 10kΩ

  // Voltage source: VDD = 5V
  mna.stampVoltageSource(VDD, GND, 5.0);

  auto result = mna.solve();
  // result.voltages[OUTPUT] ≈ 2.5V (half of 5V)
}
```

### RC Low-Pass Filter (Transient)

```cpp
#include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using namespace sim::electronics::devices::linear;

void simulateRcFilter() {
  constexpr NetID INPUT = 1, OUTPUT = 2, GND = 0;
  constexpr double dt = 1e-6;  // 1 µs timestep

  MnaSystem mna(3);

  // Resistor (static stamping, once before time loop)
  ResistorModel::stamp(mna, INPUT, OUTPUT, 1000.0);  // 1kΩ

  // Capacitor (dynamic, re-stamp each timestep)
  CapacitorCompanion cap{OUTPUT, GND, 1e-9};  // 1nF

  // Time loop
  for (int step = 0; step < 1000; ++step) {
    mna.clearStamps();  // Clear previous timestep

    // Re-stamp resistor and capacitor
    ResistorModel::stamp(mna, INPUT, OUTPUT, 1000.0);
    cap.stamp(mna, dt);

    // Input voltage (step function)
    double vin = (step < 100) ? 0.0 : 5.0;
    mna.stampVoltageSource(INPUT, GND, vin);

    // Solve and update
    auto result = mna.solve();
    double vout = result.voltages[OUTPUT];
    cap.update(vout, dt);

    // vout will exponentially approach 5V with time constant RC = 1µs
  }
}
```

---

## Dependencies

- `sim_electronics_mna` - MNA system and stamping functions
- `sim_electronics_transient` - Companion models for C, L
- `utilities_compatibility` - C++17/20/23 compatibility

---

## Performance

| Operation                 | Latency          | Scale           |
| ------------------------- | ---------------- | --------------- |
| Resistor stamp (into MNA) | 48.0 ns/resistor | 1000 resistors  |
| Conductance calculation   | 1.86 ns/eval     | 10K evaluations |

Batch throughput: 20.8K resistor-stamp batches/s. Pipeline efficiency is 3.59
IPC. The stamp cost is dominated by MNA triplet storage, not the conductance
arithmetic (single fdiv).

## Testing

Run unit tests:

```bash
make compose-testp
# Tests: sim_electronics_devices_linear_uTest
```

**Test coverage:**

- Resistor: Ohm's law, conductance, stamping (dense, sparse)
- Capacitor: Reactance calculation, impedance
- Inductor: Reactance calculation, impedance

**Companion model tests:** See `sim_electronics_transient_uTest` for full
companion model validation (integration methods, state update, convergence).

---

## See Also

- **Companion Models:** `../companions/README.md` - Companion models library
- **MNA Library:** `../../algorithms/mna/README.md` - MNA system usage
