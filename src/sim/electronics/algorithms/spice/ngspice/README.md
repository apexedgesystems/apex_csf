# Ngspice Module

**Namespace:** `sim::electronics::algorithms::spice::ngspice`
**Platform:** Linux-only
**C++ Standard:** C++23

C++ wrapper around libngspice for golden-reference SPICE simulations used to
validate the project's native device models.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
   - [NgspiceWrapper](#ngspicewrapper) - libngspice wrapper
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [CLI Tools](#7-cli-tools)
8. [Example: Verify a MOSFET Model](#8-example-verify-a-mosfet-model)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                 | Module / API                              |
| ---------------------------------------- | ----------------------------------------- |
| Is libngspice linked at build time?      | `NgspiceWrapper::isLibngspiceAvailable()` |
| How do I load a netlist from a file?     | `NgspiceWrapper::loadNetlist`             |
| How do I load a netlist from a string?   | `NgspiceWrapper::loadNetlistFromString`   |
| How do I run a DC operating point?       | `NgspiceWrapper::runDcOperatingPoint`     |
| How do I run a transient?                | `NgspiceWrapper::runTransient`            |
| How do I read a single node voltage?     | `NgspiceWrapper::getNodeVoltage`          |
| How do I read every node voltage?        | `NgspiceWrapper::getAllNodeVoltages`      |
| How do I read a transient waveform?      | `NgspiceWrapper::getNodeWaveform`         |
| What does an `NgspiceStatus` value mean? | `toString(NgspiceStatus)`                 |

---

## 2. Quick Reference

**Header:** `src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp`

```cpp
#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"

using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;
using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;

NgspiceWrapper ngspice;
if (ngspice.loadNetlist("circuit.sp") == NgspiceStatus::OK &&
    ngspice.runDcOperatingPoint() == NgspiceStatus::OK) {
  double vout = 0.0;
  ngspice.getNodeVoltage("OUT", vout);
}
```

---

## 3. Design Principles

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

- **Verification only.** This module is a comparison tool for device-model
  validation, not a production solver. Use the native MNA stack for runtime
  simulation.
- **Optional dependency.** libngspice is detected at build time via
  `APEX_HAS_LIBNGSPICE`. When unavailable, the API returns
  `ERROR_LIBNGSPICE_NOT_AVAILABLE` so callers can degrade to fixture-based
  verification.
- **Status enums over exceptions.** Every entry point returns
  `NgspiceStatus`; no operation throws on circuit errors.

---

## 4. Module Reference

### NgspiceWrapper

**Header:** `NgspiceWrapper.hpp`
**Purpose:** C++ facade over libngspice's shared-library API.

#### Key Types

```cpp
enum class NgspiceStatus : std::uint8_t {
  OK = 0,
  ERROR_NOT_INITIALIZED,
  ERROR_NETLIST_LOAD_FAILED,
  ERROR_SIMULATION_FAILED,
  ERROR_NODE_NOT_FOUND,
  ERROR_LIBNGSPICE_NOT_AVAILABLE,
};

const char* toString(NgspiceStatus status) noexcept;
```

#### API

| Method                             | Description                             | RT-Safe |
| ---------------------------------- | --------------------------------------- | ------- |
| `isLibngspiceAvailable()` (static) | Whether libngspice is linked            | Yes     |
| `loadNetlist(path)`                | Load a netlist from a file              | No      |
| `loadNetlistFromString(s)`         | Load a netlist from an in-memory string | No      |
| `runDcOperatingPoint()`            | Run a `.op` analysis                    | No      |
| `runTransient(tStop, tStep)`       | Run a transient analysis                | No      |
| `getNodeVoltage(name, v)`          | Read one node's DC voltage              | No      |
| `getAllNodeVoltages()`             | Read every node's DC voltage            | No      |
| `getNodeWaveform(name, ts, vs)`    | Read a transient waveform               | No      |
| `getVersion()`                     | Return libngspice's version string      | No      |
| `clear()`                          | Reset wrapper state                     | No      |

#### Data Sources

- libngspice via `<ngspice/sharedspice.h>` when `APEX_HAS_LIBNGSPICE` is set.

---

## 5. Common Patterns

### Detect libngspice at Startup

```cpp
if (!NgspiceWrapper::isLibngspiceAvailable()) {
  // Use fixture-based verification instead.
  return;
}
```

### DC Operating Point

```cpp
NgspiceWrapper ng;
if (ng.loadNetlist("circuit.sp") != NgspiceStatus::OK) {
  return;
}
if (ng.runDcOperatingPoint() != NgspiceStatus::OK) {
  return;
}
double vout = 0.0;
ng.getNodeVoltage("OUT", vout);
```

### Transient Waveform

```cpp
NgspiceWrapper ng;
ng.loadNetlist("rc_circuit.sp");
ng.runTransient(/*tStop=*/1e-6, /*tStep=*/1e-9);

std::vector<double> ts, vs;
ng.getNodeWaveform("OUT", ts, vs);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions

- `NgspiceWrapper::isLibngspiceAvailable()` - trivial accessor.

### NOT RT-Safe Functions

- Every method that touches libngspice (`loadNetlist`, `runDcOperatingPoint`,
  `runTransient`, `getNodeVoltage`, `getNodeWaveform`, `clear`, `getVersion`).
  libngspice allocates, performs I/O, and runs unbounded numerical solves.

### Recommended Configuration

- Use only for offline verification, never inside a real-time loop.
- Compare against the native MNA solver in unit tests with a tolerance set by
  the expected device-model accuracy (typically 1% for first-order devices).

---

## 7. CLI Tools

None.

---

## 8. Example: Verify a MOSFET Model

```cpp
#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>
#include <string>

using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;
using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

TEST(MosfetLevel1, MatchesNgspiceAtFixedBias) {
  const std::string NETLIST = R"(
    MOSFET Level 1 Test Circuit
    M1 2 1 0 0 NMOS L=1u W=10u
    .model NMOS NMOS (VTO=0.7 KP=100u LAMBDA=0.02)
    VDS 2 0 5.0
    VGS 1 0 2.0
    .op
    .end
  )";

  NgspiceWrapper ngspice;
  ASSERT_EQ(ngspice.loadNetlistFromString(NETLIST), NgspiceStatus::OK);
  ASSERT_EQ(ngspice.runDcOperatingPoint(), NgspiceStatus::OK);

  double ngspiceIds = 0.0;
  ngspice.getNodeVoltage("2", ngspiceIds);

  MosfetLevel1Params PARAMS;
  PARAMS.vto    = 0.7;
  PARAMS.kp     = 100e-6;
  PARAMS.lambda = 0.02;
  PARAMS.w      = 10e-6;
  PARAMS.l      = 1e-6;

  MosfetLevel1 mosfet(PARAMS);
  const double OUR_IDS = mosfet.current(/*Vgs=*/2.0, /*Vds=*/5.0, /*Vbs=*/0.0);

  EXPECT_NEAR(OUR_IDS, ngspiceIds, ngspiceIds * 0.01);
}
```

---

## 9. See Also

- [Nonlinear device models](../../../devices/nonlinear/README.md) - Models verified against this wrapper
- [Intel 4004 grid](../../../chips/intel4004/grid/README.md) - Per-gate validation against ngspice
- [ngspice manual](https://ngspice.sourceforge.io/docs.html)
