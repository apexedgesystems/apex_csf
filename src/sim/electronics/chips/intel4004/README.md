# Intel 4004

**Namespace:** `sim::electronics::chips::intel4004`
**Platform:** Linux-only
**C++ Standard:** C++23

Intel 4004 microprocessor model at **four fidelity levels** with explicit
authority models and clear physics fractions per level. Each level has a
documented scope and use case; lower levels remain useful for fast
verification when higher levels are too expensive.

The 1971 4004 was the first commercially available microprocessor:
2,242 PMOS transistors, 16 4-bit registers, 46 instructions, 12-bit address
space. The full SPICE netlist (`netlist/data/lajos-4004.spice`) is the
Kintli / Silverman reverse-engineered transistor topology.

---

## Levels at a Glance

| Level  | Name               | Where                           | Authority                                 | Speed                  |
| ------ | ------------------ | ------------------------------- | ----------------------------------------- | ---------------------- |
| **L0** | Functional CPU     | `Intel4004Cpu` (`behavioral/`)  | self (ISA truth)                          | ~1 us / instruction    |
| **L1** | Component Hybrid   | `Intel4004GridLevel1` (`grid/`) | L0-authoritative                          | ~1-2 s / byte          |
| **L2** | Engineered Physics | `Intel4004GridLevel2` (`grid/`) | physics-authoritative + custom primitives | minutes / byte         |
| **L3** | Pure Physics       | (future aspiration)             | physics-authoritative pure                | tens of minutes / byte |

### Authority models -- the key distinction

- **L0** is its own authority -- pure C++ ISA simulation against the MCS-4 spec.
- **L1** is **L0-authoritative**: the chip's transistor voltages are
  visualized, but register state (ACC, OPR, OPA) is forced from L0's truth
  via 5 _behavioral stubs_ during specific phases. The transistor view is
  faithful for _what L0 computed_; physics does not drive register state.
- **L2** is **physics-authoritative**: register state emerges from physics.
  Some 4004 subcircuits -- the cross-coupled latch core, the 3-stage
  Vth-drop cascades for OPR / OPA capture, the multi-stage ALU writeback
  chain -- do not converge in our NR solver (and do not converge in stock
  ngspice either on the full chip from t=0). For those, a **custom
  primitive** reads physics-driven inputs (D-bus voltages, OPR / OPA bits,
  ACC, register file, CY, decode signals), computes the deterministic
  operation the chip's design dictates, and writes the result onto the
  storage nets. Primitive inputs and outputs remain physics-driven; only
  the non-converging interior is abstracted. Any net read or written by a
  primitive carries its uncertainty into the result, and any subcircuit not
  on a primitive's read / write list is fully physics-driven.
- **L3** is **pure physics**: no abstractions. Currently a future aspiration
  -- requires multi-week to multi-month work on body-effect Vth, finer
  integration, and possibly chip-specific MOSFET variants.

---

## Sub-libraries

| Library                                      | Header                                                                   | Purpose                                        |
| -------------------------------------------- | ------------------------------------------------------------------------ | ---------------------------------------------- |
| `sim_electronics_chips_intel4004_netlist`    | `netlist/inc/SpiceNetlistParser.hpp`                                     | Parse `lajos-4004.spice` -> `Intel4004Netlist` |
| `sim_electronics_chips_intel4004_behavioral` | `behavioral/inc/Intel4004Cpu.hpp`                                        | L0 functional CPU (46 opcodes)                 |
| `sim_electronics_chips_intel4004_grid`       | `grid/inc/Intel4004GridLevel1.hpp`<br>`grid/inc/Intel4004GridLevel2.hpp` | L1 component hybrid + L2 engineered physics    |
| `sim_electronics_chips_intel4004_gate`       | `gate/inc/Intel4004GateLevel.hpp`                                        | NOR/pass gate topology extraction (used by L1) |

All libraries are header-only. L2 inherits from L1 (overriding only the
latch-feedback stamp + adding custom primitives); L1 inherits from a
common `Intel4004Grid` base.

---

## L0: Functional CPU

