# Filters

**Namespace:** `sim::electronics::topologies::filters`
**Platform:** Linux-only
**C++ Standard:** C++23

Analog filter circuit models built on top of the [Circuit](../../circuit/README.md)
construction API. Each model is a thin wrapper that allocates nets, registers
stamps for the primitive elements (resistors + voltage sources), and adds
reactive element companions (capacitors / inductors).

---

## Models

| Model       | Header              | Topology                       |
| ----------- | ------------------- | ------------------------------ |
| `RcLowPass` | `inc/RcLowPass.hpp` | First-order RC low-pass filter |

The filter set is intentionally small. Users add new models by following the
same pattern: a struct that owns a `Circuit`, exposes setters for tunable
parameters, and provides analytical reference functions for validation.

---

## RcLowPass

First-order RC low-pass filter:

```
IN ---[R]---+--- OUT
            |
           [C]
            |
           GND
```

**Transfer function:** `H(s) = 1 / (1 + sRC)`
**Cutoff frequency:** `f_c = 1 / (2*pi*R*C)`
**Time constant:** `tau = R*C`
**Step response:** `V_out(t) = V_in * (1 - exp(-t/tau))`

### Quick Example

```cpp
#include "src/sim/electronics/topologies/filters/inc/RcLowPass.hpp"

using sim::electronics::topologies::filters::RcLowPass;

// 1 kohm, 1 uF -> tau = 1 ms, fc ~159 Hz
RcLowPass filter(1e3, 1e-6);
filter.build();

// Apply 5V step input
filter.setInputVoltage(5.0);

// Step the underlying transient solver
TransientState state;
state.resize(filter.circuit().netCount(), 0);
filter.circuit().computeDC(state);

const double DT = 10e-6;
for (int i = 0; i < 500; ++i) {
  filter.circuit().solver().step(DT, state);
}

double v_out = state.nodeVoltages[filter.outNet()];
```

### API

| Function                                 | Purpose                             |
| ---------------------------------------- | ----------------------------------- |
| `RcLowPass(R, C)`                        | Construct filter, allocate nets     |
| `void build()`                           | Finalize underlying TransientSolver |
| `void setInputVoltage(V)`                | Set DC input voltage                |
| `double cutoffHz() const`                | `f_c = 1 / (2 * pi * R * C)`        |
| `double tau() const`                     | Time constant `R * C`               |
| `Circuit& circuit()`                     | Access underlying Circuit           |
| `NetID inNet() / outNet() const`         | Net IDs for probing                 |
| `double analyticalStepResponse(V_in, t)` | Closed-form `V_out(t)`              |
| `double analyticalMagnitudeResponse(f)`  | Closed-form magnitude `H(j*2*pi*f)` |

The analytical helpers are provided for self-validation: every unit test
compares the simulated step response against `analyticalStepResponse()` and
matches within 0.2% across the full 0..5\*tau window.

**RT-safety:** RT-safe after `build()`. Construction allocates.

---

## Performance

Measured with `Filters_PTEST --repeats 15` (debug build, clang-21, x86_64):

| Operation                 | Median  | Throughput | Notes                    |
| ------------------------- | ------- | ---------- | ------------------------ |
| DC solve (build + solve)  | 4.1 us  | 242 K/s    | End-to-end construction  |
| 100-step transient        | 62.1 us | 16.1 K/s   | 0.62 us/step             |
| Analytical sweep (1K pts) | 35.9 us | 27.8 K/s   | 36 ns/point (2 fn/point) |

The transient step cost (0.62 us) is dominated by the LAPACK direct solve
(dgesv) in the underlying MNA system. The analytical functions are pure
math (exp, sqrt) with no allocation.

---

## Dependencies

| Library                          | Why              |
| -------------------------------- | ---------------- |
| `sim_electronics_circuit`        | Construction API |
| `sim_electronics_devices_linear` | ResistorModel    |

Header-only.

---

## Testing

```bash
make compose-debug
make compose-testp                              # Runs all electronics tests
ctest -R TestSimElectronicsFilters              # Just filter tests
```

| Test                                      | Validates                                         |
| ----------------------------------------- | ------------------------------------------------- |
| `RcLowPass.ConstructionStoresValues`      | Cutoff and tau computed from R, C                 |
| `RcLowPass.DcOperatingPoint`              | `V_out == V_in` at steady state                   |
| `RcLowPass.StepResponseMatchesAnalytical` | Sim within 0.2% of analytical at 0.5/1/5 tau      |
| `RcLowPass.AnalyticalStepResponse`        | Closed-form formula sanity                        |
| `RcLowPass.MagnitudeResponseAtCutoff`     | -3 dB at `f_c`                                    |
| `RcLowPass.MagnitudeResponseLimits`       | Magnitude approaches 1 at DC, 0 at high frequency |

---

## Demo

The `apex_circuit_demo` app exercises this library via `--circuit rc-lowpass`:

```bash
./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass
./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass --r 10e3 --c 1e-6
./build/native-linux-debug/bin/ApexCircuitDemo --circuit rc-lowpass --vstep 3.3 --duration 5e-3
```

The demo reports both the simulated transient step response (compared
against the closed-form analytical solution) and the analytical magnitude
response at key frequencies, confirming `-3 dB` at the cutoff frequency.

---

## See Also

- [Circuit](../../circuit/README.md) -- the construction API used by every model
- [devices/linear](../../devices/linear/README.md) -- ResistorModel and friends
- `apps/apex_circuit_demo/` -- example consumer (`--circuit rc-lowpass`)
