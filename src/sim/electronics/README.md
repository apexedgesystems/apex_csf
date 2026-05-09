# sim_electronics

Apex's electronics simulation suite -- everything needed to build and analyse
analog and digital circuits at user-selectable fidelity, from a single-line
boolean truth table up to full BSIM3 transistor physics.

## Layout

```
src/sim/electronics/
  algorithms/           Numerics (everything used to *solve* a circuit)
    mna/                Modified Nodal Analysis (sparse + dense + AC + batch)
    nonlinear/          Newton-Raphson framework
    transient/          Time-domain integration loop (Backward-Euler / GEAR2)
    companions/         Time-domain companion models (Geq + Ieq stamps for C, L)
    spice/ngspice/      NGSPICE bridge for cross-validation (verification only)
  circuit/              Circuit composition API (addNet / addConductance / addCapacitor / ...)
  devices/              Physics (everything that *is* a device)
    linear/             R / L / C device classes + reactance / impedance helpers
    nonlinear/          Diodes, MOSFETs, BJTs, JFETs (full fidelity ladder)
    composite/          Multi-transistor primitives (CmosInverter / CmosNand / CmosNor)
    descriptors/        Parameter descriptor types
  topologies/           Pre-built circuits assembled from the above
    gates/              CMOS logic gates -- boolean and transistor-level
    filters/            Filter topologies (RC low-pass, ...)
    amplifiers/         Amplifier topologies (BJT common-emitter, ...)
  intel4004/            Chip-specific (Intel 4004 microprocessor)
    behavioral/         ISA-level CPU model (L0)
    gate/               ~427-gate event-driven model (Lg)
    grid/               Transistor-level (L1 + L2)
    netlist/            SPICE netlist parser for the Lajos-extracted 4004
```

The split is intentional: **`algorithms/` = how to solve, `devices/` = what's
being solved, `topologies/` = ready-made circuits, `intel4004/` = chip-scale
assembly.** Companion models live under `algorithms/` because they're a
numerical method (Backward-Euler discretization of `I = C dV/dt`), not a
physical device.

## How to construct a custom circuit

The composition pattern customers reuse is the same across every demo:

```cpp
#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
// (or whichever device model fits your fidelity / accuracy needs)

// 1. Pick a device model.
MosfetLevel1Params nmos{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};

// 2. Build a circuit topology with the Circuit API.
sim::electronics::circuit::Circuit ckt;
auto VDD = ckt.addNet("VDD").id;
auto OUT = ckt.addNet("OUT").id;
ckt.addStamp([&](MnaSystem& mna, double, const std::vector<double>& v) {
  // ... add conductances + device stamps ...
});

// 3. Drive an analysis.
ckt.computeDC(state);                    // DC operating point
// or:
ckt.solver().step(dt, state);            // transient sub-step
```

`apps/apex_circuit_demo` ships a ready-to-customize example covering three
common patterns -- digital (CMOS gates), passive analog (RC filter), and
active analog (BJT common-emitter) -- so a customer building their own
circuit has a working starting point.

## Fidelity tiers (pick the right tool)

The suite is intentionally multi-fidelity. Faster models trade physical
accuracy for speed; higher-fidelity models surface short-channel,
moderate-inversion, and dynamic-charge effects.

### Logic gates (`topologies/gates/`)

| Header                                          | Fidelity                | Latency / gate | Use when |
|-------------------------------------------------|-------------------------|---------------:|----------|
| `topologies/gates/inc/LogicGates.hpp`           | Boolean truth table     | ~6 ns          | Pure logic verification, design-time correctness, fast sweeps |
| `topologies/gates/inc/CmosGateCircuits.hpp`     | Transistor-level (MOSFET Level 1) | ~us | Transfer-characteristic / VOH / VOL / NM analysis with real physics |

### MOSFET (`devices/nonlinear/`)

| Header                  | Fidelity                                       | Pick when |
|-------------------------|------------------------------------------------|-----------|
| `MosfetBinarySwitch.hpp`| ON/OFF switch (no I-V curve)                   | Initialization / warmup; digital-only logic |
| `MosfetLevel1.hpp`      | Shichman-Hodges 3-region                       | General analog circuit work; balance of speed and accuracy |
| `MosfetLevel2.hpp`      | SPICE Level 2 (geometry + velocity saturation) | Short-channel circuits where Level 1 misses |
| `MosfetLevel3.hpp`      | SPICE Level 3 (DIBL + short-channel)           | Sub-micron / low-voltage analog |
| `MosfetBsim3.hpp`       | BSIM3 (smooth `Vgst_eff` across all regions)   | Moderate-inversion accuracy; required for the Intel 4004 L2 latch core |