```cpp
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Instructions.hpp"

using sim::electronics::chips::intel4004::Intel4004Cpu;
using sim::electronics::chips::intel4004::encodeLDM;
using sim::electronics::chips::intel4004::NOP;

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

---

## L1: Component Hybrid

L1 is the **fast transistor-visualization** layer. The chip's NOR gates run
calibrated Level 1 Shichman-Hodges physics (validated 0.0000V vs ngspice
per gate), pass gates and dynamic storage use the binary switch model, and
**5 behavioral stubs inject L0's truth** into the circuit during specific
machine-state phases.

### L1's 5 behavioral stubs (the 15% L0-authoritative fraction)

| Stub                  | What it forces                                                                     | When                |
| --------------------- | ---------------------------------------------------------------------------------- | ------------------- |
| Dynamic storage latch | Sample-and-hold voltages on dynamic nets                                           | per-byte            |
| OPR sample (M1)       | Forces OPR.0..3 from `dataBusDrive_`                                               | machineState\_ == 3 |
| OPA sample (M2)       | Forces OPA.0..3 from `dataBusDrive_`                                               | machineState\_ == 4 |
| ACC write (X3)        | Behaviorally executes the instruction (LDM/IAC/CMA/ADD/SUB/LD/etc.) and writes ACC | end of X3           |
| Register seed         | Seeds R0-R15 from L0 at byte boundaries                                            | byte boundary       |

These stubs are why L1 works for all 46 instructions: L0 is computing the
truth, L1 is making the transistors look right.

### Usage

```cpp
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

using sim::electronics::chips::intel4004::Intel4004GridLevel1;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

constexpr std::size_t WARMUP_NOPS = 16;

std::vector<std::uint8_t> rom(WARMUP_NOPS + 1, 0x00);
rom.back() = 0xD5;

const auto NETLIST = loadSpiceNetlist("path/to/lajos-4004.spice");

Intel4004GridLevel1 grid;
auto circuit = grid.buildCircuit(NETLIST);
grid.bsParams_.vth = 1.17;
grid.gminTransient_ = 1e-9;

auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(),
                                  WARMUP_NOPS, /*programBytes=*/0);

grid.enableSparseModeLevel1(circuit);
circuit.solver().invalidateCache();
grid.traceExecuteByte(circuit, state, 0xD5, /*onPhase=*/nullptr);

std::uint8_t acc = grid.readAccumulator(state.nodeVoltages);  // == 5
```

For multi-byte programs L1 builds a fresh circuit per byte and seeds ACC
from L0's prior state via `forceAccLogic`.

**RT-safety:** Construction allocates. Time stepping inside an instruction is
safe (cached LU, bounded NR iterations).

---

## L2: Engineered Physics

L2 is **physics-authoritative**: every transistor runs a real device
model (Level 1 Shichman-Hodges or BSIM3), so the chip's analog circuits
actually evolve during each cycle instead of being pinned to L0 truth.
At specific event boundaries (M1/M2 capture, X3 writeback) custom
primitives read physics-driven inputs (D-bus voltages, OPR/OPA bits,
ACC, register file, CY, PC nets) and write the deterministic chip-
design result back onto storage nets. Four kinds of issue forced
primitive abstractions:

1. **Cross-coupled latch core** (~338 transistors, the dynamic-storage
   feedback). Shichman-Hodges Level 1 produces a stable mid-rail
   equilibrium in PMOS depletion-load NOR feedback (ngspice does the
   same). Decision: swap in BSIM3 weak-inversion (`Vgst_eff`,
   `n_factor=2.5`) for the 338 latch transistors specifically. Cells
   match ngspice within ~100 mV per atomic-cell tests. **This is the
   only category that's physics-replaced -- every latch transistor is
   now stamped, no behavioral overlay.**

2. **OPR/OPA capture cascades** (3-stage pass-transistor chains: D-bus
   -> N101x -> N099x -> cross-coupled cell -> OPR.x / OPA.x). Cumulative
   Vth-drop in the cascade prevents convergence in our solver. Real
   silicon overcomes this with bootstrap caps that boost gate above
   VDD. Decision: `OprCaptureCell` / `OpaCaptureCell` primitives --
   sample physics D-bus during the capture window, force the captured
   value onto OPR.x / OPA.x, hold via `latchValues_` voltage-source
   stamping through the M1->M2->X3 window.

3. **ALU + writeback chain** (the actual instruction effect -- ACC =
   ALU result, register-file writes, PC writes for jumps, stack
   pushes). The chip's downstream decode signals can't be trusted in
   our solver, and the multi-stage writeback chain doesn't converge.
   Decision: dispatch on physics-captured OPR + OPA bits, apply the
   chip's deterministic operation, write the result. Three primitives:

   - `LdmAccWriteback` for LDM/BBL
   - `AluWriteback` for the full ACC group (IAC/CMA/CLB/CLC/CMC/STC/
     RAL/RAR/TCC/TCS/DAA/DAC/KBP/DCL) and register-operand ops (ADD/
     SUB/LD/XCH)
   - `RegPcWriteback` for INC/SRC/JIN/BBL

4. **External-chip state and 2-byte sequencing** -- scope, not
   convergence. The 4004 talks to external 4002 RAM and 4001 ROM
   chips; we don't simulate those, so a primitive maintains parallel
   `ramData_`/`ramStatus_`/`ramOutput_` arrays mirroring L0's
   structure. Similarly, 2-byte instructions (FIM/JCN/JUN/JMS/ISZ)
   need cross-byte primitive state -- `pendingTwoByteOpr_` records
   which 2-byte op is in flight so the data byte's nibbles aren't
   misinterpreted as a new opcode. The `TwoByteAndRamWriteback`
   primitive handles all of these plus FIN (which reads ROM via the
   cached `romBuffer_`).

Implication: any net read or written by a primitive carries its
uncertainty into the result; any subcircuit not on a primitive's
read/write list is fully physics-driven.

### Primitives

L2 disables every L1 behavioral stub. The cross-coupled latch core runs
BSIM3, and six custom primitives handle capture and writeback:

- `OprCaptureCell` -- samples physics-driven D-bus into OPR at M1
- `OpaCaptureCell` -- samples physics-driven D-bus into OPA at M2
- `LdmAccWriteback` -- writes ACC = OPA at X3 for LDM/BBL
- `AluWriteback` -- dispatches the ACC-modifying ops (IAC, CMA, ADD,
  SUB, LD, XCH, CLB, CLC, CMC, STC, TCC, TCS, RAL, RAR, DAA, DAC,
  DCL, KBP) on physics-captured OPR + OPA at X3
- `RegPcWriteback` -- dispatches register-file + PC ops (INC, SRC,
  JIN, BBL) on physics-captured OPR + OPA at X3
- `TwoByteAndRamWriteback` -- handles the 2-byte instructions
  (FIM/JCN/JUN/JMS/ISZ via cross-byte primitive state), FIN (reads
  ROM via cached `romBuffer_`), and the RAM/IO group (WRM/WMP/WRR/
  WPM/WR0-3/SBM/RDM/RDR/ADM/RD0-3 against parallel
  `ramData_`/`ramStatus_`/`ramOutput_` arrays)

The capture primitives use `latchValues_` voltage-source stamping to
hold captured values across the M1->M2->X3 window. The writeback
primitives dispatch on captured OPR + OPA rather than on the chip's
downstream decode nets, because those decode cascades don't converge
reliably for non-LDM opcodes in our solver. When a 2-byte op is in
flight, all other primitives suppress themselves on the data byte
(via the `pendingTwoByteOpr_` gate) so the data byte's nibbles aren't
mistakenly treated as a new opcode.

L2 reproduces L0 state -- ACC, CY, the sixteen registers, RAM data
across banks, RAM status, RAM output, srcAddress, ramBank, and post-jump
PC -- at every L0 step boundary, including JMS / BBL stack handling,
ISZ backward-jump loops, DCL bank switching, and BCD addition through
DAA.

### Usage

```cpp
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel2.hpp"

