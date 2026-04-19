# Nonlinear Circuit Solver

Newton-Raphson solver for circuits with nonlinear devices (diodes, transistors, tunnel diodes, etc.). Computes DC operating points by iteratively linearizing devices around the current voltage estimate and solving with Modified Nodal Analysis (MNA).

## Features

- **Newton-Raphson iteration** - Quadratic convergence near solution
- **Device abstraction** - Supports any device implementing `NonlinearDevice` interface
- **Adaptive damping** - Prevents oscillation in first N iterations
- **Multiple convergence criteria** - Absolute voltage, absolute current, relative error
- **Divergence detection** - Catches unbounded voltages (unstable circuits)
- **Initial guess support** - Accelerate convergence with good starting point
- **RT-safe core** - Pre-allocated workspace for embedded systems
- **CUDA readiness** - Device evaluation is embarrassingly parallel (future)

## Status

**Production Ready** - Full feature set, comprehensive tests, complete documentation.

| Feature               | Status                 |
| --------------------- | ---------------------- |
| Newton-Raphson solver | ✅ Complete            |
| Device interface      | ✅ Complete            |
| Convergence criteria  | ✅ Complete            |
| Damping               | ✅ Complete            |
| Divergence detection  | ✅ Complete            |
| Unit tests            | ✅ Complete (15 tests) |
| Documentation         | ✅ Complete            |
| CUDA acceleration     | 🚧 Future              |

## Quick Start

```cpp
#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

using sim::electronics::nonlinear::NewtonRaphsonSolver;
using sim::electronics::nonlinear::NonlinearConfig;

// 1. Create solver for circuit with N nets
NewtonRaphsonSolver solver(netCount);

// 2. Add nonlinear devices
solver.devices().addDevice(std::make_shared<DiodeModel>(anode, cathode));

// 3. Set linear stamp callback (resistors, voltage sources)
solver.setLinearStampCallback([](MnaSystem& mna) {
  mna.addResistor(1, 0, 1000.0);
  mna.addVoltageSource(2, 0, 5.0);
});

// 4. Configure convergence
NonlinearConfig config;
config.maxIterations = 20;
config.voltageTolerance = 1e-6;

// 5. Solve for operating point
NonlinearResult result = solver.solve(config);

if (result.success()) {
  std::cout << "Converged in " << result.iterations << " iterations\n";
  std::cout << "Node voltages: ";
  for (double v : result.nodeVoltages) {
    std::cout << v << " ";
  }
}
```

## Algorithm Overview

### Newton-Raphson Method

For a circuit with nonlinear devices, we seek the voltages **V** that satisfy:

**F(V) = 0**

where **F** is the nodal residual (KCL at each node). Newton-Raphson linearizes this as:

**J Δ**V = -**F**

where **J** is the Jacobian matrix (dF/dV). The solver iterates:

1. **Linearize** all devices at current operating point **V**
2. **Stamp** linearized conductances and currents into MNA system
3. **Solve** MNA system to get voltage update **ΔV**
4. **Update** voltages: **V** ← **V** + α**ΔV** (α = damping factor)
5. **Check convergence**: if |**ΔV**| < tolerance, done; else repeat

### Convergence Criteria

The solver checks three criteria (any satisfied → converged):

| Criterion        | Formula                                        | Description                   |
| ---------------- | ---------------------------------------------- | ----------------------------- |
| Absolute voltage | max(\|ΔV\|) < `voltageTolerance`               | All voltage changes small     |
| Absolute current | max(\|I_residual\|) < `currentTolerance`       | All KCL residuals small       |
| Relative voltage | max(\|ΔV\|) / max(\|V\|) < `relativeTolerance` | Relative voltage change small |

**Default tolerances:**

- `voltageTolerance = 1e-6` V
- `currentTolerance = 1e-9` A
- `relativeTolerance = 1e-3` (0.1%)

### Damping

Newton-Raphson can oscillate when far from the solution. Damping reduces the step size:

**V** ← **V** + α**ΔV**

