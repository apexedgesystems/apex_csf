# Composite Device Models

High-level circuit fragments built from primitive linear and nonlinear devices.
Part of Layer 2 (Device Models) in the electronics simulation library.

---

## Overview

Composite devices combine primitive devices (resistors, transistors, etc.) into
reusable circuit building blocks. Provides both logical truth tables and
transistor-level circuit simulation.

**Current implementations:**

- **CMOS Gates:** Inverter, NAND, NOR (built from MOSFETs)
- **Future:** Op-amp macromodels, flip-flops, registers, multiplexers

**RT-safety:** All models are RT-safe (static functions, no allocations).

---

## Models

### CmosInverter

CMOS NOT gate built from 1 PMOS (pull-up) + 1 NMOS (pull-down).

```cpp
#include "src/sim/electronics/devices/composite/inc/CmosInverter.hpp"

using namespace sim::electronics::devices::composite;

// Build inverter (2 MOSFETs)
CmosInverter inv{VDD, GND, INPUT, OUTPUT, 10e-6, 1e-6};

// Truth table (digital verification)
EXPECT_EQ(CmosInverter::truthTable(0), 1);  // NOT 0 = 1
EXPECT_EQ(CmosInverter::truthTable(1), 0);  // NOT 1 = 0
```

### CmosNand

CMOS NAND gate built from 2 PMOS (parallel pull-up) + 2 NMOS (series pull-down).

```cpp
#include "src/sim/electronics/devices/composite/inc/CmosNand.hpp"

using namespace sim::electronics::devices::composite;

// Build NAND gate (4 MOSFETs)
CmosNand nand{VDD, GND, INA, INB, OUTPUT, INTERNAL, 10e-6, 1e-6};

// Truth table (digital verification)
EXPECT_EQ(CmosNand::truthTable(0, 0), 1);
EXPECT_EQ(CmosNand::truthTable(0, 1), 1);
EXPECT_EQ(CmosNand::truthTable(1, 0), 1);
EXPECT_EQ(CmosNand::truthTable(1, 1), 0);  // Only 1,1 -> 0
```

### CmosNor

CMOS NOR gate built from 2 PMOS (series pull-up) + 2 NMOS (parallel pull-down).

```cpp
#include "src/sim/electronics/devices/composite/inc/CmosNor.hpp"

using namespace sim::electronics::devices::composite;

// Build NOR gate (4 MOSFETs)
CmosNor nor{VDD, GND, INA, INB, OUTPUT, INTERNAL, 10e-6, 1e-6};

// Truth table (digital verification)
EXPECT_EQ(CmosNor::truthTable(0, 0), 1);  // Only 0,0 -> 1
EXPECT_EQ(CmosNor::truthTable(0, 1), 0);
EXPECT_EQ(CmosNor::truthTable(1, 0), 0);
EXPECT_EQ(CmosNor::truthTable(1, 1), 0);
```

---

## Registry Header

Import all composite models at once:

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

using namespace sim::electronics::devices::composite;

// All models available:
CmosInverter inv{...};
CmosNand nand{...};
CmosNor nor{...};
```

---

## Usage Examples

### Digital Logic Verification

Validate gate behavior using truth tables:

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

void verifyNandGate() {
  // NAND truth table: OUT = ~(A & B)
  EXPECT_EQ(CmosNand::truthTable(0, 0), 1);
  EXPECT_EQ(CmosNand::truthTable(0, 1), 1);
  EXPECT_EQ(CmosNand::truthTable(1, 0), 1);
  EXPECT_EQ(CmosNand::truthTable(1, 1), 0);
}
```

### Building Complex Circuits

Compose gates into larger circuits:

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

void buildXorGate() {
  // XOR = (A NAND (A NAND B)) NAND (B NAND (A NAND B))

  const int A = 1, B = 0;

  const int nand_ab = CmosNand::truthTable(A, B);
  const int nand_a_nand_ab = CmosNand::truthTable(A, nand_ab);
  const int nand_b_nand_ab = CmosNand::truthTable(B, nand_ab);
  const int xor_out = CmosNand::truthTable(nand_a_nand_ab, nand_b_nand_ab);

  // XOR(1, 0) = 1
  EXPECT_EQ(xor_out, 1);
}
```

### Universal Gates

NAND and NOR are universal gates (can implement any logic function):

```cpp
// NOT from NAND:  OUT = A NAND A
CmosNand::truthTable(A, A) == CmosInverter::truthTable(A)

// AND from NAND:  OUT = (A NAND B) NAND (A NAND B)
const int nand = CmosNand::truthTable(A, B);
const int and_out = CmosNand::truthTable(nand, nand);

// OR from NAND:   OUT = (A NAND A) NAND (B NAND B)
const int not_a = CmosNand::truthTable(A, A);
const int not_b = CmosNand::truthTable(B, B);
const int or_out = CmosNand::truthTable(not_a, not_b);
```

---

## Dependencies

- `sim_electronics_devices_descriptors` - MosfetDescriptor topology
- `sim_electronics_devices_nonlinear` - MosfetLevel1 physics
- `sim_electronics_algorithms_mna` - MNA system for circuit simulation
- `utilities_compatibility` - C++17/20/23 compatibility

---

## Performance

| Operation              | Latency      | Scale                        |
| ---------------------- | ------------ | ---------------------------- |
| Truth table evaluation | 1.95 ns/gate | 3 gate types, 10K iterations |

Pipeline efficiency is 4.01 IPC (near theoretical maximum). Truth table
evaluation uses constexpr logic with no branching overhead.

## Testing

Run unit tests:

```bash
make compose-testp
# Tests: sim_electronics_devices_composite_uTest
```

**Test coverage:**

- Construction (net connectivity, geometry)
- Truth tables (all input combinations)
- Universal gates (NAND/NOR can implement all logic)

**27 tests total** (9 per gate: construction + truth table validation)

---

## See Also

- **Nonlinear Devices:** `../nonlinear/README.md` - MOSFET models
- **Descriptors:** `../descriptors/README.md` - Topology structures
- **MNA Library:** `../../algorithms/mna/README.md` - Circuit solver
