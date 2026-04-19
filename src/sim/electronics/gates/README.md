# Gates

**Namespace:** `sim::electronics::gates`
**Platform:** Cross-platform (constexpr, no runtime dependencies)
**C++ Standard:** C++23

User-facing logic gate truth tables for digital design verification. Wraps the
existing CMOS composite gates ([`devices/composite`](../devices/composite/README.md))
with a small constexpr API: NOT, AND, OR, NAND, NOR, XOR, XNOR, half-adder,
full-adder.

For circuit-level simulation of these gates with actual transistor physics,
use the underlying composite gates directly. For pure logic verification
(truth tables and boolean equivalence), this header is sufficient.

---

## API

```cpp
#include "src/sim/electronics/gates/inc/LogicGates.hpp"

using sim::electronics::gates::gateNot;
using sim::electronics::gates::gateAnd;
using sim::electronics::gates::gateOr;
using sim::electronics::gates::gateNand;
using sim::electronics::gates::gateNor;
using sim::electronics::gates::gateXor;
using sim::electronics::gates::gateXnor;
using sim::electronics::gates::halfAdder;
using sim::electronics::gates::fullAdder;

// Single-input
int y = gateNot(0);                  // 1

// Two-input
int and_out  = gateAnd(1, 1);        // 1
int or_out   = gateOr(0, 1);         // 1
int nand_out = gateNand(1, 1);       // 0
int nor_out  = gateNor(0, 0);        // 1
int xor_out  = gateXor(1, 0);        // 1
int xnor_out = gateXnor(1, 1);       // 1

// Adders
HalfAdderResult ha = halfAdder(1, 1);
// ha.sum = 0, ha.carry = 1

FullAdderResult fa = fullAdder(1, 1, 1);
// fa.sum = 1, fa.cout = 1
```

All functions are `constexpr` and `noexcept`. They allocate nothing, branch
predictably, and are safe to call from real-time contexts.

---

## Reference

| Function                               | Truth                                   |
| -------------------------------------- | --------------------------------------- |
| `int gateNot(int a)`                   | `~a & 1`                                |
| `int gateAnd(int a, int b)`            | `a & b`                                 |
| `int gateOr(int a, int b)`             | `a \| b`                                |
| `int gateNand(int a, int b)`           | `~(a & b) & 1`                          |
| `int gateNor(int a, int b)`            | `~(a \| b) & 1`                         |
| `int gateXor(int a, int b)`            | `a ^ b`                                 |
| `int gateXnor(int a, int b)`           | `~(a ^ b) & 1`                          |
| `HalfAdderResult halfAdder(a, b)`      | `{sum=a^b, carry=a&b}`                  |
| `FullAdderResult fullAdder(a, b, cin)` | `{sum=a^b^cin, cout=majority(a,b,cin)}` |

`HalfAdderResult` and `FullAdderResult` are POD structs with public `sum` /
`carry` / `cout` fields.

---

## Implementation Notes

The gate functions are not direct boolean operators -- each one routes
through the corresponding `CmosInverter::truthTable`, `CmosNand::truthTable`,
or `CmosNor::truthTable` from `devices/composite/`. This keeps the gate API
in lockstep with the underlying CMOS composite models so that any future
change to the composite truth table behavior is reflected in this header
without code duplication.

`gateXor` is built from `CmosNand::truthTable` only:

```
XOR(a, b) = NAND(NAND(a, NAND(a, b)), NAND(b, NAND(a, b)))
```

This is the standard 4-NAND XOR construction.

---

## Dependencies

| Library                             | Why                               |
| ----------------------------------- | --------------------------------- |
| `sim_electronics_devices_composite` | CmosInverter / CmosNand / CmosNor |

Header-only. No source files.

---

## Testing

```bash
make compose-debug
make compose-testp
ctest -R TestSimElectronicsGates
```

| Test                        | Coverage                 |
| --------------------------- | ------------------------ |
| `LogicGates.NotTruthTable`  | NOT (2 cases)            |
| `LogicGates.AndTruthTable`  | AND (4 cases)            |
| `LogicGates.OrTruthTable`   | OR (4 cases)             |
| `LogicGates.NandTruthTable` | NAND (4 cases)           |
| `LogicGates.NorTruthTable`  | NOR (4 cases)            |
| `LogicGates.XorTruthTable`  | XOR (4 cases)            |
| `LogicGates.XnorTruthTable` | XNOR (4 cases)           |
| `LogicGates.HalfAdder`      | All 4 input combinations |
| `LogicGates.FullAdder`      | All 8 input combinations |

---

## Performance

All functions are constexpr with zero allocations. Measured on x86_64
(Docker, Clang 21, debug build, 10K iterations per batch, 15 repeats):

| Operation  | Median | Per-unit    | CV%  |
| ---------- | ------ | ----------- | ---- |
| 7 gates    | 434 us | 6.2 ns/gate | 4.0% |
| Half adder | 210 us | 21.0 ns/op  | 1.8% |
| Full adder | 520 us | 52.0 ns/op  | 1.7% |

Full adder cost (~52 ns) reflects 2 half adders + OR gate, consistent with
the ~14 primitive gate evaluations in its decomposition. NAND is the dominant
primitive (33% of samples in gperftools profiling), as expected for the
NAND-based composition approach.

---

## Demo

```bash
./build/native-linux-debug/bin/ApexGatesDemo            # All truth tables + 5+3 demo
./build/native-linux-debug/bin/ApexGatesDemo --add 9 6  # 4-bit ripple-carry add
```

The demo also includes a 4-bit ripple-carry adder built from full-adders to
show how composing the primitives yields multi-bit arithmetic.

---

## See Also

- [devices/composite](../devices/composite/README.md) -- underlying CMOS gates
- [intel4004](../intel4004/README.md) -- a much larger PMOS gate-level model
- `apps/apex_gates_demo/` -- example consumer
