# Nonlinear Device Models

Physics-based models for nonlinear circuit elements (diodes, MOSFETs, BJTs) with smooth I-V curves and derivatives for Newton-Raphson solvers.

## Overview

This library provides **Layer 2** device physics models for transient circuit simulation. Each model implements:

- Current computation (I-V characteristic)
- Conductance computation (dI/dV for Newton-Raphson)
- MNA stamping (linearized around operating point)

These models are used by Layer 3 applications (circuit grids, netlists) combined with Layer 1 algorithms (MNA, Newton-Raphson, transient solver).

## Available Models

| Model              | Description                                | Use Case                              | Tests |
| ------------------ | ------------------------------------------ | ------------------------------------- | ----- |
| BjtEbersMoll       | Bipolar junction transistor (Ebers-Moll)   | Op-amps, analog amplifiers            | 33    |
| DiodeShockley      | Exponential I-V (Shockley equation)        | Rectifiers, clamps, analog circuits   | 15    |
| DiodeSpice         | SPICE diode with series R and junction cap | Accurate transient, power diodes      | 23    |
| SchottkyDiode      | Schottky barrier diode (fast switching)    | Fast rectifiers, low-voltage power    | 19    |
| ZenerDiode         | Zener diode with breakdown                 | Voltage regulation, references        | 24    |
| JfetShichman       | Junction FET (Shichman-Hodges 3-region)    | Precision op-amps, analog switches    | 28    |
| JfetLevel2         | Advanced JFET (gate leakage, capacitance)  | High-frequency, low-noise amplifiers  | 24    |
| MosfetBinarySwitch | Digital switch (ON/OFF only)               | Digital logic, fast simulation        | 17    |
| MosfetLevel1       | Shichman-Hodges 3-region model             | Analog circuits, accurate transient   | 32    |
| MosfetLevel2       | SPICE Level 2 (geometry, velocity sat)     | Short-channel, body-effect circuits   | 29    |
| MosfetLevel3       | SPICE Level 3 (DIBL, short-channel)        | Submicron devices, low-voltage analog | 25    |

## Usage Example

### Diode Forward Bias

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

using namespace sim::electronics::devices::nonlinear;

// Silicon diode parameters
DiodeShockleyParams params{
  .Is = 1e-14,  // Saturation current
  .n = 1.0,     // Ideality factor
  .Vt = 0.026   // Thermal voltage (300K)
};

// Compute current at 0.7V forward bias
double vDiode = 0.7;
double iDiode = DiodeShockley::current(vDiode, params);
// iDiode ≈ 5 mA (typical silicon turn-on)

// Stamp into MNA system for Newton-Raphson
MnaSystem mna(3);
DiodeShockley::stamp(mna, anodeNet, cathodeNet, vDiode, params);
```

### Zener Diode Voltage Regulator

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/ZenerDiode.hpp"

using namespace sim::electronics::devices::nonlinear;

// 5.1V Zener regulator
ZenerDiodeParams params{
  .Vz = 5.1,     // Breakdown voltage
  .Ibv = 1e-3,   // Breakdown knee current
  .Vbv = 0.1     // Breakdown sharpness
};

// Reverse bias beyond breakdown (regulation region)
double vZener = -5.5;
double iZener = ZenerDiode::current(vZener, params);
// iZener ≈ -1 mA (regulated current)

// Stamp into MNA system for Newton-Raphson
MnaSystem mna(3);
ZenerDiode::stamp(mna, anodeNet, cathodeNet, vZener, params);
```

### JFET Precision Op-Amp Input

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"

using namespace sim::electronics::devices::nonlinear;

// N-channel JFET (e.g., 2N5457, J201)
JfetShichmanParams params{
  .Beta = 1e-3,  // Transconductance parameter
  .Vp = -2.0,    // Pinch-off voltage
  .lambda = 0.01 // Channel-length modulation
};

// Saturation region (typical operating point)
double vgs = -0.5;  // Gate voltage
double vds = 5.0;   // Drain-source voltage
double id = JfetShichman::current(vgs, vds, params);
// id ≈ 2.25 mA (constant current source)