where α ∈ (0, 1] is the damping factor. Default: α = 0.5 for first 5 iterations, then α = 1.0.

## Device Interface

All nonlinear devices implement the `NonlinearDevice` interface:

```cpp
class NonlinearDevice {
public:
  // Terminal nets
  [[nodiscard]] virtual NetID posNet() const noexcept = 0;
  [[nodiscard]] virtual NetID negNet() const noexcept = 0;

  // I-V characteristic
  [[nodiscard]] virtual double current(double vTerminal) const noexcept = 0;
  [[nodiscard]] virtual double conductance(double vTerminal) const noexcept = 0;

  // Linearized stamp (provided by base class)
  void stampLinearized(MnaSystem& mna, double vTerminal) const;
};
```

### Linearization

Given a device I-V curve **I(V)**, linearization at operating point **V₀** gives:

**I(V) ≈ I(V₀) + g(V₀) · (V - V₀)**

where **g(V₀) = dI/dV |<sub>V₀</sub>** is the small-signal conductance. This becomes a Norton equivalent circuit:

- **Conductance**: g(V₀) (parallel)
- **Current source**: I<sub>eq</sub> = I(V₀) - g(V₀)·V₀ (parallel)

### Example Device: Ideal Diode

```cpp
class DiodeModel : public NonlinearDevice {
public:
  DiodeModel(NetID anode, NetID cathode,
             double saturationCurrent = 1e-12,
             double thermalVoltage = 0.026)
      : anode_(anode), cathode_(cathode),
        Is_(saturationCurrent), Vt_(thermalVoltage) {}

  [[nodiscard]] NetID posNet() const noexcept override { return anode_; }
  [[nodiscard]] NetID negNet() const noexcept override { return cathode_; }

  // Shockley equation: I = Is * (exp(V/Vt) - 1)
  [[nodiscard]] double current(double V) const noexcept override {
    double expArg = std::min(V / Vt_, 40.0);  // Limit to prevent overflow
    return Is_ * (std::exp(expArg) - 1.0);
  }

  // Small-signal conductance: g = dI/dV = (Is/Vt) * exp(V/Vt)
  [[nodiscard]] double conductance(double V) const noexcept override {
    double expArg = std::min(V / Vt_, 40.0);
    return (Is_ / Vt_) * std::exp(expArg);
  }

private:
  NetID anode_, cathode_;
  double Is_;  // Saturation current
  double Vt_;  // Thermal voltage (kT/q ≈ 26 mV at 300K)
};
```

## Configuration

```cpp
struct NonlinearConfig {
  std::size_t maxIterations = 20;  // Maximum Newton-Raphson iterations
  double voltageTolerance = 1e-6;  // Voltage convergence (Volts)
  double currentTolerance = 1e-9;  // Current convergence (Amps)
  double relativeTolerance = 1e-3; // Relative error tolerance

  bool enableDamping = true;         // Enable damping for oscillatory convergence
  double dampingFactor = 0.5;        // Damping factor (0.5 = half-step update)
  std::size_t dampingIterations = 5; // Apply damping for first N iterations
};
```

### Tuning Parameters

| Parameter          | Tight Tolerances  | Relaxed Tolerances |
| ------------------ | ----------------- | ------------------ |
| `voltageTolerance` | 1e-9 V            | 1e-3 V             |
| `currentTolerance` | 1e-12 A           | 1e-6 A             |
| `maxIterations`    | 50                | 10                 |
| `dampingFactor`    | 0.3 (more stable) | 0.7 (faster)       |

**Rule of thumb:**

- Digital circuits (saturated devices): relaxed tolerances OK
- Precision analog (op-amps, ADCs): tight tolerances required
- Stiff devices (tunnel diodes, SCRs): enable damping, increase iterations

## Status Codes

```cpp
enum class NonlinearStatus : uint8_t {
  SUCCESS = 0,                // Converged successfully
  ERROR_MAX_ITERATIONS = 1,   // Failed to converge within iteration limit
  ERROR_SINGULAR_MATRIX = 2,  // Jacobian matrix is singular (no solution)
  ERROR_VOLTAGE_DIVERGENCE = 3, // Voltages diverging (unstable circuit)
  ERROR_INVALID_CONFIG = 4    // Invalid configuration parameters
};
```

