# sim_electronics_topologies_amplifiers

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
#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

using sim::electronics::topologies::amplifiers::BjtCommonEmitter;

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

## Performance

| Operation                              | Latency  | Notes                                        |
| -------------------------------------- | -------- | -------------------------------------------- |
| `BjtCommonEmitter` construction        | 0.6 us   | 3 net allocations + stamp callback registry  |
| `BjtCommonEmitter::computeDC()`        | 142 us   | Newton-Raphson DC solve of the 3-net circuit |

DC solve cost is dominated by LAPACK BLAS routines (dense LU factor +
triangular solves on the augmented MNA matrix). For batched parameter
sweeps, the per-call overhead amortizes well.

## Demo

The `apex_circuit_demo` app exercises this library via
`--circuit common-emitter`:

```bash
./build/native-linux-debug/bin/ApexCircuitDemo --circuit common-emitter
./build/native-linux-debug/bin/ApexCircuitDemo --circuit common-emitter --vcc 9 --rb-base 47e3
```

The demo reports the DC operating point (V_B, V_C, V_CE, I_C, I_B) and a
region check (cutoff / saturation / active) so you can quickly explore
how supply and bias resistor values move the operating point around.

## See Also

- [Circuit](../../circuit/README.md) -- the construction API
- [devices/nonlinear/BjtEbersMoll](../../devices/nonlinear/README.md) -- the BJT model used internally
- `apps/apex_circuit_demo/` -- example consumer (`--circuit common-emitter`)
