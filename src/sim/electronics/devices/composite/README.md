# Composite Devices Module

**Namespace:** `sim::electronics::devices::composite`
**Platform:** Linux-only
**C++ Standard:** C++23

CMOS logic-gate building blocks (Inverter, NAND, NOR) assembled from MOSFET
primitives. Provides both transistor-level circuit construction and a
boolean truth-table reference for verification.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [CmosInverter](#cmosinverter) - 2-transistor inverter
   - [CmosNand](#cmosnand) - 4-transistor NAND
   - [CmosNor](#cmosnor) - 4-transistor NOR
   - [CompositeDevices](#compositedevices) - Aggregate registry header
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Build an XOR from NANDs](#8-example-build-an-xor-from-nands)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                                | Module / API               |
| ------------------------------------------------------- | -------------------------- |
| How do I build a CMOS inverter?                         | `CmosInverter`             |
| How do I build a CMOS NAND?                             | `CmosNand`                 |
| How do I build a CMOS NOR?                              | `CmosNor`                  |
| How do I verify gate-level behavior without simulating? | `truthTable(...)` (static) |
| How do I import every gate at once?                     | `CompositeDevices.hpp`     |

---

## 2. Quick Reference

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

using namespace sim::electronics::devices::composite;

CmosInverter inv{/*vdd=*/1, /*gnd=*/0, /*in=*/2, /*out=*/3, /*W=*/10e-6, /*L=*/1e-6};
const int OUT = CmosInverter::truthTable(/*in=*/0);  // 1
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Two faces.** Each gate exposes a transistor-level layout (pos/neg nets,
  W and L) and a `truthTable` static method. The first feeds the analog
  simulator; the second is a verification oracle.
- **Pure construction.** Gate types are PODs; they hold net IDs and geometry,
  nothing else. Stamping is delegated to the MOSFET primitives.
- **No allocation.** `truthTable` is `constexpr`-friendly; gate constructors
  store integers and doubles only.

---

## 4. Module Reference

### CmosInverter

**Header:** `CmosInverter.hpp`
**Purpose:** Two-transistor (PMOS pull-up + NMOS pull-down) inverter.

#### API

```cpp
struct CmosInverter {
  NetID vddNet, gndNet, inNet, outNet;
  double width;
  double length;

  static constexpr int truthTable(int a) noexcept;
};
```

#### Usage

```cpp
CmosInverter inv{vdd, gnd, in, out, /*W=*/10e-6, /*L=*/1e-6};
EXPECT_EQ(CmosInverter::truthTable(0), 1);
EXPECT_EQ(CmosInverter::truthTable(1), 0);
```

### CmosNand

**Header:** `CmosNand.hpp`
**Purpose:** Four-transistor NAND (2 PMOS parallel pull-up, 2 NMOS series
pull-down).

#### API

```cpp
struct CmosNand {
  NetID vddNet, gndNet, inANet, inBNet, outNet, internalNet;
  double width;
  double length;

  static constexpr int truthTable(int a, int b) noexcept;
};
```

#### Usage

```cpp
CmosNand nand{vdd, gnd, a, b, out, mid, 10e-6, 1e-6};
EXPECT_EQ(CmosNand::truthTable(1, 1), 0);
```

### CmosNor

**Header:** `CmosNor.hpp`
**Purpose:** Four-transistor NOR (2 PMOS series pull-up, 2 NMOS parallel
pull-down).

#### API

```cpp
struct CmosNor {
  NetID vddNet, gndNet, inANet, inBNet, outNet, internalNet;
  double width;
  double length;

  static constexpr int truthTable(int a, int b) noexcept;
};
```

#### Usage

```cpp
CmosNor nor_{vdd, gnd, a, b, out, mid, 10e-6, 1e-6};
EXPECT_EQ(CmosNor::truthTable(0, 0), 1);
```

### CompositeDevices

**Header:** `CompositeDevices.hpp`
**Purpose:** Single include that pulls in every composite gate.

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"
```

---

## 5. Common Patterns

### Truth-Table Verification

```cpp
EXPECT_EQ(CmosNand::truthTable(0, 0), 1);
EXPECT_EQ(CmosNand::truthTable(0, 1), 1);
EXPECT_EQ(CmosNand::truthTable(1, 0), 1);
EXPECT_EQ(CmosNand::truthTable(1, 1), 0);
```

### Universal Gates

NAND and NOR can synthesize any boolean function. The truth-table API makes
the substitution explicit:

```cpp
// NOT from NAND
const int NOT_A = CmosNand::truthTable(A, A);

// AND from NAND
const int NAND_AB = CmosNand::truthTable(A, B);
const int AND_AB  = CmosNand::truthTable(NAND_AB, NAND_AB);

// OR from NAND
const int OR_AB = CmosNand::truthTable(CmosNand::truthTable(A, A),
                                       CmosNand::truthTable(B, B));
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- All gate constructors (POD aggregate init).
- All `truthTable` static methods (constexpr-friendly).

### NOT RT-Safe Functions

- None within this module; stamping into an MNA system follows the rules of
  the underlying device primitives.

---

## 7. CLI Tools

None.

---

## 8. Example: Build an XOR from NANDs

```cpp
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

#include <fmt/core.h>

int main() {
  using sim::electronics::devices::composite::CmosNand;

  const int A = 1;
  const int B = 0;
  const int NAND_AB = CmosNand::truthTable(A, B);
  const int NAND_A  = CmosNand::truthTable(A, NAND_AB);
  const int NAND_B  = CmosNand::truthTable(B, NAND_AB);
  const int XOR_OUT = CmosNand::truthTable(NAND_A, NAND_B);

  fmt::print("XOR({}, {}) = {}\n", A, B, XOR_OUT);
  return 0;
}
```

---

## 9. See Also

- [Nonlinear devices](../nonlinear/README.md) - MOSFET primitives
- [Descriptors](../descriptors/README.md) - Net / topology structures
- [MNA library](../../algorithms/mna/README.md) - Circuit solver
