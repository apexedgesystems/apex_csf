# Amplifiers Module

**Namespace:** `sim::electronics::topologies::amplifiers`
**Platform:** Linux-only
**C++ Standard:** C++23

Pre-built BJT amplifier circuit topologies that compose the Circuit API
with the `BjtEbersMoll` device model.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [BjtCommonEmitter](#bjtcommonemitter)
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Common-Emitter Bias Point](#8-example-common-emitter-bias-point)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                          | Module                                   |
| ------------------------------------------------- | ---------------------------------------- |
| How do I simulate a common-emitter amplifier?     | `BjtCommonEmitter`                       |
| How do I find the DC bias point of a BJT circuit? | `BjtCommonEmitter::computeDC`            |
| Which BJT model is used internally?               | `BjtEbersMoll` (in `devices/nonlinear/`) |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

using sim::electronics::topologies::amplifiers::BjtCommonEmitter;

BjtCommonEmitter amp(/*VCC=*/12.0, /*RC=*/470.0, /*RB=*/100e3);
amp.computeDC();
const double VC = amp.collectorVoltage();
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Header-only.** All amplifier classes live in headers and compose the
  shared Circuit construction API.
- **Cached operating point.** `computeDC` populates accessors
  (`collectorVoltage`, `collectorCurrent`, `baseVoltage`); accessors are
  RT-safe and idempotent.
- **No exceptions.** `computeDC` returns a `bool` for convergence; the
  cached fields stay at their last successful value otherwise.

---

## 4. Module Reference

### BjtCommonEmitter

**Header:** `BjtCommonEmitter.hpp`
**Purpose:** Fixed-bias NPN common-emitter amplifier.

#### API

```cpp
BjtCommonEmitter(double vcc, double rc, double rb);

[[nodiscard]] bool   computeDC();

[[nodiscard]] double vcc() const noexcept;
[[nodiscard]] double rc()  const noexcept;
[[nodiscard]] double rb()  const noexcept;

[[nodiscard]] double collectorVoltage() const noexcept;
[[nodiscard]] double baseVoltage()      const noexcept;
[[nodiscard]] double collectorCurrent() const noexcept;
```

`computeDC` is NOT RT-safe (Newton-Raphson solve). All getters are RT-safe.

---

## 5. Common Patterns

### Parameter Sweep

```cpp
for (const double VCC : {5.0, 9.0, 12.0}) {
  BjtCommonEmitter amp(VCC, /*RC=*/470.0, /*RB=*/100e3);
  if (amp.computeDC()) {
    fmt::print("VCC={:.1f}  V_C={:.4f}\n", VCC, amp.collectorVoltage());
  }
}
```

### Bias-Region Probe

```cpp
BjtCommonEmitter amp(12.0, 470.0, 100e3);
amp.computeDC();
const double V_CE = amp.collectorVoltage() - /*VE=*/0.0;
const bool   SATURATED = V_CE < 0.2;
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- All accessors (`vcc`, `rc`, `rb`, `collectorVoltage`, `baseVoltage`,
  `collectorCurrent`).

### NOT RT-Safe Functions

- Constructor (registers stamp callbacks).
- `computeDC` (Newton-Raphson solve with potential workspace allocation).

### Recommended Configuration

- Run `computeDC` during configuration; query accessors inside the RT
  loop. For sweeps, batch the `computeDC` calls outside the loop.

---

## 7. CLI Tools

- `apex_circuit_demo --circuit common-emitter` exercises this module and
  reports `V_B`, `V_C`, `V_CE`, `I_C`, `I_B`, plus a region check
  (cutoff / saturation / active). See
  [demos/apex_circuit_demo](../../../../demos/apex_circuit_demo/).

---

## 8. Example: Common-Emitter Bias Point

```cpp
#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

#include <fmt/core.h>

int main() {
  using sim::electronics::topologies::amplifiers::BjtCommonEmitter;

  BjtCommonEmitter amp(/*VCC=*/12.0, /*RC=*/470.0, /*RB=*/100e3);
  if (!amp.computeDC()) {
    fmt::print("DC solve failed\n");
    return 1;
  }
  fmt::print("V_B = {:.4f} V\n", amp.baseVoltage());
  fmt::print("V_C = {:.4f} V\n", amp.collectorVoltage());
  fmt::print("I_C = {:.4e} A\n", amp.collectorCurrent());
  return 0;
}
```

---

## 9. See Also

- [Circuit](../../circuit/README.md) - Construction API
- [devices/nonlinear/BjtEbersMoll](../../devices/nonlinear/README.md) - BJT model used internally
- [demos/apex_circuit_demo](../../../../demos/apex_circuit_demo/) - Example consumer (`--circuit common-emitter`)
