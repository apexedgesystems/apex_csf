# Transient Circuit Simulation

Time-domain circuit simulation engine with multiple integration methods for reactive elements (capacitors, inductors).

**Status:** [OK] Production-ready
**Tests:** 21/21 passing (unit tests cover all integration methods)
**RT-Safety:** Header-only core is RT-safe with pre-allocated workspace
**Performance:** GPU acceleration for companion evaluation (10,000+ elements)
**Integration Methods:** Backward Euler, Trapezoidal, GEAR2 (BDF2)

---

## Quick Start

```cpp
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"

using namespace sim::electronics::algorithms::transient;

// 1. Create solver
TransientSolver solver(netCount);

// 2. Add reactive elements
solver.companions().addCapacitor(nodeA, nodeB, 1e-6); // 1uF
solver.companions().addInductor(nodeC, nodeD, 1e-3);  // 1mH

// 3. Set stamp callback for resistors, sources
solver.setStampCallback([](MnaSystem& mna, double time) {
  mna.stampConductance(1, 0, 1e3);     // 1kOhm resistor
  mna.stampVoltageSource(0, 2, 0, 5.0); // 5V source
});

// 4. Configure simulation with integration method
TransientConfig config{
  .tStart = 0.0,
  .tEnd = 1e-3,      // 1ms
  .tStep = 1e-6,     // 1us steps
  .method = IntegrationMethod::TRAPEZOIDAL  // Energy-conserving (oscillators)
  // Options: BACKWARD_EULER (fast, stable), TRAPEZOIDAL (accurate), GEAR2 (stiff)
};

auto result = solver.run(config, /*recordHistory=*/true);

// 5. Access results
for (const auto& state : result.history) {
  std::cout << "t=" << state.time << " V1=" << state.nodeVoltages[1] << "\n";
}
```

---

## Libraries

| Library                                     | Type                    | Purpose          |
| ------------------------------------------- | ----------------------- | ---------------- |
| `sim_electronics_algorithms_transient`      | INTERFACE (header-only) | CPU solver       |
| `sim_electronics_algorithms_transient_cuda` | SHARED (optional)       | GPU acceleration |

**CMake:**

```cmake
target_link_libraries(my_circuit
  PRIVATE sim_electronics_algorithms_transient
)

# Optional GPU acceleration (requires APEX_USE_CUDA=ON)
if (APEX_USE_CUDA)
  target_link_libraries(my_circuit
    PRIVATE sim_electronics_algorithms_transient_cuda
  )
endif()
```

---

## Core API

### TransientSolver

**Construction:**

```cpp
TransientSolver(std::size_t netCount);  // NOT RT-safe: allocates
```

**Configuration:**

```cpp
// Add reactive elements
solver.companions().addCapacitor(NetID nA, NetID nB, double C);
solver.companions().addInductor(NetID nA, NetID nB, double L);

// Stamp static elements
solver.setStampCallback(StampCallback cb);
solver.setStatefulStampCallback(StatefulStampCallback cb); // With prevVoltages

// Optimization flags
solver.setCachedLU(true);   // Reuse LU factorization (constant topology)
solver.setDualLU(true);     // Alternating topologies (e.g., clock HIGH/LOW)
solver.setSparse(true);     // Sparse LU for low-fill circuits (< 5%)
```

**Simulation:**

```cpp
// Full run
TransientResult run(const TransientConfig& config, bool recordHistory = false);

// Single step (RT-safe with pre-allocated workspace)
TransientStatus step(double dt, TransientState& state);

// DC operating point
TransientStatus computeDC(TransientState& state);
```

**State Management:**

```cpp
double time() const;              // Current simulation time
void reset();                     // Reset for new simulation
void invalidateCache();           // Topology changed
void setIntegrationMethod(IntegrationMethod method);  // Set discretization
IntegrationMethod integrationMethod() const;          // Get current method
```

---

## Integration Methods

