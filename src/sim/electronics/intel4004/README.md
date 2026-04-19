# Intel 4004

**Namespace:** `sim::electronics::intel4004`
**Platform:** Linux-only
**C++ Standard:** C++23

Intel 4004 microprocessor model at two fidelity levels:
**L0 behavioral CPU** (functional reference) and **L1 component hybrid**
(transistor-level visualization with ngspice-validated physics on the
NOR-gate path).

The 1971 4004 was the first commercially available microprocessor:
2,242 PMOS transistors, 16 4-bit registers, 46 instructions, 12-bit address
space. The full SPICE netlist (`netlist/data/lajos-4004.spice`) is the
Kintli/Silverman reverse-engineered transistor topology.

---

## Levels at a Glance

| Level | Where                           | Speed               | What you get                               |
| ----- | ------------------------------- | ------------------- | ------------------------------------------ |
| L0    | `Intel4004Cpu` (`behavioral/`)  | ~1 us / instruction | Final ACC, registers, carry, stack         |
| L1    | `Intel4004GridLevel1` (`grid/`) | ~1-2 s / byte       | Per-byte transistor voltages on 1,081 nets |

**L0** is the functional gold reference: pure C++ that executes all 46
opcodes against the MCS-4 specification. Use it for software emulation,
program development, and verifying L1 results.

**L1** is the transistor-level visualization: 1,305 NOR gates use Level 1
Shichman-Hodges physics (calibrated W/L bins, validated 0.0000V vs ngspice
per gate); 222 pass gates and 610 dynamic storage transistors use the binary
switch model; 105 standalone loads stamp as resistive `G_LOAD`. Behavioral
timing injection drives the clock generator and the 8 machine-state nets.