### Diode (`devices/nonlinear/`)

| Header              | Fidelity                                | Use when |
|---------------------|-----------------------------------------|----------|
| `DiodeShockley.hpp` | Pure exponential I-V (Shockley equation)| Rectifiers, clamps, basic analog |
| `DiodeSpice.hpp`    | SPICE diode (series R, junction cap)    | Power diodes, accurate transient |
| `SchottkyDiode.hpp` | Schottky barrier diode                  | Fast rectifiers, low-voltage power |
| `ZenerDiode.hpp`    | Zener diode with breakdown region       | Voltage regulators, references |

### JFET (`devices/nonlinear/`)

| Header             | Fidelity                              | Use when |
|--------------------|---------------------------------------|----------|
| `JfetShichman.hpp` | Shichman-Hodges 3-region              | Precision op-amps, analog switches |
| `JfetLevel2.hpp`   | Adds gate leakage and capacitance     | High-frequency / low-noise amplifiers |

### BJT (`devices/nonlinear/`)

| Header              | Fidelity                       | Use when |
|---------------------|--------------------------------|----------|
| `BjtEbersMoll.hpp`  | Ebers-Moll (4-parameter)       | Op-amps, analog amplifiers, the `BjtCommonEmitter` topology |

### Intel 4004 (`intel4004/`)

| Level | Library                          | Latency / byte | What it captures |
|-------|----------------------------------|---------------:|------------------|
| L0    | `intel4004/behavioral/`          | sub-us         | ISA-level instruction execution (truth-table) |
| Lg    | `intel4004/gate/`                | ms-range       | ~427 extracted NOR gates with event-driven propagation |
| L1    | `intel4004/grid/` (Level 1)      | ~2.5 s         | Full transistor circuit, Shichman-Hodges + behavioral overlay |
| L2    | `intel4004/grid/` (Level 2)      | ~3-5 s         | BSIM3 latches + Meyer caps + bootstrap caps, no behavioral overlay |

## Solvers and numerics (`algorithms/`)

| Library                       | Role |
|-------------------------------|------|
| `algorithms/mna/`             | MNA solver; sparse (KLU), dense (LAPACK), AC, multi-thread parallel KLU, batched CUDA |
| `algorithms/nonlinear/`       | Newton-Raphson framework (used by every nonlinear device stamp) |
| `algorithms/transient/`       | Backward-Euler time integration on top of MNA + companions |
| `algorithms/spice/ngspice/`   | NGSPICE bridge -- verification only, used by dtests to cross-check our models against the reference SPICE engine. Not consumed by any production app. |

## Linear devices (`devices/linear/`) and Companions (`algorithms/companions/`)

Linear (`R`, `L`, `C`) elements have two complementary surfaces:

- **`devices/linear/{Resistor,Capacitor,Inductor}Model.hpp`** -- analytical /
  frequency-domain helpers: `reactance(C, f)`, `impedance(L, f)`, etc. Useful
  for AC / Bode work without running a transient sim.
- **`algorithms/companions/CompanionModels.hpp`** -- time-domain companion
  models: `CapacitorCompanion`, `InductorCompanion` -- the `Geq + Ieq`
  stamps consumed by the transient solver each sub-step.

Both surfaces share parameters; the model class re-exports the companion
type for convenience (`using Capacitor = CapacitorCompanion`).

## Demo apps

| App                  | What it shows                                  |
|----------------------|------------------------------------------------|
| `apex_circuit_demo`  | `--circuit gates` / `rc-lowpass` / `common-emitter` -- the reference customer-facing template for building custom circuits |
| `apex_cpu_sim_demo`  | Intel 4004 CPU at L0 / L1 / L2; ships 19 example `.4004` programs that span the full ISA |

## See also

- `apps/apex_circuit_demo/docs/HOW_TO_RUN.md` -- 3-step composition pattern + flag reference
- `apps/apex_cpu_sim_demo/docs/HOW_TO_RUN.md` -- fidelity-level ladder + example programs
- Per-library READMEs under each subdirectory for API details and per-device tables