The solver supports three integration methods for discretizing reactive elements:

| Method             | Order | Stability | Energy      | Use Case                         |
| ------------------ | ----- | --------- | ----------- | -------------------------------- |
| **BACKWARD_EULER** | 1st   | A-stable  | Dissipative | Digital circuits, fast settling  |
| **TRAPEZOIDAL**    | 2nd   | A-stable  | Conserving  | Analog filters, oscillators      |
| **GEAR2 (BDF2)**   | 2nd   | L-stable  | Damping     | Stiff systems, mixed time scales |

**Key differences:**

- **Backward Euler:** First-order accurate, unconditionally stable, but introduces numerical damping (energy dissipates). Best for digital circuits where exact energy conservation isn't critical.
- **Trapezoidal:** Second-order accurate, energy-conserving (symplectic). Ideal for LC oscillators, filters, and analog circuits where amplitude preservation matters.
- **GEAR2:** Second-order backward differentiation formula (BDF2), L-stable with superior damping of high-frequency oscillations. Best for stiff systems with widely separated time scales.

**Example - LC oscillator comparison:**

```cpp
// Backward Euler: amplitude decays ~5% per cycle (numerical damping)
config.method = IntegrationMethod::BACKWARD_EULER;

// Trapezoidal: amplitude stays constant (energy conserving)
config.method = IntegrationMethod::TRAPEZOIDAL;

// GEAR2: amplitude controlled decay (stiff system stability)
config.method = IntegrationMethod::GEAR2;
```

**Performance:** All methods have same computational cost per step (same matrix solve).
**Accuracy:** Trapezoidal and GEAR2 provide 2x better accuracy than Backward Euler for same step size.

---

## GPU Acceleration

**When to use:** Circuits with **100+ nets** where GPU parallelism outweighs transfer overhead.

**Free functions** (no wrapper class):

```cpp
#include "src/sim/electronics/algorithms/transient/inc/TransientCuda.cuh"

using namespace sim::electronics::algorithms::transient;

// 1. Prepare GPU workspace
mna::cuda::MnaCudaWorkspace cudaWs;
cudaWs.prepare(netCount + 16);

// 2. Use stepCuda() for GPU-accelerated solving
MnaSystem mna(netCount);
CompanionSet companions;
mna::MnaSolveWorkspace workspace;
double time = 0.0;
std::vector<double> prevVoltages;

auto status = cuda::stepCuda(
  cudaWs, mna, companions, stampCallback,
  dt, time, prevVoltages, workspace, state
);

if (status != TransientStatus::SUCCESS) {
  // GPU failed, fall back to CPU
  solver.step(dt, state);
}
```

**Availability check:**

```cpp
if (transient::cuda::available()) {
  // GPU path available
}
```

### Companion Evaluation on GPU

For circuits with **10,000+ capacitors/inductors**, parallel companion evaluation provides ~100x speedup:

```cpp
#include "src/sim/electronics/algorithms/transient/inc/CompanionSetCuda.hpp"

using namespace sim::electronics::algorithms::transient;

CompanionSet companions;
// ... add 10,000 capacitors ...

std::vector<double> capGeq(companions.capacitorCount());
std::vector<double> capIeq(companions.capacitorCount());

// Evaluate all companions in parallel on GPU
cuda::evaluateCapacitorsCuda(
  companions.capacitor(0),        // Array base pointer
  companions.capacitorCount(),    // Number of capacitors
  dt,                             // Time step
  IntegrationMethod::TRAPEZOIDAL, // Integration method
  capGeq.data(),                  // Output: conductances
  capIeq.data()                   // Output: current sources
);

// Use geq/ieq for stamping...
```

**When to use:**

- Circuits with 10,000+ reactive elements
- Batch processing (multiple circuits)
- Real-time simulation with many components

**Limitations:**

- Requires CUDA-capable GPU
- Allocation overhead (use for repeated evaluations)

