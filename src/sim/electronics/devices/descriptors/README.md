# Device Topology Descriptors

Pure topology descriptions for circuit devices. Descriptors specify **connectivity
and parameters only** - no physics, no simulation code.

## Purpose

Descriptors enable **separation of topology from physics** in the Layer 2 device
model architecture:

| Concept        | Responsibility                 | Example                          |
| -------------- | ------------------------------ | -------------------------------- |
| **Descriptor** | Topology (NetIDs + parameters) | `ResistorDescriptor{1, 0, 10e3}` |
| **Model**      | Physics (I-V curves, stamping) | `ResistorModel::stamp(...)`      |
| **Component**  | Descriptor + Model (future)    | `Resistor{desc, model}`          |

This separation provides:

1. **Fidelity switching**: Same circuit, different physics accuracy
2. **Netlist parsing**: Parse topology independent of choosing physics models
3. **Test isolation**: Verify connectivity without running simulation
4. **Grid construction**: Build circuit structure separate from simulation policy

## Quick Start

```cpp
#include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"

using namespace sim::electronics::devices::descriptors;

// Define circuit topology
ResistorDescriptor r1{VDD, OUTPUT, 10e3};    // 10kOhm pullup
CapacitorDescriptor c1{OUTPUT, GND, 100e-12}; // 100pF decoupling
InductorDescriptor l1{VDD, OUTPUT, 10e-6};   // 10uH filter

// Physics models use descriptors
ResistorModel::stamp(mna, r1.posNet, r1.negNet, r1.resistance);

CapacitorCompanion cap;
cap.posNet = c1.posNet;
cap.negNet = c1.negNet;
cap.capacitance = c1.capacitance;
```

## Available Descriptors

### ResistorDescriptor

Two-terminal resistor topology.

```cpp
struct ResistorDescriptor {
  NetID posNet;       // Positive terminal
  NetID negNet;       // Negative terminal
  double resistance;  // Ohms
};

// Examples
ResistorDescriptor pullup{VDD, OUTPUT, 10e3};     // 10kOhm
ResistorDescriptor series{INPUT, OUTPUT, 100.0};  // 100Ohm
```

### CapacitorDescriptor

Two-terminal capacitor topology.

```cpp
struct CapacitorDescriptor {
  NetID posNet;        // Positive terminal
  NetID negNet;        // Negative terminal
  double capacitance;  // Farads
};

// Examples
CapacitorDescriptor decoupling{VDD, GND, 100e-9};  // 100nF
CapacitorDescriptor coupling{IN, OUT, 1e-6};       // 1uF
```

### InductorDescriptor

Two-terminal inductor topology.

```cpp
struct InductorDescriptor {
  NetID posNet;       // Positive terminal
  NetID negNet;       // Negative terminal
  double inductance;  // Henries
};

// Examples
InductorDescriptor filter{VDD, OUTPUT, 10e-6};  // 10uH
InductorDescriptor choke{INPUT, GND, 1e-3};     // 1mH
```

## Usage Patterns

### Netlist Parsing

```cpp
// Parse SPICE netlist into descriptors
std::vector<ResistorDescriptor> resistors;
std::vector<CapacitorDescriptor> capacitors;

for (const auto& line : netlistLines) {
  if (line.starts_with("R")) {
    resistors.push_back(parseResistor(line));
  } else if (line.starts_with("C")) {
    capacitors.push_back(parseCapacitor(line));
  }
}

// Later: apply physics model selection
for (const auto& r : resistors) {
  ResistorModel::stamp(mna, r.posNet, r.negNet, r.resistance);
}
```

### Fidelity Switching

```cpp
// Same circuit topology, different physics accuracy
std::vector<MosfetDescriptor> transistors = parseNetlist("circuit.spice");

// Fast verification (binary switch)
for (const auto& m : transistors) {
  MosfetBinarySwitch::stamp(mna, m);
}

// Accurate verification (Level 1 model)
for (const auto& m : transistors) {
  MosfetLevel1::stamp(mna, m);
}
```

### Test Isolation

```cpp
TEST(Netlist, Connectivity) {
  auto descriptors = parseNetlist("test.spice");

  // Verify topology without running simulation
  EXPECT_EQ(descriptors[0].posNet, VDD);
  EXPECT_EQ(descriptors[0].negNet, OUTPUT);
  EXPECT_DOUBLE_EQ(descriptors[0].resistance, 10e3);
}
```

## Design Principles

### [OK] Descriptors ARE

- **Pure topology**: NetIDs and device parameters only
- **Stateless**: No simulation state (voltages, currents)
- **Lightweight**: Trivial structs, no allocations
- **RT-safe**: Can be used in real-time contexts
- **Thread-safe**: No shared mutable state

### [X] Descriptors are NOT

- **Physics models**: No I-V curves, no stamping code
- **Components**: No runtime behavior
- **Stateful**: No voltage/current state
- **PCB layout**: No footprints or physical placement
- **Parameterized**: Device parameters are fixed (not tunable at runtime)

## Integration with Physics Models

Descriptors are **consumed by physics models**, not the other way around:

```cpp
// Descriptor: topology only
ResistorDescriptor r{VDD, OUTPUT, 10e3};

// Model: physics only (uses descriptor data)
ResistorModel::stamp(mna, r.posNet, r.negNet, r.resistance);

// Future: Component = Descriptor + Model
Resistor component{r, ResistorModel{}};
```

## Files

| File                          | Purpose                  |
| ----------------------------- | ------------------------ |
| `inc/ResistorDescriptor.hpp`  | Resistor topology        |
| `inc/CapacitorDescriptor.hpp` | Capacitor topology       |
| `inc/InductorDescriptor.hpp`  | Inductor topology        |
| `inc/Descriptors.hpp`         | Registry (single import) |
| `utst/*_uTest.cpp`            | Unit tests (27 tests)    |

## Performance

| Operation                    | Latency           | Scale            |
| ---------------------------- | ----------------- | ---------------- |
| MOSFET descriptor creation   | 6.2 ns/descriptor | 2242 descriptors |
| Resistor descriptor creation | 4.5 ns/descriptor | 1000 descriptors |

Batch throughput: 71.7K MOSFET batches/s, 221.6K resistor batches/s. Pipeline
efficiency is 2.44 IPC with negligible overhead compared to physics models.
Descriptors are pure data containers -- construction cost is dominated by the
models that consume them.

## Testing

```bash
# Build tests
cd build/native-linux-debug
ninja sim_electronics_devices_descriptors_uTest

# Run tests
./bin/tests/sim_electronics_devices_descriptors_uTest
```

**Test coverage**: 27 tests covering construction, parameter ranges, and topology patterns.

## See Also

- **Linear Models**: `../linear/README.md` - Physics models using descriptors
- **Companions**: `../companions/README.md` - Transient integration wrappers
- **MNA Library**: `../../algorithms/mna/README.md` - Circuit solver

---

**Status**: Complete [OK] (Phase 2 of Layer 2 implementation)