### Troubleshooting

| Status                     | Likely Cause                          | Fix                                                        |
| -------------------------- | ------------------------------------- | ---------------------------------------------------------- |
| `ERROR_MAX_ITERATIONS`     | Poor initial guess, tight tolerances  | Increase `maxIterations`, relax tolerances, enable damping |
| `ERROR_SINGULAR_MATRIX`    | Floating net, zero conductance        | Add ground connection, check device models                 |
| `ERROR_VOLTAGE_DIVERGENCE` | Negative resistance, unstable circuit | Check device models, reduce voltage source magnitude       |
| `ERROR_INVALID_CONFIG`     | `maxIterations = 0`                   | Set `maxIterations > 0`                                    |

## Advanced Usage

### Initial Guess

Provide a good initial guess to accelerate convergence:

```cpp
// Solve first with simplified model (all diodes OFF)
solver.solve(config);

// Use result as initial guess for full model
solver.setInitialGuess(result.nodeVoltages);
solver.devices().addDevice(complexDevice);
solver.solve(config);  // Converges faster
```

### Transient Analysis Integration

Use Newton-Raphson at each time step for circuits with reactive + nonlinear elements:

```cpp
TransientSolver transient(netCount);
NewtonRaphsonSolver nonlinear(netCount);

// Add reactive elements to transient solver
transient.companions().addCapacitor(...);

// Add nonlinear devices to Newton-Raphson solver
nonlinear.devices().addDiode(...);

// At each time step:
for (double t = 0; t < tEnd; t += dt) {
  // 1. Stamp companion models (linearized reactive elements)
  transient.companions().stampAll(mna, dt, method);

  // 2. Stamp linear elements
  stampResistors(mna);

  // 3. Solve nonlinear system
  nonlinear.setLinearStampCallback([&](MnaSystem& mna) {
    transient.companions().stampAll(mna, dt, method);
    stampResistors(mna);
  });
  NonlinearResult result = nonlinear.solve(config);

  // 4. Update companion states
  transient.companions().updateAll(result.nodeVoltages, dt);
}
```

### Device Collections

Use `NonlinearDeviceSet` to manage multiple devices:

```cpp
NonlinearDeviceSet devices;

// Add multiple diodes
for (int i = 0; i < 10; ++i) {
  devices.addDevice(std::make_shared<DiodeModel>(i+1, 0));
}

// Stamp all devices at once
std::vector<double> voltages = getCurrentOperatingPoint();
devices.stampAllLinearized(mna, voltages);

// Clear all devices
devices.clear();
```

## CUDA Acceleration (Future)

Device evaluation is embarrassingly parallel - each device computes `I(V)` and `g(V)` independently:

```cpp
// Future: Evaluate 10,000+ devices in parallel on GPU
__global__ void evaluateDevicesKernel(const Device* devices,
                                      const double* voltages,
                                      double* currents,
                                      double* conductances,
                                      int n) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < n) {
    double V = voltages[devices[idx].posNet()] - voltages[devices[idx].negNet()];
    currents[idx] = devices[idx].current(V);
    conductances[idx] = devices[idx].conductance(V);
  }
}
```

**Performance benefit**: For circuits with 10,000+ nonlinear devices, GPU evaluation provides ~100x speedup over sequential CPU evaluation.

## RT-Safety

| Component                        | RT-Safe?               | Notes                                         |
| -------------------------------- | ---------------------- | --------------------------------------------- |
| `NewtonRaphsonSolver::solve()`   | ⚠️ With pre-allocation | MNA allocates on first call, reuses workspace |
| `NonlinearDevice::current()`     | ✅ Yes                 | Pure computation, no allocation               |
| `NonlinearDevice::conductance()` | ✅ Yes                 | Pure computation, no allocation               |
| `NonlinearDeviceSet::stampAll()` | ✅ Yes                 | Iterates existing devices, no allocation      |
| `NonlinearConfig`                | ✅ Yes                 | POD struct                                    |