---

## Companion Models

**Capacitor:**

```cpp
companions().addCapacitor(nodeA, nodeB, C);

// Backward Euler: C/dt conductance + I = C/dt * V_prev current source
```

**Inductor:**

```cpp
companions().addInductor(nodeA, nodeB, L);

// Backward Euler: dt/L conductance + I = dt/L * V_prev current source
```

**Access:**

```cpp
CompanionSet& companions();           // Mutable access
const CompanionSet& companions() const; // Read-only
```

---

## Performance

Step throughput (debug build, clang-21, 15-repeat median):

| Circuit                  | Median (us) | Per-Step | CV%  |
| ------------------------ | ----------- | -------- | ---- |
| BE step, 1 RC (4 nets)   | 0.83        | 0.83 us  | 7.2% |
| BE step, 10 RC (12 nets) | 2.31        | 2.31 us  | 1.6% |
| BE step, 50 RC (52 nets) | 17.27       | 17.27 us | 4.3% |
| Trap step, 1 RC (4 nets) | 0.77        | 0.77 us  | 6.9% |
| RC transient, 100 steps  | 50.0        | 500 ns   | 0.5% |
| RC transient 10x, 100 st | 182.1       | 1821 ns  | 2.7% |

Pipeline: 3.43 IPC, 0.01% branch miss. LAPACK dense solve is 40% of step
cost. Companion evaluation is ~10%. Trapezoidal is 6-13% slower than backward
Euler. Step cost scales linearly with net count (dense LU).

## Performance Optimization

### Sparse Solver

For circuits with **< 5% matrix fill** (e.g., grid topologies):

```cpp
solver.setSparse(true);
```

**Benefits:**

- 10-50x speedup over dense LAPACK
- Only processes non-zero entries
- Example: 150-net Apex4Grid (2% fill) -> 5.6ms vs 138ms

### Cached LU

For **constant topology** circuits:

```cpp
solver.setCachedLU(true);
```

**Benefits:**

- 6-8x speedup via LU reuse
- O(n^2) back-substitution vs O(n^3) factorization
- **Important:** Call `invalidateCache()` if topology changes

### Dual-LU Caching

For **alternating topologies** (e.g., clocked circuits):

```cpp
solver.setDualLU(true);

// In simulation loop:
int stateIdx = (clockHigh) ? 0 : 1;
solver.stepDual(dt, stateIdx, state);
```

**Benefits:**

- Caches LU factors for both states
- O(n^2) once both factorized
- Ideal for clock-driven digital circuits

---

## Stateful Stamping

For components that depend on **previous voltages** (e.g., digital registers):

```cpp
solver.setStatefulStampCallback(
  [](MnaSystem& mna, double time, const std::vector<double>& prevVoltages) {
    // Read previous voltage at node 5
    double vPrev = prevVoltages[5];

    // Stamp based on previous state
    if (vPrev > 2.5) {
      mna.stampConductance(5, 0, 1e-3); // Pull-down
    } else {
      mna.stampConductance(5, 3, 1e-3); // Pull-up
    }
  }
);
```

**Use cases:**

- Digital flip-flops
- Full adders
- State-dependent resistors

---

## RT-Safety

| Function                          | RT-Safe | Notes                          |
| --------------------------------- | ------- | ------------------------------ |
| `TransientSolver()`               | [X]      | Allocates workspace            |
| `addCapacitor()`, `addInductor()` | [X]      | Resizes internal vectors       |
| `setStampCallback()`              | [OK]      | Copies function object (small) |
| `run()`                           | [X]      | May allocate history           |
| `step()`                          | [OK]      | **If workspace pre-allocated** |
| `computeDC()`                     | [OK]      | Uses pre-allocated workspace   |

**RT-safe workflow:**

1. Construct solver and add elements during **setup phase**
2. Call `run()` once to **pre-allocate workspace**
3. Call `step()` in **RT loop** (no allocations)

