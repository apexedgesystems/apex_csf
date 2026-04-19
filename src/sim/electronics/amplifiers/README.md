# sim_electronics_amplifiers

BJT amplifier circuit models built on the electronics simulation framework.

## Overview

Header-only library providing pre-built amplifier topologies that compose the
Circuit API with nonlinear device models to simulate transistor-level behavior.

## Modules

| Module             | Description                             | RT-safe             |
| ------------------ | --------------------------------------- | ------------------- |
| `BjtCommonEmitter` | Fixed-bias NPN common-emitter amplifier | After `computeDC()` |

## Dependencies

| Library                             | Purpose                             |
| ----------------------------------- | ----------------------------------- |
| `sim_electronics_circuit`           | Circuit construction and solver API |
| `sim_electronics_devices_nonlinear` | BJT Ebers-Moll device model         |

## Quick Start

```cpp
#include "src/sim/electronics/amplifiers/inc/BjtCommonEmitter.hpp"

using sim::electronics::amplifiers::BjtCommonEmitter;

// VCC=12V, RC=470 ohm, RB=100k ohm
BjtCommonEmitter amp(12.0, 470.0, 100e3);
amp.computeDC();

double vc = amp.collectorVoltage();  // Collector voltage
double ic = amp.collectorCurrent();  // Collector current (A)
double vb = amp.baseVoltage();       // Base voltage (~0.7V)
```

## Question-to-Module Matrix

| Question                                          | Module                                   |
| ------------------------------------------------- | ---------------------------------------- |
| How do I simulate a common-emitter amplifier?     | `BjtCommonEmitter`                       |
| How do I find the DC bias point of a BJT circuit? | `BjtCommonEmitter::computeDC()`          |
| What BJT model is used internally?                | `BjtEbersMoll` (in `devices/nonlinear/`) |