**Making solve() RT-safe:**

1. Call `solve()` once at initialization to allocate MNA workspace
2. Call `reset()` to clear state
3. Subsequent `solve()` calls reuse workspace (RT-safe)

## Performance

NR solve throughput (debug build, clang-21, 15-repeat median):

| Circuit                   | Median (us) | Throughput | CV%  |
| ------------------------- | ----------- | ---------- | ---- |
| Single diode (3 nets)     | 3.0         | 335K/s     | 1.1% |
| Diode voltage divider (4) | 3.4         | 292K/s     | 2.3% |
| Three-diode chain (5)     | 4.6         | 219K/s     | 4.1% |
| NL resistor network (3)   | 9.1         | 110K/s     | 2.3% |
| Re-solve with warm start  | 6.7         | 150K/s     | 0.8% |

Pipeline: 2.75 IPC, 0.08% branch miss. MNA LAPACK dense solve is 35.6% of
NR iteration cost. Device evaluation is ~3% (negligible). Scaling: iteration
count is O(1) with number of devices (local convergence). MNA solve dominates:
O(n^3) dense, O(n) sparse (n = number of nets).

## Examples

### Diode Rectifier

```cpp
// Half-wave rectifier: AC source -> Diode -> Load resistor
NewtonRaphsonSolver solver(3);  // GND, AC source, diode output

// Add diode
solver.devices().addDevice(std::make_shared<DiodeModel>(2, 1));

// Add load resistor and AC source
solver.setLinearStampCallback([t](MnaSystem& mna) {
  double vAC = 10.0 * std::sin(2 * M_PI * 60 * t);  // 60 Hz, 10V peak
  mna.addVoltageSource(1, 0, vAC);
  mna.addResistor(2, 0, 1000.0);  // 1 kΩ load
});

// Solve at each time step
for (double t = 0; t < 1.0/60; t += 1e-6) {
  NonlinearResult result = solver.solve(config);
  // result.nodeVoltages[2] = rectified output
}
```

### BJT Common-Emitter Amplifier

```cpp
// Requires BJT device model (Ebers-Moll or Gummel-Poon)
class BjtModel : public NonlinearDevice {
  // Implement current() and conductance() for base-emitter junction
};

NewtonRaphsonSolver solver(5);  // GND, VCC, base, collector, emitter

solver.devices().addDevice(std::make_shared<BjtModel>(base, emitter, collector));

solver.setLinearStampCallback([](MnaSystem& mna) {
  mna.addVoltageSource(vccNet, 0, 12.0);  // VCC = 12V
  mna.addResistor(collectorNet, vccNet, 1000.0);  // RC
  mna.addResistor(baseNet, 0, 10000.0);  // RB
});

NonlinearResult result = solver.solve(config);
// DC operating point computed
```

## Testing

```bash
# Run all nonlinear solver tests
make compose-testp --gtest_filter="*NewtonRaphson*"

# Run specific test
make compose-testp --gtest_filter="NewtonRaphsonSolver.DiodeCircuitConvergence"
```

15 comprehensive tests covering:

- Device interface and stamping
- Convergence criteria (absolute, relative)
- Damping effectiveness
- Failure modes (max iterations, divergence, singular matrix)
- Initial guess acceleration
- Determinism (repeated solves)

## References

- **Algorithm**: "Computer Methods for Circuit Analysis and Design" - Vlach & Singhal
- **SPICE Implementation**: "The SPICE Book" - Vladimirescu
- **Convergence Theory**: "Numerical Methods for Unconstrained Optimization" - Dennis & Schnabel
- **Device Models**: "Device Electronics for Integrated Circuits" - Muller & Kamins

## See Also

- [MNA Solver](../mna/README.md) - Modified Nodal Analysis (linear solver)
- [Transient Solver](../transient/README.md) - Time-domain integration
- [Companion Models](../transient/README.md#companion-models) - Reactive element discretization