---

## Examples

### RC Step Response

```cpp
TransientSolver solver(2); // Nodes: 0=GND, 1=V_out

// RC circuit: V_in --[1kOhm]-- V_out --[1uF]-- GND
solver.companions().addCapacitor(1, 0, 1e-6);
solver.setStampCallback([](MnaSystem& mna, double time) {
  mna.stampVoltageSource(0, 0, 0, 5.0);  // 5V step
  mna.stampConductance(0, 1, 1e3);       // 1kOhm
});

TransientConfig config{.tStart = 0.0, .tEnd = 5e-3, .timeStep = 1e-5};
auto result = solver.run(config, true);

// Time constant: tau = R*C = 1ms
// V_out(t) = 5*(1 - e^(-t/tau))
```

### RLC Oscillator

```cpp
TransientSolver solver(2);

// Series RLC: V_in --[10Ohm]--[1mH]-- V_out --[100nF]-- GND
solver.companions().addInductor(0, 1, 1e-3);
solver.companions().addCapacitor(1, 0, 100e-9);
solver.setStampCallback([](MnaSystem& mna, double time) {
  mna.stampVoltageSource(0, 0, 0, 10.0); // 10V step
  mna.stampConductance(0, 1, 10.0);      // 10Ohm damping
});

// Natural frequency: omega0 = 1/sqrt(LC) ~= 31.6 kHz
// Damping ratio: zeta = Rsqrt(C/L)/2 ~= 0.158 (underdamped)

TransientConfig config{.tStart = 0.0, .tEnd = 1e-4, .timeStep = 1e-7};
auto result = solver.run(config, true);
```

### GPU-Accelerated Large Circuit

```cpp
// 200-net power distribution network
TransientSolver cpuSolver(200);
mna::cuda::MnaCudaWorkspace gpuWs;
gpuWs.prepare(216); // 200 + 16 voltage sources

// ... configure circuit ...

TransientState state;
for (double t = 0; t < 1e-3; t += 1e-6) {
  // Try GPU first (100+ nets)
  if (cuda::stepCuda(gpuWs, mna, companions, callback,
                     1e-6, t, prevVoltages, workspace, state)
      == TransientStatus::SUCCESS) {
    // GPU success
  } else {
    // Fall back to CPU
    cpuSolver.step(1e-6, state);
  }
}
```

---

## Error Handling

```cpp
enum class TransientStatus {
  SUCCESS,
  ERROR_STEP_FAILED,     // MNA solve failed (singular matrix)
  ERROR_DC_FAILED,       // DC operating point failed
  ERROR_INVALID_CONFIG   // Invalid time step or range
};

auto result = solver.run(config);
if (result.status != TransientStatus::SUCCESS) {
  // Handle error
}
```

**Common failures:**

- Singular matrix: Check for floating nodes, missing ground
- DC convergence: Initial conditions too far from solution
- Timestep too large: Reduce for stiff systems

---

## Testing

```bash
# Build and run tests
make compose-debug
make compose-testp

# Test target
./build/native-linux-debug/bin/tests/TestSimElectronicsAlgorithmsTransient
```

**Coverage:** All tests pass (13/13)

- RC/RLC step response
- Multiple integration methods
- Cached LU optimization
- Sparse solver path
- Companion model accuracy

---

## Dependencies

**Core library:**

- `sim_electronics_algorithms_mna` - MNA DC solver
- `fmt::fmt` - Formatting (header-only mode)
- `utilities_compatibility` - Compatibility shims

**CUDA library** (optional):

- `sim_electronics_algorithms_mna_cuda` - GPU MNA solver
- CUDA Toolkit 11.0+ (cuSOLVER, cuBLAS)

---

## See Also

- **MNA Library:** `../mna/README.md` - DC/AC circuit analysis
- **Companion Models:** `inc/CompanionModels.hpp` - Reactive element theory
