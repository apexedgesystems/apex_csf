# Nonlinear Devices Module

**Namespace:** `sim::electronics::devices::nonlinear`
**Platform:** Linux-only
**C++ Standard:** C++23

Physics-based models for nonlinear circuit elements (diodes, MOSFETs,
JFETs, BJTs) with smooth I-V curves and analytic / numeric derivatives
suitable for Newton-Raphson stamping.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [Diode family](#diode-family) - Shockley, SPICE, Schottky, Zener
   - [JFET family](#jfet-family) - Shichman, Level 2
   - [MOSFET family](#mosfet-family) - Binary switch, Level 1, Level 2, Level 3, BSIM3
   - [BJT family](#bjt-family) - Ebers-Moll
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Diode Forward Bias](#8-example-diode-forward-bias)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                                     | Model                                                          |
| ------------------------------------------------------------ | -------------------------------------------------------------- |
| How do I model a generic forward-biased diode?               | `DiodeShockley`                                                |
| How do I model a SPICE diode with series R and junction cap? | `DiodeSpice`                                                   |
| How do I model a Schottky diode?                             | `SchottkyDiode`                                                |
| How do I model a Zener regulator?                            | `ZenerDiode`                                                   |
| How do I model a JFET?                                       | `JfetShichman` (3-region) or `JfetLevel2` (gate leakage / cap) |
| How do I model a digital MOSFET switch?                      | `MosfetBinarySwitch`                                           |
| How do I model an analog MOSFET?                             | `MosfetLevel1` (Shichman-Hodges)                               |
| How do I capture short-channel effects?                      | `MosfetLevel2` or `MosfetLevel3`                               |
| How do I model moderate-inversion conduction?                | `MosfetBsim3`                                                  |
| How do I model a bipolar transistor?                         | `BjtEbersMoll`                                                 |

### Selection by Circuit Type

| Circuit Type           | Model                                 | Why                                              |
| ---------------------- | ------------------------------------- | ------------------------------------------------ |
| Intel 4004 CPU         | `MosfetBinarySwitch` / `MosfetLevel1` | Digital logic, 2,242 transistors, speed critical |
| Bipolar op-amp (LM741) | `BjtEbersMoll`                        | 4-region BJT model                               |
| CMOS op-amp            | `MosfetLevel1` / `MosfetBsim3`        | Smooth I-V plus moderate-inversion behavior      |
| Power supply rectifier | `DiodeShockley` / `DiodeSpice`        | Exponential diode characteristic                 |
| Voltage reference      | `ZenerDiode`                          | Breakdown-region regulation                      |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;

DiodeShockleyParams params{.Is = 1e-14, .n = 1.0, .Vt = 0.026};
const double I = DiodeShockley::current(/*v=*/0.7, params);
DiodeShockley::stamp(mna, anodeNet, cathodeNet, /*vDiode=*/0.7, params);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Static helpers, not classes.** Each model is a stateless `struct` /
  namespace with static `current`, `stamp`, and derivative helpers. The
  caller owns terminal voltages and net IDs.
- **Linearization on demand.** Every model exposes the Norton equivalent
  needed by Newton-Raphson stamping (`I_device = G * V + Ieq`).
- **No exceptions.** Out-of-range inputs return finite numbers (clamped
  exponents) so the matrix stamp never produces NaNs.
- **RT-safe everywhere.** Stamping into a pre-sized MNA system performs
  only arithmetic.

---

## 4. Module Reference

### Diode family

**Headers:** `DiodeShockley.hpp`, `DiodeSpice.hpp`, `SchottkyDiode.hpp`,
`ZenerDiode.hpp`

| Model           | Captures                                  | Notes                          |
| --------------- | ----------------------------------------- | ------------------------------ |
| `DiodeShockley` | Forward exponential, reverse saturation   | Basic Is / n / Vt model        |
| `DiodeSpice`    | Adds series R, junction capacitance       | Closer to SPICE behavior       |
| `SchottkyDiode` | Lower turn-on, fast reverse recovery      | Suitable for low-voltage power |
| `ZenerDiode`    | Forward conduction plus reverse breakdown | Vz, Ibv, Vbv parameters        |

Each model provides:

```cpp
[[nodiscard]] static double current(double v, const Params& p) noexcept;
[[nodiscard]] static double conductance(double v, const Params& p) noexcept;

static void stamp(MnaSystem& mna, NetID anode, NetID cathode,
                  double vDiode, const Params& p);
```

### JFET family

**Headers:** `JfetShichman.hpp`, `JfetLevel2.hpp`

| Model          | Captures                          |
| -------------- | --------------------------------- |
| `JfetShichman` | Three-region Shichman-Hodges I-V  |
| `JfetLevel2`   | Adds gate-leakage and capacitance |

```cpp
[[nodiscard]] static double current(double vgs, double vds, const Params& p) noexcept;
static void stamp(MnaSystem& mna, NetID drain, NetID gate, NetID source,
                  double vgs, double vds, const Params& p);
```

### MOSFET family

**Headers:** `MosfetBinarySwitch.hpp`, `MosfetLevel1.hpp`, `MosfetLevel2.hpp`,
`MosfetLevel3.hpp`, `MosfetBsim3.hpp`

| Model                | Captures                       | When to use                          |
| -------------------- | ------------------------------ | ------------------------------------ |
| `MosfetBinarySwitch` | ON / OFF resistor only         | Fastest digital simulation           |
| `MosfetLevel1`       | Shichman-Hodges 3-region       | Default analog model                 |
| `MosfetLevel2`       | Geometry, velocity saturation  | Short-channel analog                 |
| `MosfetLevel3`       | DIBL, short-channel effects    | Submicron / low-voltage              |
| `MosfetBsim3`        | Moderate-inversion, Meyer caps | Cross-coupled latches, dynamic logic |

All MOSFET models provide `current`, `transconductance`, `outputConductance`
(plus `bodyTransconductance` for the bias-aware models), and a `stamp` entry
point that writes the linearized stamp into an `MnaSystem` or
`MnaSystemSparse`. `MosfetLevel1BatchCuda.cuh` exposes a CUDA batch
evaluator for large device arrays.

### BJT family

**Header:** `BjtEbersMoll.hpp`

```cpp
[[nodiscard]] static double collectorCurrent(double vbe, double vbc, const Params& p) noexcept;
[[nodiscard]] static double baseCurrent     (double vbe, double vbc, const Params& p) noexcept;
[[nodiscard]] static double transconductance(double vbe, double vbc, const Params& p) noexcept;
[[nodiscard]] static double outputConductance(double vbe, double vbc, const Params& p) noexcept;

static void stamp(MnaSystem& mna, NetID collector, NetID base, NetID emitter,
                  double vbe, double vbc, const Params& p);
```

Operating regions (forward / reverse active, saturation, cutoff) emerge
from the Ebers-Moll equations; the stamp linearizes around the current
(vbe, vbc) operating point for Newton-Raphson.

---

## 5. Common Patterns

### Linearized Newton-Raphson Stamp

```cpp
for (int iter = 0; iter < maxIter; ++iter) {
  mna.clear();
  DiodeShockley::stamp(mna, anode, cathode, vDiode, diodeParams);
  MosfetLevel1::stamp(mna, drain, gate, source, vgs, vds, mosfetParams);
  const auto RESULT = mna.solve();
  vDiode = RESULT.nodeVoltages[anode] - RESULT.nodeVoltages[cathode];
  vgs    = RESULT.nodeVoltages[gate]  - RESULT.nodeVoltages[source];
  vds    = RESULT.nodeVoltages[drain] - RESULT.nodeVoltages[source];
}
```

### MOSFET Operating-Point Probe

```cpp
const double VGS = 1.5;
const double VDS = 2.0;
MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

const double ID  = MosfetLevel1::current(VGS, VDS, PARAMS);
const double GM  = MosfetLevel1::transconductance(VGS, VDS, PARAMS);
const double GDS = MosfetLevel1::outputConductance(VGS, VDS, PARAMS);
```

### Zener Regulator

```cpp
ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};
ZenerDiode::stamp(mna, anode, cathode, /*vDiode=*/-5.5, params);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- Every `current`, `conductance`, `transconductance`, `outputConductance`,
  and `stamp` entry point. All take POD parameter structs and do no
  allocation.

### NOT RT-Safe Functions

- The CUDA batch path (`MosfetLevel1BatchCuda`) performs device allocation
  and host-device transfers.

### Recommended Configuration

- Build parameter structs once during configuration and pass them by
  const reference through the RT loop.
- For dynamic-logic circuits (e.g. Intel 4004 latch core), prefer
  `MosfetBsim3` so moderate-inversion conduction is captured rather than
  rounded to zero.

---

## 7. CLI Tools

None.

---

## 8. Example: Diode Forward Bias

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

#include <fmt/core.h>

int main() {
  using namespace sim::electronics;

  const algorithms::mna::NetID ANODE = 1, CATHODE = 0;

  algorithms::mna::MnaSystem mna(/*nodeCount=*/2);
  devices::nonlinear::DiodeShockleyParams params{
      .Is = 1e-14, .n = 1.0, .Vt = 0.026};

  double vDiode = 0.7;
  for (int iter = 0; iter < 20; ++iter) {
    mna.clear();
    devices::nonlinear::DiodeShockley::stamp(mna, ANODE, CATHODE, vDiode, params);
    mna.addCurrent(ANODE, CATHODE, /*Is=*/1e-3);
    const auto RESULT = mna.solve();
    vDiode = RESULT.nodeVoltages[ANODE] - RESULT.nodeVoltages[CATHODE];
  }

  fmt::print("v(diode) = {:.6f} V\n", vDiode);
  return 0;
}
```

---

## 9. See Also

- [Linear devices](../linear/README.md) - R / L / C primitives
- [Composite devices](../composite/README.md) - CMOS gates built from MOSFETs
- [Descriptors](../descriptors/README.md) - Topology-only device descriptions
- [Nonlinear solver](../../algorithms/nonlinear/README.md) - Newton-Raphson driver
- [MNA library](../../algorithms/mna/README.md) - Linear system