// Stamp into MNA system for Newton-Raphson
MnaSystem mna(4);
JfetShichman::stamp(mna, drainNet, gateNet, sourceNet, vgs, vds, params);
```

### MOSFET Digital Switch (Fast)

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/MosfetBinarySwitch.hpp"

using namespace sim::electronics::devices::nonlinear;

// Digital MOSFET (for CPU simulation)
MosfetBinarySwitchParams params{
  .Vth = 0.7,    // Threshold voltage
  .Ron = 500.0,  // On-resistance
  .Roff = 1e9    // Off-resistance
};

// Check if MOSFET is ON
double vgs = 1.5;  // Gate voltage
bool on = MosfetBinarySwitch::isOn(vgs, params);  // true

// Stamp as resistor (no Newton-Raphson needed)
double rds = MosfetBinarySwitch::resistance(vgs, params);
// rds = 500 Ω (ON state)
```

### MOSFET Analog Model (Accurate)

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

using namespace sim::electronics::devices::nonlinear;

// NMOS transistor parameters
MosfetLevel1Params params{
  .Kp = 100e-6,   // Transconductance parameter
  .Vth = 0.7,     // Threshold voltage
  .lambda = 0.02  // Channel-length modulation
};

// Operating point
double vgs = 1.5;  // Gate-source voltage
double vds = 2.0;  // Drain-source voltage

// Compute drain current
double id = MosfetLevel1::current(vgs, vds, params);
// id ≈ 32 µA (saturation region)

// Compute transconductances for Newton-Raphson
double gm = MosfetLevel1::transconductance(vgs, vds, params);   // dId/dVgs
double gds = MosfetLevel1::outputConductance(vgs, vds, params); // dId/dVds

// Stamp into MNA system
MosfetLevel1::stamp(mna, drainNet, gateNet, sourceNet, vgs, vds, params);
```

### BJT Amplifier (Op-Amp Stage)

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"

using namespace sim::electronics::devices::nonlinear;

// NPN transistor parameters (e.g., 2N2222, 2N3904)
BjtEbersMollParams params{
  .Is = 1e-14,   // Saturation current
  .Bf = 100.0,   // Forward current gain (beta)
  .Br = 1.0,     // Reverse current gain
  .Vt = 0.026    // Thermal voltage (300K)
};

// Operating point (forward active region)
double vbe = 0.7;  // Base-emitter voltage
double vbc = -5.0; // Base-collector voltage (reverse-biased)

// Compute currents
double ic = BjtEbersMoll::collectorCurrent(vbe, vbc, params);
double ib = BjtEbersMoll::baseCurrent(vbe, vbc, params);
double beta = ic / ib;  // Current gain ≈ 100

// Compute transconductances for Newton-Raphson
double gm = BjtEbersMoll::transconductance(vbe, vbc, params);  // dIc/dVbe
double go = BjtEbersMoll::outputConductance(vbe, vbc, params); // dIc/dVbc

// Stamp into MNA system
BjtEbersMoll::stamp(mna, collectorNet, baseNet, emitterNet, vbe, vbc, params);
```

## Fidelity Switching

Different models provide speed vs accuracy trade-offs:

| Circuit Type   | Model              | Why                                              |
| -------------- | ------------------ | ------------------------------------------------ |
| Intel 4004 CPU | MosfetBinarySwitch | Digital logic, 2,242 transistors, speed critical |
| Op-amp (LM741) | BjtEbersMoll       | Bipolar op-amps use BJTs, 4-region model         |
| CMOS op-amp    | MosfetLevel1       | Analog MOSFET behavior, smooth I-V curves        |
| Power supply   | DiodeShockley      | Exponential diode characteristic                 |

## Newton-Raphson Integration

All models provide linearized stamping for Newton-Raphson iteration:

```
I_device = G * V + Ieq
```

Where:

- `G` = small-signal conductance (dI/dV)
- `Ieq` = equivalent current source (I - G\*V)

This allows the nonlinear device to be solved iteratively:

```cpp
// Newton-Raphson loop (Layer 1 algorithm)
for (int iter = 0; iter < maxIter; ++iter) {
  mna.clearStamps();

  // Stamp all devices (Layer 2 models)
  DiodeShockley::stamp(mna, anode, cathode, vDiode, params);
  MosfetLevel1::stamp(mna, drain, gate, source, vgs, vds, params);

  // Solve linear system (Layer 1 algorithm)
  mna.solve();

  // Update voltages for next iteration
  vDiode = mna.voltage(anode) - mna.voltage(cathode);
  // ...
}
```

## RT-Safety Annotations

All models in this library are **RT-safe**:

- Static functions (no state)
- No allocations (pure math)
- Deterministic execution (no branching on runtime data)

