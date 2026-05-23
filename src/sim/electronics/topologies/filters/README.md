# Filters Module

**Namespace:** `sim::electronics::topologies::filters`
**Platform:** Linux-only
**C++ Standard:** C++23

Analog filter circuit models built on top of the
[Circuit](../../circuit/README.md) construction API. Each model wraps a
`Circuit`, allocates nets, registers stamps for primitives, and adds
reactive-element companions.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [RcLowPass](#rclowpass)
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: RC Low-Pass Step Response](#8-example-rc-low-pass-step-response)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                              | Model                                    |
| ----------------------------------------------------- | ---------------------------------------- |
| How do I build a first-order RC low-pass filter?      | `RcLowPass`                              |
| How do I get the cutoff frequency?                    | `RcLowPass::cutoffHz`                    |
| How do I get the time constant?                       | `RcLowPass::tau`                         |
| How do I evaluate the closed-form step response?      | `RcLowPass::analyticalStepResponse`      |
| How do I evaluate the closed-form magnitude response? | `RcLowPass::analyticalMagnitudeResponse` |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/topologies/filters/inc/RcLowPass.hpp"

using sim::electronics::topologies::filters::RcLowPass;

RcLowPass filter(/*R=*/1e3, /*C=*/1e-6);
filter.build();
filter.setInputVoltage(5.0);
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Composition over inheritance.** Each filter owns a `Circuit` and
  exposes setters for the tunable parameters.
- **Analytical helpers.** Every model ships closed-form references
  (`analyticalStepResponse`, `analyticalMagnitudeResponse`) so callers
  can validate simulated output against theory.
- **RT-safe after build.** `build()` allocates the underlying transient
  solver workspace; step / setter calls afterward are RT-safe.

---

## 4. Module Reference

### RcLowPass

**Header:** `RcLowPass.hpp`
**Purpose:** First-order RC low-pass filter.

```
IN ---[R]---+--- OUT
            |
           [C]
            |
           GND
```

Transfer function `H(s) = 1 / (1 + sRC)`; cutoff `fc = 1 / (2 * pi * R * C)`;
time constant `tau = R * C`; step response
`V_out(t) = V_in * (1 - exp(-t / tau))`.

#### API

| Function                                                | Purpose                             |
| ------------------------------------------------------- | ----------------------------------- |
| `RcLowPass(R, C)`                                       | Construct filter, allocate nets     |
| `void build()`                                          | Finalize underlying TransientSolver |
| `void setInputVoltage(V) noexcept`                      | Set DC input voltage                |
| `double cutoffHz() const noexcept`                      | `fc = 1 / (2 * pi * R * C)`         |
| `double tau() const noexcept`                           | Time constant `R * C`               |
| `Circuit& circuit() noexcept`                           | Access the underlying circuit       |
| `NetID inNet() / outNet() const noexcept`               | Net IDs for probing                 |
| `double analyticalStepResponse(V_in, t) const noexcept` | Closed-form `V_out(t)`              |
| `double analyticalMagnitudeResponse(f) const noexcept`  | Closed-form magnitude `H(j*2*pi*f)` |

`build()` is NOT RT-safe. After build, `setInputVoltage`, `cutoffHz`,
`tau`, and the analytical helpers are RT-safe.

---

## 5. Common Patterns

### DC Operating Point

```cpp
RcLowPass filter(1e3, 1e-6);
filter.build();
filter.setInputVoltage(5.0);

TransientState state;
state.resize(filter.circuit().netCount(), 0);
filter.circuit().computeDC(state);

const double V_OUT = state.nodeVoltages[filter.outNet()];
```

### Transient Step Response

```cpp
const double DT = 10e-6;
for (int i = 0; i < 500; ++i) {
  filter.circuit().solver().step(DT, state);
}
```

### Analytical vs Simulated Comparison

```cpp
const double V_SIM   = state.nodeVoltages[filter.outNet()];
const double V_ANALY = filter.analyticalStepResponse(5.0, t);
const double ERROR   = std::abs(V_SIM - V_ANALY);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `setInputVoltage`
- `cutoffHz`, `tau`
- `analyticalStepResponse`, `analyticalMagnitudeResponse`
- `inNet`, `outNet`

### NOT RT-Safe Functions

- Constructor and `build()` (allocate the underlying transient solver).
- `Circuit::computeDC` and `Circuit::solver().step()` are RT-safe after
  the first call; see the transient solver documentation for details.

---

## 7. CLI Tools

- `apex_circuit_demo --circuit rc-lowpass` exercises this module
  end-to-end and prints simulated vs analytical step / magnitude
  responses. See [apps/apex_circuit_demo](../../../../apps/apex_circuit_demo/).

---

## 8. Example: RC Low-Pass Step Response

```cpp
#include "src/sim/electronics/topologies/filters/inc/RcLowPass.hpp"

#include <fmt/core.h>

int main() {
  using sim::electronics::topologies::filters::RcLowPass;

  RcLowPass filter(/*R=*/1e3, /*C=*/1e-6);
  filter.build();
  filter.setInputVoltage(5.0);

  TransientState state;
  state.resize(filter.circuit().netCount(), 0);
  filter.circuit().computeDC(state);

  const double DT = 10e-6;
  for (int i = 0; i < 500; ++i) {
    filter.circuit().solver().step(DT, state);
    const double T = (i + 1) * DT;
    fmt::print("t={:.3e}  v(out)={:.6f}  analytical={:.6f}\n",
               T,
               state.nodeVoltages[filter.outNet()],
               filter.analyticalStepResponse(5.0, T));
  }
  return 0;
}
```

---

## 9. See Also

- [Circuit](../../circuit/README.md) - Construction API used by every model
- [Linear devices](../../devices/linear/README.md) - ResistorModel and friends
- [apps/apex_circuit_demo](../../../../apps/apex_circuit_demo/) - Example consumer (`--circuit rc-lowpass`)
