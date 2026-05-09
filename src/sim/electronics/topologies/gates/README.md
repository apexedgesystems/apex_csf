# Gates

**Namespace:** `sim::electronics::topologies::gates`
**Platform:** Cross-platform
**C++ Standard:** C++23

CMOS logic gates at two fidelity levels. The library exposes the **same set
of seven gates** (NOT, AND, OR, NAND, NOR, XOR, XNOR) two different ways
depending on what you need:

| Header                              | Fidelity              | Cost          | Use when |
|-------------------------------------|-----------------------|---------------|----------|
| `gates/inc/LogicGates.hpp`          | Boolean truth tables  | ~6 ns / gate  | Pure logic verification, design-time correctness, fast sweeps |
| `gates/inc/CmosGateCircuits.hpp`    | Transistor-level (MOSFET Level 1, MNA-solved) | ~us / gate | Real CMOS physics: actual output voltages, transfer characteristic, VOH / VOL / noise margin analysis |

Both share the same gate set so you can move from boolean verification down
to physics-based simulation without rebuilding your topology -- swap the
header and the function calls map onto the matching circuits.

---

## API: `LogicGates.hpp` (boolean tier)

```cpp
#include "src/sim/electronics/topologies/gates/inc/LogicGates.hpp"

using sim::electronics::topologies::gates::gateNot;
using sim::electronics::topologies::gates::gateAnd;
using sim::electronics::topologies::gates::gateOr;
using sim::electronics::topologies::gates::gateNand;
using sim::electronics::topologies::gates::gateNor;
using sim::electronics::topologies::gates::gateXor;
using sim::electronics::topologies::gates::gateXnor;
using sim::electronics::topologies::gates::halfAdder;
using sim::electronics::topologies::gates::fullAdder;

// Single / two input
int y       = gateNot(0);                  // 1
int and_out = gateAnd(1, 1);                // 1
int xor_out = gateXor(1, 0);                // 1

// Adders
HalfAdderResult ha = halfAdder(1, 1);       // {sum=0, carry=1}
FullAdderResult fa = fullAdder(1, 1, 1);    // {sum=1, cout=1}
```

All boolean functions are `constexpr` and `noexcept` -- safe to call from
real-time contexts. They route through the matching `CmosInverter::truthTable`,
`CmosNand::truthTable`, and `CmosNor::truthTable` from
[`devices/composite`](../../devices/composite/README.md), so the boolean
behavior stays in lockstep with the underlying CMOS composite primitives.

`gateXor` is built from NAND only (the standard 4-NAND XOR construction).

---

## API: `CmosGateCircuits.hpp` (transistor tier)

Each gate is a real CMOS circuit built from MOSFETs, solved through the MNA
infrastructure with `MosfetLevel1` (Shichman-Hodges) physics.

```cpp
#include "src/sim/electronics/topologies/gates/inc/CmosGateCircuits.hpp"

using sim::electronics::topologies::gates::CmosNandCircuit;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

const double VDD = 5.0, W = 10e-6, L = 1e-6;
MosfetLevel1Params nmos{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};
MosfetLevel1Params pmos{.Kp = 60e-6,  .Vth = 0.7, .lambda = 0.02};

CmosNandCircuit nandGate(VDD, W, L, nmos, pmos);
nandGate.build();

nandGate.setInputs(5.0, 5.0);
double vOut = nandGate.computeDC();   // ~0.0 V (low) -- both inputs high
nandGate.setInputs(0.0, 5.0);
       vOut = nandGate.computeDC();   // ~5.0 V (high)
```

Available circuits and transistor counts:

| Class                | Gate | MOSFETs |
|----------------------|------|--------:|
| `CmosInverterCircuit`| NOT  |       2 |
| `CmosNandCircuit`    | NAND |       4 |
| `CmosNorCircuit`     | NOR  |       4 |
| `CmosAndCircuit`     | AND  |       6 |
| `CmosOrCircuit`      | OR   |       6 |
| `CmosXorCircuit`     | XOR  |      16 |
| `CmosXnorCircuit`    | XNOR |      18 |

These circuits are NOT RT-safe (the Newton-Raphson solver allocates during
`computeDC()`). They produce real output voltages -- not boolean 0/1 -- so
you can probe noise margin, pull-up / pull-down strength, and switching
threshold under arbitrary `MosfetLevel1Params`.

---

## Reference: boolean truth functions

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

## Dependencies

| Library                             | Why                                             |
| ----------------------------------- | ----------------------------------------------- |
| `sim_electronics_devices_composite` | CmosInverter / CmosNand / CmosNor primitives    |
| `sim_electronics_devices_nonlinear` | `MosfetLevel1` for transistor-level circuits    |
| `sim_electronics_circuit`           | Circuit composition API (transistor tier only)  |
| `sim_electronics_algorithms_mna`               | MNA solver (transistor tier only)               |

Header-only.

---

## Testing

```bash
make compose-debug
make compose-testp
ctest -R TestSimElectronicsGates
```

| Test                                  | Coverage                                          |
| ------------------------------------- | ------------------------------------------------- |
| `LogicGates.NotTruthTable` ... `Xnor` | Boolean tier truth tables (all input combinations)|
| `LogicGates.HalfAdder`                | Half adder, all 4 cases                           |
| `LogicGates.FullAdder`                | Full adder, all 8 cases                           |
| `CmosGateCircuits.*`                  | Transistor-tier voltage tables and DC convergence |

---

## Performance

Boolean tier (constexpr, zero allocations, x86_64, Clang 21, debug):

| Operation  | Median  | Per-unit    | CV%  |
| ---------- | ------- | ----------- | ---- |
| 7 gates    | 434 us  | 6.2 ns/gate | 4.0% |
| Half adder | 210 us  | 21.0 ns/op  | 1.8% |
| Full adder | 520 us  | 52.0 ns/op  | 1.7% |

Transistor tier costs depend on the gate's transistor count and the
solver's NR iteration budget; expect microseconds per gate for DC solves.

---

## Demo

The `apex_circuit_demo` app exercises the transistor tier via
`--circuit gates` (default) -- printing voltage truth tables for all seven
gates with real `MosfetLevel1` physics:

```bash
./build/native-linux-debug/bin/ApexCircuitDemo                # gates default
./build/native-linux-debug/bin/ApexCircuitDemo --circuit gates
```

The boolean tier is exercised by the unit test suite directly; it's also a
suitable building block for a customer integrating a digital design check
without paying the transistor solve cost.

---

## See also

- [`devices/composite`](../../devices/composite/README.md) -- CMOS composite primitives the boolean tier wraps
- [`devices/nonlinear`](../../devices/nonlinear/README.md) -- MOSFET models the transistor tier consumes
- [`intel4004/grid`](../../intel4004/grid/README.md) -- a much larger PMOS gate-level model (the entire 4004 chip)
- `apps/apex_circuit_demo/` -- example consumer