using sim::electronics::chips::intel4004::Intel4004GridLevel2;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

const auto NETLIST = loadSpiceNetlist("path/to/lajos-4004.spice");
constexpr std::size_t WARMUP_NOPS = 32;

std::vector<std::uint8_t> rom(WARMUP_NOPS + 1, 0x00);
rom.back() = 0xD7;  // LDM 7

Intel4004GridLevel2 grid;  // L2 defaults: BSIM3 latch + all 6 custom primitives ON
grid.enableMeyerCaps_ = true;
grid.gminTransient_ = grid.gminTransientWithCaps_;

auto circuit = grid.buildCircuit(NETLIST);

// Load 66 layout-extracted bootstrap caps + 4 D-bus pin caps (datasheet).
grid.loadBootstrapCaps("path/to/lajos-4004-bootstrap-caps.txt");

auto state = grid.simulateLevel1FromScratch(
    circuit, rom.data(), rom.size(), WARMUP_NOPS, 0,
    /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);

grid.traceExecuteByte(circuit, state, 0xD7, nullptr);
std::uint8_t acc = grid.readAccumulator(state.nodeVoltages);  // == 7
```

---

## L3: Pure Physics (future aspiration)

L3 would be every transistor in every subcircuit converging via full
transistor-level physics, no custom primitives. It is **not on the
near-term roadmap.** The critical-path blocker is cross-coupled latch
metastability convergence -- a solver-level problem (mature SPICE
simulators including ngspice cannot transient-from-t=0 the full 4004
either). Other gaps (body-effect Vth, bootstrap cap dynamics, variable
timestep, per-transistor schematic W/L) are pure engineering work and
would land at L2 first if pursued.

We confirmed the metastability blocker concretely while investigating
whether `OprCaptureCell` could be retired: the OPR cell is a
**two-stage** cross-coupled latch (D-bus -> N1011 pass-gate -> N0998/
N0999 cross-coupled inverter pair -> OPR.x/~OPR.x cross-coupled inverter
pair), while OPA is a single-stage cell with one direct pass-gate from
D-bus. With BSIM3 weak-inversion at `n_factor=2.5` (+101 mV overdrive
per cell) the OPA cells flip cleanly; the OPR cells need to flip
_twice in series_ and the second flip lands in the metastable region.
Sweeping `n_factor` from 2.5 through 4.0 produces non-monotonic results
(some bits flip, others regress) -- increasing overdrive shifts the
metastable balance points without reliably resolving the flip. Same
shape as ngspice's GMIN-stepping problem on this chip. See
`Intel4004L2_Bootstrap.NFactorSweep_OprConvergence` for the data.

`Intel4004GridLevel3`, if/when it exists, would inherit from L2 and
disable the L2 custom primitives.

---

## Tests

| Target                                  | Type | Coverage                                                                                                                             |
| --------------------------------------- | ---- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `TestSimElectronicsIntel4004Behavioral` | utst | All 46 opcodes, register file, stack, carry                                                                                          |
| `TestSimElectronicsIntel4004Netlist`    | utst | SPICE netlist parser vs `lajos-4004.spice` fixture                                                                                   |
| `TestSimElectronicsIntel4004Grid`       | utst | L1/L2 stamps, schematic-anchored counts, datasheet compliance, BSIM3 verification                                                    |
| `TestSimElectronicsIntel4004Gate`       | utst | Gate extraction, NOR evaluation, propagation, accessors, execution                                                                   |
| `Intel4004Grid_Dev`                     | dtst | L0+L1 multi-instruction, L2 phased plan, OPR/OPA decode, full instruction set parity vs L0 (single-byte, 2-byte, RAM/IO), divergence |
| `Intel4004GateLevel_Dev`                | dtst | Gate-level execution comparison vs L0                                                                                                |
| `Intel4004Gate_PTEST`                   | ptst | Gate extraction and propagation throughput                                                                                           |

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

| Library                                      | Used by                             |
| -------------------------------------------- | ----------------------------------- |
| `sim_electronics_circuit`                    | `grid/`, `gate/`                    |
| `sim_electronics_devices_composite`          | `grid/`                             |
| `sim_electronics_devices_nonlinear`          | `grid/` (MosfetLevel1, MosfetBsim3) |
| `sim_electronics_chips_intel4004_netlist`    | `grid/`, `gate/`                    |
| `sim_electronics_chips_intel4004_behavioral` | `grid/` (L0-authoritative for L1)   |

---

## Performance

### Behavioral CPU (L0)

| Metric          | Value                      |
| --------------- | -------------------------- |
| Step throughput | ~7.7M steps/s (NOP, debug) |
| Step latency    | ~16 ns/step (NOP)          |
| Per-instruction | ~45 ns/instr (with reset)  |
| IPC             | 3.41                       |

### Grid (L1)

| Metric         | Value                               |
| -------------- | ----------------------------------- |
| Circuit build  | ~1.2 ms (2,242 tx, ~1,081 nets)     |
| L1 single-byte | ~2.5 s (16 NOP warm-up + 1 L1 byte) |
| DC solve       | ~3.0 ms (stamp 2242 tx + KLU)       |

### Grid (L2)

| Metric                                 | Value                                                                    |
| -------------------------------------- | ------------------------------------------------------------------------ |
| L2 single-byte (cold, includes warmup) | ~4.6 s (32-NOP warmup + BSIM3 + Meyer caps + bootstrap caps + 1 L2 byte) |
| L2 multi-byte (warmup amortized)       | ~0.7-0.8 s per byte                                                      |
| L2 vs L1 byte cost                     | ~1.9x (L1 ~2.5 s, L2 ~4.6 s)                                             |

---

## Reference materials

The transistor topology comes from the Kintli/Silverman reverse-engineered
SPICE netlist (`netlist/data/lajos-4004.spice`); the 66 layout-extracted
bootstrap caps come from Lajos Kintli's original layout netlist. The
Intel 4004 datasheet (CDB=7pF, IOL, VOL/VOH, AC timing) and the Rev G
hand-drawn schematic (Aug 1976) anchor electrical and topological
verification.

---

## See Also

- [Circuit](../../circuit/README.md) -- construction API
- [devices/nonlinear](../../devices/nonlinear/README.md) -- MosfetLevel1, MosfetBsim3
- [algorithms/spice/ngspice](../../algorithms/spice/ngspice/README.md) -- ngspice integration used for L1 validation
- `apps/apex_cpu_sim_demo/` -- end-to-end demo
