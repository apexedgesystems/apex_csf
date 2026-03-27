# Regulator Model

**Namespace:** `sim::analog`
**Platform:** Cross-platform
**C++ Standard:** C++23

LDO voltage regulator model for Monte Carlo tolerance analysis.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Module Reference](#3-module-reference)
4. [Requirements](#4-requirements)
5. [Testing](#5-testing)
6. [See Also](#6-see-also)

---

## 1. Quick Reference

```cpp
#include "src/sim/analog/regulator/inc/RegulatorModel.hpp"

sim::analog::RegulatorParams params;
params.r1 = 100000.0;  // 100k feedback upper resistor
params.r2 = 60606.0;   // 60.6k feedback lower resistor
params.cOut = 10.0e-6;  // 10uF output capacitor
params.esr = 0.010;     // 10m ESR
params.vRef = 1.25;     // 1.25V reference

const auto RESULT = sim::analog::simulate(params);
// RESULT.vOut         -> regulated output voltage (V)
// RESULT.ripple       -> peak ripple from load step (V)
// RESULT.settlingTime -> time to settle within 1% (s)
// RESULT.phaseMargin  -> stability margin (degrees)
// RESULT.inSpec       -> true if vOut within +/-3% of 3.3V
```

| Header               | RT Status   | Reason                                                |
| -------------------- | ----------- | ----------------------------------------------------- |
| `RegulatorModel.hpp` | NOT RT-safe | Floating-point math, not intended for real-time loops |

---

## 2. Design Principles

| Principle          | Rationale                                                 |
| ------------------ | --------------------------------------------------------- |
| **Closed-form**    | No ODE integration; runs in microseconds per evaluation   |
| **Deterministic**  | Same parameters always produce the same result            |
| **Physical units** | All parameters carry explicit units (ohms, farads, volts) |
| **Sweep-friendly** | Designed for parameter sweep via MonteCarloDriver         |

### Circuit Model

The regulator drives V_out such that the feedback node matches V_ref:

```
V_out = V_ref * (1 + R1/R2)
```

Transient response is modeled as a second-order system with output capacitor
and ESR. The ESR zero provides phase boost for stability analysis.

---

## 3. Module Reference

### RegulatorParams

**Header:** `RegulatorModel.hpp`
**Purpose:** Input parameters for the voltage regulator model.

#### Key Types

```cpp
struct RegulatorParams {
  double r1{100000.0};        ///< Feedback upper resistor (ohms)
  double r2{60606.0};         ///< Feedback lower resistor (ohms)
  double cOut{10.0e-6};       ///< Output capacitance (farads)
  double esr{0.010};          ///< Output cap ESR (ohms)
  double vRef{1.25};          ///< Voltage reference (volts)
  double vIn{5.0};            ///< Input voltage (volts)
  double iLoad{0.100};        ///< Load current step (amps)
  double bandwidth{100000.0}; ///< Loop bandwidth (Hz)
};
```

### RegulatorResult

**Header:** `RegulatorModel.hpp`
**Purpose:** Output results from the voltage regulator model.

#### Key Types

```cpp
struct RegulatorResult {
  double vOut{0.0};         ///< Regulated output voltage (V)
  double ripple{0.0};       ///< Peak ripple from load transient (V)
  double settlingTime{0.0}; ///< Settling time to 1% of final value (s)
  double phaseMargin{0.0};  ///< Phase margin estimate (degrees)
  bool inSpec{false};       ///< True if vOut within +/-3% of 3.3V target
};
```

### simulate

**Header:** `RegulatorModel.hpp`
**Purpose:** Run the voltage regulator model for a single parameter set.

#### API

```cpp
/**
 * @brief Run the voltage regulator model.
 * @param params Input parameters (component values with tolerances applied).
 * @return RegulatorResult with output voltage, ripple, settling, phase margin.
 * @note NOT RT-safe: Floating-point math with trig and log functions.
 * @note Deterministic: same params always produce same result.
 */
RegulatorResult simulate(const RegulatorParams& params);
```

#### Usage

```cpp
sim::analog::RegulatorParams params;
params.esr = 0.050;  // 50m ESR (high tolerance ceramic)
params.r1 = 102000.0; // R1 at +2% tolerance

const auto RESULT = sim::analog::simulate(params);
if (!RESULT.inSpec) {
  fmt::print("OUT OF SPEC: vOut={:.4f}V\n", RESULT.vOut);
}
```

---

## 4. Requirements

| Requirement  | Detail                 |
| ------------ | ---------------------- |
| Compiler     | C++23                  |
| Platform     | Any (no OS dependency) |
| Dependencies | None (header-only)     |

---

## 5. Testing

Run tests using the standard Docker workflow:

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run regulator tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L sim
```

### Test Organization

| Module         | Test File                | Tests |
| -------------- | ------------------------ | ----- |
| RegulatorModel | RegulatorModel_uTest.cpp | 13    |

### Expected Output

```
100% tests passed, 0 tests failed out of 13
```

---

## 6. See Also

- `apps/apex_mc_demo/` -- Monte Carlo regulator analysis application
- `docs/standards/CODE_STANDARD.md` -- Coding conventions
