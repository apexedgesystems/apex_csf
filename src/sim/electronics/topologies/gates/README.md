# Gates Module

**Namespace:** `sim::electronics::topologies::gates`
**Platform:** Cross-platform
**C++ Standard:** C++23

CMOS logic gates at two fidelity tiers: a constexpr boolean tier for
truth-table verification, and a transistor-level tier that solves real
CMOS circuits through the MNA solver with `MosfetLevel1` physics.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [LogicGates (boolean tier)](#logicgates-boolean-tier)
   - [CmosGateCircuits (transistor tier)](#cmosgatecircuits-transistor-tier)
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: NAND DC Sweep](#8-example-nand-dc-sweep)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                     | Header / API                                                   |
| -------------------------------------------- | -------------------------------------------------------------- |
| How do I evaluate a gate's truth table?      | `LogicGates.hpp` (`gateNot` / `gateAnd` / ...)                 |
| How do I solve a gate's real output voltage? | `CmosGateCircuits.hpp` (`CmosInverterCircuit::computeDC` etc.) |
| How do I build a half adder?                 | `halfAdder(a, b)`                                              |
| How do I build a full adder?                 | `fullAdder(a, b, cin)`                                         |
| How do I probe VOH / VOL / noise margin?     | Transistor tier with `MosfetLevel1Params`                      |

Both tiers expose the same seven gates (NOT, AND, OR, NAND, NOR, XOR,
XNOR). Boolean and transistor results stay in lockstep because the
boolean tier routes through the matching `CmosInverter::truthTable`,
`CmosNand::truthTable`, and `CmosNor::truthTable` from
[devices/composite](../../devices/composite/README.md).

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/topologies/gates/inc/LogicGates.hpp"
#include "src/sim/electronics/topologies/gates/inc/CmosGateCircuits.hpp"

using namespace sim::electronics::topologies::gates;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

// Boolean tier
const int Y = gateXor(/*a=*/1, /*b=*/0);

// Transistor tier
CmosNandCircuit nandGate(/*vdd=*/5.0, /*W=*/10e-6, /*L=*/1e-6,
                         MosfetLevel1Params{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02},
                         MosfetLevel1Params{.Kp =  60e-6, .Vth = 0.7, .lambda = 0.02});
nandGate.build();
nandGate.setInputs(5.0, 5.0);
const double V_OUT = nandGate.computeDC();
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Two tiers, one gate set.** Boolean and transistor implementations are
  interchangeable from the caller's point of view -- swap the include
  and the API stays parallel.
- **Boolean tier is `constexpr`.** Every function is `constexpr` and
  `noexcept`, safe in any context including templates and `static_assert`.
- **Transistor tier exposes physics.** Output is a real voltage so
  callers can probe noise margin, switching threshold, and pull
  strength under arbitrary device parameters.

---

## 4. Module Reference

### LogicGates (boolean tier)

**Header:** `LogicGates.hpp`
**Purpose:** Constexpr boolean truth functions for the seven gates plus
half / full adders.

#### API

```cpp
[[nodiscard]] constexpr int gateNot(int a) noexcept;
[[nodiscard]] constexpr int gateAnd(int a, int b) noexcept;
[[nodiscard]] constexpr int gateOr (int a, int b) noexcept;
[[nodiscard]] constexpr int gateNand(int a, int b) noexcept;
[[nodiscard]] constexpr int gateNor (int a, int b) noexcept;
[[nodiscard]] constexpr int gateXor (int a, int b) noexcept;
[[nodiscard]] constexpr int gateXnor(int a, int b) noexcept;

struct HalfAdderResult { int sum; int carry; };
struct FullAdderResult { int sum; int cout;  };

[[nodiscard]] constexpr HalfAdderResult halfAdder(int a, int b) noexcept;
[[nodiscard]] constexpr FullAdderResult fullAdder(int a, int b, int cin) noexcept;
```

| Function             | Truth                                       |
| -------------------- | ------------------------------------------- |
| `gateNot(a)`         | `~a & 1`                                    |
| `gateAnd(a,b)`       | `a & b`                                     |
| `gateOr(a,b)`        | <code>a &#124; b</code>                     |
| `gateNand(a,b)`      | `~(a & b) & 1`                              |
| `gateNor(a,b)`       | <code>~(a &#124; b) & 1</code>              |
| `gateXor(a,b)`       | `a ^ b` (4-NAND construction)               |
| `gateXnor(a,b)`      | `~(a ^ b) & 1`                              |
| `halfAdder(a,b)`     | `{sum = a^b, carry = a&b}`                  |
| `fullAdder(a,b,cin)` | `{sum = a^b^cin, cout = majority(a,b,cin)}` |

### CmosGateCircuits (transistor tier)

**Header:** `CmosGateCircuits.hpp`
**Purpose:** MNA-solved CMOS gate circuits at `MosfetLevel1` physics.

#### Available Circuits

| Class                 | Gate | MOSFETs |
| --------------------- | ---- | ------- |
| `CmosInverterCircuit` | NOT  | 2       |
| `CmosNandCircuit`     | NAND | 4       |
| `CmosNorCircuit`      | NOR  | 4       |
| `CmosAndCircuit`      | AND  | 6       |
| `CmosOrCircuit`       | OR   | 6       |
| `CmosXorCircuit`      | XOR  | 16      |
| `CmosXnorCircuit`     | XNOR | 18      |

#### API

```cpp
explicit CmosXxxCircuit(double vdd, double width, double length,
                        const MosfetLevel1Params& nmos,
                        const MosfetLevel1Params& pmos);

void build();
void setInputs(double vA);           // single-input gates
void setInputs(double vA, double vB); // two-input gates

[[nodiscard]] double computeDC();
```

`build` allocates the circuit; `computeDC` runs a Newton-Raphson solve
and returns the output voltage. NOT RT-safe because of the iterative
solve.

---

## 5. Common Patterns

### Boolean Verification

```cpp
static_assert(gateXor(1, 1) == 0);
static_assert(halfAdder(1, 1).carry == 1);
```

### Transistor DC Sweep

```cpp
CmosInverterCircuit inv(VDD, W, L, nmos, pmos);
inv.build();
for (double vin = 0.0; vin <= VDD; vin += 0.1) {
  inv.setInputs(vin);
  const double V_OUT = inv.computeDC();
  fmt::print("{:.2f} -> {:.4f}\n", vin, V_OUT);
}
```

### Switching-Threshold Probe

```cpp
CmosNorCircuit gate(VDD, W, L, nmos, pmos);
gate.build();
gate.setInputs(VDD / 2.0, VDD / 2.0);
const double V_THRESH = gate.computeDC();
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- Every boolean-tier function: all are `constexpr` `noexcept` with no
  allocation.

### NOT RT-Safe Functions

- Every transistor-tier `build` / `computeDC` call. The Newton-Raphson
  solver allocates workspace and runs an unbounded iteration count.

### Recommended Configuration

- Use the boolean tier inside RT loops for digital design checks.
- Run transistor-tier sweeps offline (verification, characterization).

---

## 7. CLI Tools

- `apex_circuit_demo --circuit gates` exercises the transistor tier and
  prints DC voltage tables for all seven gates. See
  [demos/apex_circuit_demo](../../../../demos/apex_circuit_demo/).

---

## 8. Example: NAND DC Sweep

```cpp
#include "src/sim/electronics/topologies/gates/inc/CmosGateCircuits.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics;

  topologies::gates::CmosNandCircuit nandGate(
      /*vdd=*/5.0, /*W=*/10e-6, /*L=*/1e-6,
      devices::nonlinear::MosfetLevel1Params{
          .Kp = 120e-6, .Vth = 0.7, .lambda = 0.02},
      devices::nonlinear::MosfetLevel1Params{
          .Kp =  60e-6, .Vth = 0.7, .lambda = 0.02});

  nandGate.build();
  for (const double VB : {0.0, 5.0}) {
    for (const double VA : {0.0, 5.0}) {
      nandGate.setInputs(VA, VB);
      fmt::print("a={:.1f} b={:.1f}  v(out) = {:.4f}\n", VA, VB, nandGate.computeDC());
    }
  }
  return 0;
}
```

---

## 9. See Also

- [Composite devices](../../devices/composite/README.md) - CMOS composite primitives the boolean tier wraps
- [Nonlinear devices](../../devices/nonlinear/README.md) - MOSFET models the transistor tier consumes
- [Intel 4004 grid](../../chips/intel4004/README.md) - Larger PMOS gate-level model (the entire 4004 chip)
- [demos/apex_circuit_demo](../../../../demos/apex_circuit_demo/)