For multi-instruction programs L1 uses the **L0/L1 hybrid pattern**:
L0 is authoritative for instruction state, and a fresh L1 transistor circuit
is built per byte (seeded from L0's prior ACC via `forceAccLogic`) so each
byte's per-transistor voltage trace can be observed without inter-byte drift.
W/L parameters and calibration methodology are documented in the header
comments of `Intel4004GridLevel1.hpp`.

---

## Sub-libraries

| Library                                | Header                               | Purpose                                        |
| -------------------------------------- | ------------------------------------ | ---------------------------------------------- |
| `sim_electronics_intel4004_netlist`    | `netlist/inc/SpiceNetlistParser.hpp` | Parse `lajos-4004.spice` -> `Intel4004Netlist` |
| `sim_electronics_intel4004_behavioral` | `behavioral/inc/Intel4004Cpu.hpp`    | L0 behavioral CPU (46 opcodes)                 |
| `sim_electronics_intel4004_grid`       | `grid/inc/Intel4004GridLevel1.hpp`   | L1 transistor circuit + binary-switch base     |
| `sim_electronics_intel4004_gate`       | `gate/inc/Intel4004GateLevel.hpp`    | NOR/pass gate topology extraction (used by L1) |

All libraries are header-only.

---

## L0: Behavioral CPU

```cpp
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Instructions.hpp"

using sim::electronics::intel4004::Intel4004Cpu;
using sim::electronics::intel4004::encodeLDM;
using sim::electronics::intel4004::NOP;

const std::uint8_t PROG[] = {encodeLDM(5), NOP, encodeLDM(3)};

Intel4004Cpu cpu;
cpu.loadProgram(PROG, sizeof(PROG));
cpu.step();                       // LDM 5
cpu.step();                       // NOP
cpu.step();                       // LDM 3

assert(cpu.accumulator == 3);
```

The behavioral CPU exposes its full state directly: `registers[0..15]`,
`accumulator`, `carry`, `pc`, `stack[0..2]`, `ramData`, `ramStatus`,
`ramOutput`, `srcAddress`, `ramBank`, `testPin`. There is no encapsulation
because it is intended as a reference, not as a runtime dependency.

**RT-safety:** RT-safe after `loadProgram()`. `step()` and `run()` are pure
computation.

### Programs

`Intel4004Programs.hpp` provides constexpr test programs (`PROGRAM_LDM`,
`PROGRAM_ADD`, `PROGRAM_SUB`, `PROGRAM_ACC_OPS`, `PROGRAM_SUBROUTINE`).
`Intel4004Instructions.hpp` provides constexpr opcode encoders (`NOP`,
`encodeLDM`, `encodeFIM`, `encodeLD`, etc.).

---

## L1: Component Hybrid

```cpp
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

using sim::electronics::intel4004::Intel4004GridLevel1;
using sim::electronics::intel4004::loadSpiceNetlist;

constexpr std::size_t WARMUP_NOPS = 16;

// 16 NOPs followed by LDM 5
std::vector<std::uint8_t> rom(WARMUP_NOPS + 1, 0x00);
rom.back() = 0xD5;

const auto NETLIST = loadSpiceNetlist("path/to/lajos-4004.spice");

Intel4004GridLevel1 grid;
auto circuit = grid.buildCircuit(NETLIST);
grid.bsParams_.vth = 1.17;     // Match Level 1 enhancement threshold
grid.gminTransient_ = 1e-9;    // SPICE convergence aid

// Binary-switch warmup, then trace the LDM byte at L1 fidelity
auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(),
                                  WARMUP_NOPS, /*programBytes=*/0);

grid.enableSparseModeLevel1(circuit);
circuit.solver().invalidateCache();
grid.traceExecuteByte(circuit, state, 0xD5, /*onPhase=*/nullptr);

std::uint8_t acc = grid.readAccumulator(state.nodeVoltages);
// acc == 5
```

For multi-byte programs, build a **fresh** `Intel4004GridLevel1` per byte and
seed the ACC nets from L0's prior state via `forceAccLogic`. This is the
production pattern used by `Intel4004L1.MultiInstructionLdmNopLdm` and by the
`ApexCpuSimDemo` app.

### Why a hybrid?

Pure transistor-level multi-instruction execution is structurally blocked
on the 4004 even in ngspice with depletion-load positive VTO (proven during
calibration). The binary-switch sub-model is too coarse to maintain charge
on dynamic ACC nodes between instructions, and the Level 1 model alone
diverges in NR on the full 2,242-transistor circuit. The component hybrid
splits the netlist by topology:

- **NOR gates (1,305)** -- physics-modeled with calibrated Level 1, gives
  the per-transistor voltage trace users actually want to see.
- **Pass gates (222)** + **dynamic storage (610)** -- binary switch.
  Behaves correctly within a single byte where charge retention is bounded.
- **Standalone loads (105)** -- resistive `G_LOAD`.
- **Clocks + 8 state nets + latch controls** -- behavioral injection.

End-to-end validation: `Level1Physics_uTest` compares the L1 NOR gate stamp
0.0000V vs ngspice per gate. `Intel4004L1` (in `grid/dtst/`) runs the full
L0/L1 hybrid for `LDM 5`, `LDM 5; NOP; LDM 3` and verifies L0 ACC matches
the expected sequence and L1 produces no NaN/Inf.

**RT-safety:** Construction allocates. Time stepping inside an instruction is
safe (cached LU, bounded NR iterations).

---

## Tests

| Target                                  | Type | Coverage                                                                         |
| --------------------------------------- | ---- | -------------------------------------------------------------------------------- |
| `TestSimElectronicsIntel4004Behavioral` | utst | All 46 opcodes, register file, stack, carry                                      |
| `TestSimElectronicsIntel4004Netlist`    | utst | SPICE netlist parser vs `lajos-4004.spice` fixture                               |
| `TestSimElectronicsIntel4004Grid`       | utst | Level 1 stamp vs ngspice (0.0000V), component classifier, NR sub-region behavior |
| `TestSimElectronicsIntel4004Gate`       | utst | Gate extraction, NOR evaluation, propagation, accessors, execution               |
| `Intel4004Grid_Dev`                     | dtst | L0 + L1 multi-instruction integration (60-70s/test)                              |
| `Intel4004GateLevel_Dev`                | dtst | Gate-level execution comparison vs L0                                            |
| `Intel4004Gate_PTEST`                   | ptst | Gate extraction and propagation throughput                                       |

```bash
make compose-debug
make compose-testp                                 # All utsts
ctest -L intel4004                                 # Just intel4004 tests
ctest --test-dir build/native-linux-debug -R Intel4004
```

---

## Demo

```bash
./build/native-linux-debug/bin/ApexCpuSimDemo                       # L0 + L1, default LDM 5
./build/native-linux-debug/bin/ApexCpuSimDemo --level 0              # L0 only (fast)
./build/native-linux-debug/bin/ApexCpuSimDemo --level 1              # L0 + L1 transistor sim
./build/native-linux-debug/bin/ApexCpuSimDemo --program "D5 00 D3"   # Multi-instruction
./build/native-linux-debug/bin/ApexCpuSimDemo --probe ACC.0 --probe D0
```

The demo prints L0 and L1 ACC for every byte, with a final per-net voltage
table from the last byte's L1 state.

---

## Dependencies

| Library                                | Used by                                |
| -------------------------------------- | -------------------------------------- |
| `sim_electronics_circuit`              | `grid/`, `gate/`                       |
| `sim_electronics_devices_composite`    | `grid/`                                |
| `sim_electronics_devices_nonlinear`    | `grid/` (MosfetLevel1)                 |
| `sim_electronics_intel4004_netlist`    | `grid/`, `gate/`                       |
| `sim_electronics_intel4004_behavioral` | `grid/` (for behavioral state passing) |

---

## Performance

### Netlist Parser

| Metric            | Value                 |
| ----------------- | --------------------- |
| Parse throughput  | ~275 parses/s (debug) |
| Per-parse latency | ~3.6 ms (2,242 tx)    |
| CV%               | 3.8%                  |
| IPC               | 2.92                  |
| Branch miss rate  | 0.44%                 |

The netlist parser is an initialization-only routine (runs once at startup).
Time is dominated by `std::set<string>` insertion for unique net collection
and `std::istringstream` tokenization. No optimization warranted.

### Behavioral CPU

| Metric             | Value                       |
| ------------------ | --------------------------- |
| Step throughput    | ~7.7M steps/s (NOP, debug)  |
| Step latency       | ~16 ns/step (NOP)           |
| Program throughput | ~131K runs/s (17-instr ISZ) |
| Per-instruction    | ~45 ns/instr (with reset)   |
| CV%                | 1.5% (step), 2.3% (program) |
| IPC                | 3.41                        |
| Branch miss rate   | 0.69%                       |

The behavioral CPU is pure computation after `loadProgram()`. Per-instruction
cost is dominated by the switch-case decode in `step()`. The 3.41 IPC and
0.69% branch miss rate confirm near-optimal dispatch. No optimization warranted.

### Grid (Transistor-Level L1)

| Metric               | Value                            |
| -------------------- | -------------------------------- |
| Circuit build        | ~1.2 ms (2,242 tx, ~1,081 nets)  |
| L1 single-byte       | ~36 s (16 NOP warm-up + 1 L1)    |
| DC solve (stamp+KLU) | ~3.0 ms (stamp 2242 tx + factor) |
| CV%                  | 2.0-2.9%                         |
| IPC                  | 2.49                             |
| Branch miss rate     | 0.41%                            |

Circuit build is an initialization-only cost (hash map insertion for ~1,081
net names). The L1 per-byte cost is structural: 17 bytes x 8 machine states
x 4 clock phases x 10-20 sub-steps, each requiring a full KLU sparse
factorize+solve on a 1,081-net matrix. The 2.49 IPC and 0.41% branch miss
rate confirm near-optimal execution of the stamp loop. KLU (SuiteSparse)
dominates the per-solve cost. No optimization warranted.

---

## See Also

- [Circuit](../circuit/README.md) -- construction API
- [devices/nonlinear](../devices/nonlinear/README.md) -- MosfetLevel1
- [algorithms/spice/ngspice](../algorithms/spice/ngspice/README.md) -- ngspice integration used for L1 validation
- `apps/apex_cpu_sim_demo/` -- end-to-end demo