Safe for use in hard real-time contexts.

## Physical Accuracy

### DiodeShockley

Matches SPICE diode model with exponential I-V characteristic:

- Forward bias: Exponential turn-on at ~0.6-0.7V (silicon)
- Reverse bias: Saturates at -Is (reverse saturation current)
- Temperature: Controlled via Vt = kT/q

### MosfetBinarySwitch

Digital approximation for fast simulation:

- ON: Vgs > Vth → Rds = Ron (typically 100-1000 Ω)
- OFF: Vgs < Vth → Rds = Roff (typically 10M-1G Ω)
- No smooth transition (binary switch)

### MosfetLevel1

SPICE Level 1 Shichman-Hodges model with three regions:

- Cutoff: `Vgs < Vth` -> `Id = 0`
- Linear: `Vgs > Vth, Vds < Vgst` -> `Id = Kp * (Vgst * Vds - 0.5 * Vds^2)`
- Saturation: `Vgs > Vth, Vds >= Vgst` -> `Id = 0.5 * Kp * Vgst^2`

Channel-length modulation: `Id * (1 + lambda * Vds)` for non-ideal output resistance.

### BjtEbersMoll

SPICE-compatible Ebers-Moll model with four operating regions:

- Cutoff: Both junctions reverse-biased (Vbe < 0.6V, Vbc < 0.6V) → Ic ≈ 0, Ib ≈ 0
- Forward Active: BE forward, BC reverse (Vbe > 0.6V, Vbc < 0) → Ic = Is \* exp(Vbe/Vt), beta = Ic/Ib ≈ Bf
- Reverse Active: BE reverse, BC forward (Vbe < 0, Vbc > 0.6V) → Ic < 0 (reverse current)
- Saturation: Both junctions forward (Vbe > 0.6V, Vbc > 0) → Ic reduced, Vce_sat ≈ 0.2V

Exponential I-V: Every 60mV increase in Vbe → 10x increase in Ic (at 300K).

## Performance

| Operation                                  | Latency        | Scale        |
| ------------------------------------------ | -------------- | ------------ |
| MosfetLevel1 current evaluation            | 7.6 ns/device  | 2242 devices |
| MosfetLevel1 full NR stamp (Id+gm+gds+ieq) | 19.1 ns/device | 2242 devices |
| DiodeShockley evaluation                   | 9.6 ns/diode   | 1000 diodes  |
| BjtEbersMoll evaluation                    | 13.4 ns/BJT    | 500 BJTs     |

MOSFET stamp throughput: 23.3K batches/s (2242 devices per batch). For the Intel
4004 reference circuit (2242 MOSFETs), device evaluation costs 42.9 us per
Newton-Raphson iteration, representing approximately 40% of total NR cost. The
remaining 60% is linear algebra (MNA solve).

## Testing

All models have comprehensive unit tests covering:

- I-V characteristic (all operating regions)
- Transconductances (dI/dV)
- Numerical Jacobian validation (analytical matches numerical derivative)
- Physical behavior (output/transfer characteristics)
- MNA stamping

Run tests:

```bash
make compose-testp
# sim_electronics_devices_nonlinear_uTest: 269 tests (33 BJT + 15 diode + 23 spice + 19 schottky + 24 zener + 28 jfet1 + 24 jfet2 + 17 binary + 32 level1 + 29 level2 + 25 level3)
```

## Architecture

```
Layer 1 (Algorithms)         Layer 2 (Device Models)        Layer 3 (Applications)
┌─────────────────────┐      ┌─────────────────────┐       ┌─────────────────────┐
│ MNA Solver          │◄─────│ BjtEbersMoll        │◄──────│ LM741 Op-Amp        │
│ Newton-Raphson      │      │ DiodeShockley       │       │ Low-Pass Filter     │
│ Transient Solver    │      │ MosfetLevel1        │       │ CMOS Op-Amp         │
│                     │      │ MosfetBinarySwitch  │       │ Intel 4004 Grid     │
└─────────────────────┘      └─────────────────────┘       └─────────────────────┘
```

## See Also

- `../linear/README.md` - Linear device models (R, L, C)
- `../companions/README.md` - Numerical integration wrappers for reactive devices
- `../descriptors/README.md` - Topology descriptors (no physics)
- `../../algorithms/mna/README.md` - MNA solver (Layer 1)
- `../../algorithms/newton_raphson/README.md` - Nonlinear solver (Layer 1)
