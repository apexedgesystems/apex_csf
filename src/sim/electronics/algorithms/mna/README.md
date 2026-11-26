# Modified Nodal Analysis (MNA) Solvers

**Namespace:** `sim::electronics::algorithms::mna`
**Platform:** Linux (CUDA optional)
**C++ Standard:** C++23

High-performance Modified Nodal Analysis solvers for circuit simulation. Provides DC transient, AC frequency domain, dense/sparse, and GPU-accelerated batch solving with 25-31x performance over baseline.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Modules](#3-modules)
4. [Question-to-Module Matrix](#4-question-to-module-matrix)
5. [Common Workflows](#5-common-workflows)
6. [Performance](#6-performance)
7. [RT-Safety](#7-rt-safety)
8. [Requirements](#8-requirements)
9. [Testing](#9-testing)
10. [Migration](#10-migration)
11. [See Also](#11-see-also)

---

## 1. Quick Start

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

using namespace sim::electronics::algorithms::mna;

// Create sparse MNA system for 1000-net circuit
MnaSystemSparse mna(1000);

// Stamp components
mna.addConductance(nodeA, nodeB, 1.0 / resistance);
mna.addCurrent(nodeC, nodeD, current);

// Factorize, then solve G*V = I
if (mna.factorize()) {
  auto result = mna.solve();
  if (result.success) {
    double voltage = result.nodeVoltages[nodeA];
  }
}
```

**Build:**

```bash
make compose-debug
make compose-testp  # Verify tests pass
```

---

## 2. Key Features

**Analysis Types:**

- DC transient (time-domain) for digital and analog circuits
- AC frequency domain for op-amps, filters, Bode plots
- Batch solving for Monte Carlo and parameter sweeps

**Solver Options:**

- Dense DC (BLAS/LAPACK dgesv) - optimized for small circuits (<200 nets)
- Dense AC (LAPACK zgesv) - complex frequency domain (<200 nets)
- Sparse DC (KLU) - optimized for large circuits (200+ nets, up to 10,000+)
- GPU (CUDA) - single-system and batch modes

**Performance:**

- Dense DC: 31x speedup over baseline (104.7ms -> 3.33ms)
- Dense AC: 5-10x speedup from LAPACK zgesv vs naive complex Gaussian
- Sparse DC: 25x speedup over baseline (138.2ms -> 5.57ms)
- Cached LU: O(n^2) back-substitution vs O(n^3) full solve (massive win for sweeps)
- RT-safe with pre-allocated workspace (both DC and AC)

**Applications:**

- Transistor-level CPU simulation (Intel 4004, MOS 6502)
- Analog circuit design (op-amps, ADCs, filters)
- SPICE-level circuit analysis
- Real-time hardware-in-the-loop simulation

---

## 3. Modules

### MnaSystem (Dense Solver)

**RT-safe:** Yes (with workspace)
**Best for:** Small circuits (<200 nets), dense connectivity

Dense MNA solver using BLAS/LAPACK cached LU factorization. 76% of time spent in optimized BLAS/LAPACK routines.

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

MnaSystem mna(netCount);
mna.addConductance(a, b, g);
auto result = mna.solve();  // O(n^3) first solve, O(n^2) cached
```

**Key features:**

- Cached LU factorization (10x faster on repeated solves)
- Voltage source augmentation
- Direct LAPACK integration

### MnaSystemSparse (Sparse Solver)

**RT-safe:** Yes (with workspace)
**Best for:** Large circuits (200+ nets), sparse connectivity

Sparse MNA solver using KLU (SuiteSparse). Direct CSC construction, counting sort assembly. Within 1.6x of dense performance at 150 nets, scales better for larger circuits.

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

MnaSystemSparse mna(netCount);
mna.addConductance(a, b, g);
mna.factorize();            // Sparse LU factorization
auto result = mna.solve();  // Back-substitution
```

**Key features:**

- Direct CSC (Compressed Sparse Column) build
- `factorize()` is the one entry point after any change. It keeps the
  symbolic analysis while the pattern (stamped positions and voltage sources)
  is the analysed one, and the pivot order (`klu_refactor`) while it suits
  the new values: no pivot is zero and the pivots have not grown small
  beside their columns (`klu_rgrowth` at least the square root of machine
  epsilon). Otherwise it analyses or pivots anew.
- `tripletsMatch()` tells whether the stamps are those of the factors in
  hand, after any factorization; `updateRhs()` then rebuilds the right-hand
  side alone.

### AcMnaSystem (AC Frequency Domain)

**RT-safe:** Yes (with workspace)
**Best for:** Frequency response, Bode plots, AC analysis

AC MNA solver with complex impedances. Supports frequency sweeps for analog circuit design.

```cpp
#include "src/sim/electronics/algorithms/mna/inc/AcMnaSystem.hpp"

AcMnaSystem mna(netCount, omega);

// Stamp complex impedances
mna.stampConductance(a, b, g);               // Resistor (real)
mna.stampAdmittance(a, b, Complex(0, w*C));  // Capacitor (imaginary)

auto result = mna.solve();  // Complex node voltages
```

**Key features:**

- Complex admittance matrix (Y = G + jB)
- `frequencySweep()`: log-spaced points with both ends included; arguments
  that describe no sweep (a start at or below zero, an end below the start,
  no points per decade, no input voltage) return no points
- Bode plot generation (magnitude/phase)

### MnaBatchCuda (GPU Batch Solver)

**RT-safe:** No (GPU kernel launches)
**Best for:** Monte Carlo, parameter sweeps, batch Newton iteration
**Requires:** CUDA toolkit, `APEX_USE_CUDA=ON`

GPU-accelerated batch MNA solver for small-medium matrices (8-64 dimensions). Lower overhead than cuSOLVER for small systems.

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"

using namespace sim::electronics::algorithms::mna::cuda;

MnaBatchWorkspace workspace;
workspace.prepare(32, batchSize);

// Solve 1000 independent 32x32 systems in parallel
bool everySolved = solveBatchCustom(workspace, hA, hb, 32, batchSize, stream);
```

**Key features:**

- Supported dimensions: 8, 16, 32, 64, in double (`solveBatchCustom`) and
  single precision (`solveBatchCustomF32`)
- Custom kernels optimized per size
- A CUDA stream may be given; the call returns when the batch is done
- Returns true only when every system was solved. A system with no pivot
  in some column (relative to that column's largest entry) is reported in
  the optional `solved` flags and comes back as not-a-number in every
  entry; the rest of its batch is solved all the same

### StampContext (Stamping Helper)

**RT-safe:** Yes (pure stamping, no allocations)
**Best for:** Building MNA matrices from circuit components

Helper for stamping circuit elements into MNA matrices. Used internally by MnaSystem/MnaSystemSparse.

```cpp
#include "src/sim/electronics/algorithms/mna/inc/StampContext.hpp"

StampContext ctx(netCount);
ctx.stampConductance(a, b, g);       // Resistor
ctx.stampCurrent(a, b, i);           // Current source
ctx.stampVoltageSource(pos, neg, v); // Voltage source
```

---

## 4. Question-to-Module Matrix

| Question                                          | Module                                                       |
| ------------------------------------------------- | ------------------------------------------------------------ |
| How do I solve a small circuit efficiently?       | `MnaSystem` (dense DC, BLAS/LAPACK)                          |
| How do I solve a large circuit (1000+ nets)?      | `MnaSystemSparse` (sparse DC, KLU)                           |
| How do I analyze frequency response (Bode plots)? | `AcMnaSystem` (AC, complex LAPACK zgesv)                     |
| How do I run Monte Carlo parameter sweeps?        | `MnaBatchCuda` (GPU batch solver)                            |
| How do I stamp resistors/capacitors into MNA?     | `StampContext` (DC) or `AcMnaSystem` (AC)                    |
| How do I make solving RT-safe?                    | Use `MnaSolveWorkspace` (DC) or `AcMnaSolveWorkspace` (AC)   |
| When should I use dense vs sparse?                | Dense <200 nets, sparse >200 nets                            |
| How do I handle voltage sources?                  | Augmented MNA (built-in to all solvers)                      |
| How do I cache factorizations for speed?          | DC: `MnaFactorizedWorkspace`, AC: `AcMnaFactorizedWorkspace` |
| How do I optimize frequency sweeps?               | Use `AcMnaFactorizedWorkspace` for resistive circuits        |

---

## 5. Common Workflows

### Workflow 1: DC Transient Simulation (Time-Domain)

**Use case:** Digital circuits, transistor-level CPUs, dynamic analog

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

using namespace sim::electronics::algorithms::mna;

// Setup (NOT RT-safe)
const std::size_t netCount = 500;
MnaSystemSparse mna(netCount);
std::vector<double> v_prev(netCount, 0.0);
std::vector<double> v(netCount, 0.0);
std::vector<double> i_branch(sourceCount, 0.0);

// Time-stepping loop
const double dt = 1e-6;  // 1us timestep
const std::size_t steps = 1000;

for (std::size_t step = 0; step < steps; ++step) {
  mna.clearStamps();  // RT-safe: clears the stamps, keeps the factors in hand

  // Stamp resistors (RT-safe)
  for (const auto& r : resistors) {
    mna.addConductance(r.a, r.b, 1.0 / r.R);
  }

  // Stamp capacitors with backward Euler companion model (RT-safe)
  for (const auto& c : capacitors) {
    double Geq = c.C / dt;
    double Ieq = Geq * v_prev[c.a];
    mna.addConductance(c.a, c.b, Geq);
    mna.addCurrent(c.a, c.b, Ieq);
  }

  // With a constant timestep and constant resistances the matrix is the one
  // factorized on the first step: after it only the right-hand side is
  // rebuilt (RT-safe). factorize() is NOT RT-safe.
  if (mna.tripletsMatch()) {
    mna.updateRhs();
  } else if (!mna.factorize()) {
    break;
  }

  // Solve into pre-allocated buffers (RT-safe)
  mna.solveInto(v.data(), i_branch.data());

  // Update state for next step (RT-safe)
  v_prev = v;
}
```

### Workflow 2: AC Frequency Sweep (Bode Plot)

**Use case:** Op-amp gain/phase analysis, filter design

**Option A: Basic sweep (allocates per frequency)**

```cpp
#include "src/sim/electronics/algorithms/mna/inc/AcMnaSystem.hpp"

using namespace sim::electronics::algorithms::mna;

const std::size_t netCount = 50;
const NetID input = 1, output = 10;

// Frequency sweep parameters
const double startFreq = 1.0;      // 1 Hz
const double endFreq = 1e6;        // 1 MHz
const std::size_t points = 100;

std::vector<AcFrequencyPoint> bode;

for (std::size_t i = 0; i < points; ++i) {
  double freq = startFreq * std::pow(endFreq / startFreq, i / (points - 1.0));
  double omega = 2.0 * std::numbers::pi * freq;

  AcMnaSystem mna(netCount, omega);

  // Stamp circuit (resistors, capacitors, inductors with complex impedances)
  for (const auto& r : resistors) {
    mna.stampConductance(r.a, r.b, 1.0 / r.R);
  }
  for (const auto& c : capacitors) {
    mna.stampCapacitor(c.a, c.b, c.C);  // Y = j*omega*C
  }

  // Input voltage source (1V reference)
  mna.addVoltageSource(input, 0, Complex(1.0, 0.0));

  auto result = mna.solve();  // LAPACK zgesv (5-10x faster than naive)

  // Extract Bode plot data
  Complex vOut = result.nodeVoltages[output];
  double magnitude = std::abs(vOut);
  double phase = std::arg(vOut) * 180.0 / std::numbers::pi;

  bode.push_back({freq, omega, vOut, 20.0 * std::log10(magnitude), phase});
}

// Plot bode (magnitude and phase vs frequency)
```

**Option B: RT-safe sweep with workspace (no allocation in loop)**

```cpp
#include "src/sim/electronics/algorithms/mna/inc/AcMnaSystem.hpp"

using namespace sim::electronics::algorithms::mna;

const std::size_t netCount = 50;
const NetID input = 1, output = 10;

// Setup workspace (NOT RT-safe)
AcMnaSystem mna(netCount, 0.0);
AcMnaSolveWorkspace workspace;
workspace.prepare(netCount + 1);  // +1 for voltage source
std::vector<Complex> vNodes(netCount);
std::vector<Complex> vBranch(1);

const double startFreq = 1.0;
const double endFreq = 1e6;
const std::size_t points = 100;

std::vector<AcFrequencyPoint> bode;

for (std::size_t i = 0; i < points; ++i) {
  double freq = startFreq * std::pow(endFreq / startFreq, i / (points - 1.0));

  mna.setFrequency(freq);  // Update omega (RT-safe)
  mna.clear();             // Reset stamps (RT-safe)

  // Stamp circuit (RT-safe)
  for (const auto& r : resistors) {
    mna.stampConductance(r.a, r.b, 1.0 / r.R);
  }
  for (const auto& c : capacitors) {
    mna.stampCapacitor(c.a, c.b, c.C);
  }
  mna.addVoltageSource(input, 0, Complex(1.0, 0.0));

  // Solve using workspace (RT-safe)
  mna.solveInto(workspace, vNodes.data(), vBranch.data());

  Complex vOut = vNodes[output];
  double magnitude = std::abs(vOut);
  double phase = std::arg(vOut) * 180.0 / std::numbers::pi;

  bode.push_back({freq, mna.omega(), vOut, 20.0 * std::log10(magnitude), phase});
}
```

**Option C: Cached LU for resistive-dominant circuits**

When circuit has mostly resistors (topology unchanged across frequency), cache the LU factorization for massive speedup:

```cpp
// Setup (NOT RT-safe)
AcMnaSystem mna(netCount, 0.0);
AcMnaFactorizedWorkspace workspace;
workspace.prepare(netCount + 1);
std::vector<Complex> vNodes(netCount);
std::vector<Complex> vBranch(1);

// Factorize at first frequency (O(n^3), NOT RT-safe)
mna.setFrequency(startFreq);
// Stamp resistive circuit...
mna.factorize(workspace);

// Sweep loop (O(n^2) per frequency, RT-safe)
for (std::size_t i = 0; i < points; ++i) {
  double freq = startFreq * std::pow(endFreq / startFreq, i / (points - 1.0));

  mna.setFrequency(freq);
  mna.clearRHS();  // Keep matrix, clear RHS only

  // Re-stamp only frequency-dependent elements
  for (const auto& c : capacitors) {
    mna.stampCapacitor(c.a, c.b, c.C);
  }
  mna.addVoltageSource(input, 0, Complex(1.0, 0.0));

  // Back-substitution only (RT-safe, O(n^2))
  mna.solveFactorized(workspace, vNodes.data(), vBranch.data());

  // Extract results...
}

// Note: For reactive-dominant circuits or wide frequency ranges,
// refactorize periodically when matrix structure changes significantly
```

### Workflow 3: Monte Carlo Parameter Sweep (GPU Batch)

**Use case:** Component tolerance analysis, yield estimation

**Requires:** `APEX_USE_CUDA=ON`

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"

using namespace sim::electronics::algorithms::mna::cuda;

const std::size_t dim = 32;
const std::size_t batchSize = 1000;  // 1000 Monte Carlo trials

// Prepare GPU workspace (NOT RT-safe)
MnaBatchWorkspace workspace;
if (!workspace.prepare(dim, batchSize)) {
  return;  // No device, or no memory
}

// Host-side matrices (batchSize independent 32x32 systems, row-major)
std::vector<double> hA(batchSize * dim * dim);
std::vector<double> hb(batchSize * dim);
std::unique_ptr<bool[]> solved(new bool[batchSize]());  // every flag false

// Fill hA and hb with parameter-varied circuits
// (each trial has slightly different component values)

// Solve all 1000 systems in parallel on GPU (NOT RT-safe)
bool everySolved =
    solveBatchCustom(workspace, hA.data(), hb.data(), dim, batchSize, nullptr, solved.get());

// hb holds the solution of each trial whose flag is true, whatever became
// of its neighbours; a trial that was not solved (a floating node, say)
// holds not-a-number throughout. When the batch did not run to its end
// (everySolved is false then) every flag is false: read no trial without
// its flag.
for (std::size_t trial = 0; trial < batchSize; ++trial) {
  if (solved[trial]) {
    record(&hb[trial * dim]);
  }
}
```

### Workflow 4: RT-Safe Hot-Path Simulation

**Use case:** Hardware-in-the-loop, real-time control loops

```cpp
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using namespace sim::electronics::algorithms::mna;

// SETUP PHASE (NOT RT-safe - run once at initialization)
const std::size_t netCount = 100;
const std::size_t sourceCount = 1;
MnaSystem mna(netCount);
MnaSolveWorkspace workspace;
workspace.prepare(netCount + sourceCount);
std::vector<double> v(netCount);
std::vector<double> i_branch(sourceCount);
mna.addVoltageSource(supplyNode, 0, supplyVolts);  // reserves the source list

// HOT LOOP (RT-safe - runs every control cycle)
void rtControlLoop() {
  mna.clear();  // RT-safe: zeros the stamps, keeps the storage

  // Stamp circuit from sensor inputs (RT-safe)
  mna.addVoltageSource(supplyNode, 0, supplyVolts);
  for (const auto& r : resistors) {
    mna.addConductance(r.a, r.b, 1.0 / r.R);
  }

  // Solve into pre-allocated buffers (with LAPACKE: no allocations)
  if (mna.solveInto(workspace, v.data(), i_branch.data())) {
    // Use result for control output (RT-safe)
    setActuatorVoltage(v[outputNode]);
  }
}
```

This holds for a build with LAPACKE (`COMPAT_HAVE_LAPACKE=1`, the hosted
default). Without it the dense solver falls back to its own elimination,
which allocates on every solve whatever the workspace: that build has no
allocation-free dense solve.

---

## 6. Performance

Measured on x86_64 (clang-21, debug), Docker container, 15 repeats per data point.

### Sparse KLU Solver (MnaSystemSparse)

| Operation                     | Median (us) | Throughput   |
| ----------------------------- | ----------- | ------------ |
| Factorize + solve (150 nets)  | 104         | 9.6K/s       |
| Cached solve only (150 nets)  | 1.5         | 689K/s       |
| Stamp 2000 entries (150 nets) | 100         | 20M stamps/s |

### Dense LAPACK Solver (MnaSystem)

| Operation            | Median (us) | Throughput |
| -------------------- | ----------- | ---------- |
| Full solve (10 nets) | 1.8         | 571K/s     |
| Full solve (50 nets) | 18.3        | 55K/s      |

### Sparse Scaling

| Nets | Factorize + Solve (us) |
| ---- | ---------------------- |
| 10   | 9.6                    |
| 50   | 37.1                   |
| 100  | 70.7                   |
| 150  | 109                    |
| 250  | 173                    |
| 500  | 348                    |

### Multi-threaded KLU (batched MC / parameter sweeps)

For independent K-circuit batched workloads, `std::thread` work-stealing
scales near-linearly through 8 cores. Per-circuit time on the Intel 4004
matrix size (Dim ~= 1081):

| K (circuits) | T=1 (us) | T=8 (us) | Speedup |
| ------------ | -------: | -------: | ------: |
| 8            |     1.13 |     0.24 |    4.7x |
| 16           |     2.27 |     0.49 |    4.6x |
| 64           |     9.09 |     1.58 |    5.8x |

### Dense vs Sparse Crossover

| Circuit Size | Recommended | Reason                                |
| ------------ | ----------- | ------------------------------------- |
| <50 nets     | Dense       | LAPACK dgesv faster than KLU overhead |
| 50-100 nets  | Either      | Comparable performance                |
| 100+ nets    | Sparse      | KLU scales linearly, dense is O(n^3)  |

### Factorization Caching

Cached back-substitution is 45x faster than full factorize+solve at 150 nets
(1.5 us vs 65.5 us). When the stamps are those of the matrix already
factorized, only the right-hand side is rebuilt:

```cpp
mna.clearStamps();
mna.addConductance(...);
if (mna.tripletsMatch()) {
  mna.updateRhs();   // RT-safe path: 1.5 us/solve (back-substitution only)
} else {
  mna.factorize();   // Full path: 65.5 us/solve
}
mna.solveInto(nodeV, branchI);
```

`tripletsMatch()` compares with the stamps of the last factorization,
whichever way that one was done, so an earlier matrix's stamps never select
another matrix's factors. The comparison allocates nothing: `clearStamps()`
sets the factorized stamp list aside by exchanging two buffers.

When the values change on every pass (a Newton iteration), `factorize()`
keeps the pivot order and then measures the pivot growth of the result, a
pass over the matrix and its factors on top of the refactorization. Where
the growth is too small it pivots anew, which costs a full numeric
factorization for that pass.

---

## 7. RT-Safety

### RT-Safe Operations

| Operation               | RT-Safe? | Notes                                |
| ----------------------- | -------- | ------------------------------------ |
| `addConductance()`      | [OK] Yes | Pure stamping, no allocations        |
| `addCurrent()`          | [OK] Yes | Pure stamping, no allocations        |
| `addGEntry()`           | [OK] Yes | One asymmetric entry (dense)         |
| `addTransconductance()` | [OK] Yes | Controlled current source (dense)    |
| `clearStamps()`         | [OK] Yes | Sparse: clears stamps, keeps factors |
| `tripletsMatch()`       | [OK] Yes | Sparse: compares, no allocations     |
| `updateRhs()`           | [OK] Yes | Sparse: right-hand side only         |
| Sparse `solveInto()`    | [OK] Yes | After a `factorize()`                |
| Dense `solveInto()`     | [OK] Yes | Prepared workspace, LAPACKE build    |

### NOT RT-Safe

| Operation            | RT-Safe? | Notes                             |
| -------------------- | -------- | --------------------------------- |
| MNA construction     | [X] No   | Allocates matrices                |
| `solve()`            | [X] No   | Allocates matrix and result       |
| Resizing             | [X] No   | Reallocates matrices              |
| Sparse `factorize()` | [X] No   | KLU analysis and factors allocate |
| Dense, no LAPACKE    | [X] No   | Fallback elimination allocates    |
| GPU batch solving    | [X] No   | Kernel launches, memory transfers |

### RT-Safe Usage Pattern

```cpp
// SETUP PHASE (not RT-safe): stamp once and factorize. factorize() also
// sizes the storage the solves work in, so the first solve allocates nothing.
MnaSystemSparse mna(netCount);
stampCircuit(mna, inputs);
if (!mna.factorize()) {
  return;  // Singular, or a floating net
}
std::vector<double> v(netCount);
std::vector<double> i_branch(mna.voltageSourceCount());

// HOT LOOP (RT-safe while the matrix is the one factorized)
while (running) {
  mna.clearStamps();  // RT-safe: clears the stamps, keeps the factors in hand

  // Stamp the same conductances with new source values (RT-safe)
  stampCircuit(mna, inputs);

  if (!mna.tripletsMatch()) {
    break;  // The matrix changed: factorize() outside the RT path
  }
  mna.updateRhs();                           // RT-safe: right-hand side only
  mna.solveInto(v.data(), i_branch.data());  // RT-safe: back-substitution

  // Use result
  updateCircuit(v);
}
```

---

## 8. Requirements

**C++ Build Requirements:**

- C++23 compiler (clang-21 or gcc-13+)
- CMake 3.24+
- BLAS (OpenBLAS or Intel MKL)
- LAPACK
- SuiteSparse (KLU)
- Eigen3

**Optional Dependencies:**

- CUDA Toolkit 13.0+ (for GPU acceleration)
- cuSOLVER (for batch CUDA)

**Platform:**

- Linux (Ubuntu 22.04+, Debian 12+)
- CUDA support: NVIDIA GPU with compute capability 8.0+ (Ampere/Ada)

---

## 9. Testing

**Build and test:**

```bash
# Build library
make compose-debug

# Run all tests
make compose-testp

# Run this module's unit tests
docker compose run --rm dev-cuda bash -c \
  './build/hosted-x86_64-debug/bin/utests/TestSimElectronicsMna'

# Run the GPU batch solver's unit tests (needs a CUDA device)
docker compose run --rm dev-cuda bash -c \
  './build/hosted-x86_64-debug/bin/utests/TestSimElectronicsMnaBatch'
```

**Test coverage:**

- **`TestSimElectronicsMna` (CPU):**
  - Dense system: stamping, solve, cached factorization, the
    external-matrix and matvec modes and what each solve refuses
  - Sparse system: what a factorization reuses of the one before it, the
    right-hand-side-only path, singular and floating-net refusals, agreement
    with the dense system
  - Stamp context in its three modes
  - AC system: impedances, RC filters, RLC resonance, and the frequency
    sweep's points, ends and refusals
- **`TestSimElectronicsMnaAllocation` (CPU; a binary of its own, counting
  through the global allocation functions):**
  - The sparse right-hand-side path and, with LAPACKE, the dense solve into
    a prepared workspace allocate nothing, from the first pass after setup
- **`TestSimElectronicsMnaBatch` (CUDA; skipped where there is no device, and
  run by no CI job):**
  - Every supported dimension in both precisions: known solutions, systems
    that need a row exchange at every step, entries far from 1
  - A circuit's matrix with voltage sources against the dense solver
  - Systems that cannot be solved, reported one by one
  - The reported launch configuration against the launch performed

**Expected results:** All tests pass with no regressions. Sparse and dense solvers produce results within 1e-9 numerical precision.

---

## 10. Migration

The sparse system and the batch workspaces changed layout, and the batch
solvers their signature: rebuild everything that includes
`MnaSystemSparse.hpp` or `MnaBatchCuda.cuh` together with the library.
Objects compiled against the earlier headers do not link or load against it.

| Earlier                                                                 | Now                                                                                                                                                                          |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MnaSystemSparse::refactorize()`, `refactorizeInPlace()`                | `factorize()`. It keeps the analysis and the pivot order where they still apply.                                                                                             |
| `tripletsMatch()` to choose `refactorizeInPlace()`                      | `tripletsMatch()` true: `updateRhs()`. False: `factorize()`.                                                                                                                 |
| `MnaBatchWorkspace`, `MnaBatchWorkspaceF32` as two structs              | Aliases of `BatchWorkspace<double>` and `BatchWorkspace<float>`. Neither copyable nor movable; no aggregate construction. A forward declaration must name the template.      |
| `MnaBatchWorkspace::dSuccess`                                           | Gone. Pass `solved` to the solve call, or test the first entry of a system for not-a-number.                                                                                 |
| `solveBatchCustom(ws, As, bs, dim, batch, stream)` and the `F32` form   | A trailing `bool* solved = nullptr`. Calls compile as they are; a function pointer or wrapper of the six-argument type takes the seventh.                                    |
| A batch with an unsolved system returned true, that system's `bs` kept  | Returns false; that system is not-a-number in every entry and its flag is false. The others are solved.                                                                      |
| `MnaSystem::solve()`, `factorize()` on an external-matrix/matvec system | Refused. An external-matrix system solves with `solveInto()` and the workspace it stamps into, in a LAPACKE build. A matvec system holds no matrix and has nothing to solve. |
| `StampContext::clear()` left borrowed storage as it was                 | Zeroes the external matrix, and the net entries of the matvec output.                                                                                                        |
| `frequencySweep()` with equal ends, a short span, or no valid span      | One point; both ends; no points.                                                                                                                                             |

---

## 11. See Also

**Related Modules:**

- `../devices/` - Layer 2: Device physics models (MOSFETs, resistors, capacitors)
- `../circuit/` - Layer 3: Circuit assembly (net allocation, component storage)
- `../transient/` - Layer 4: Time-stepping (backward Euler integration)
- `../grids/` - Layer 5: Orchestrators (Intel4004Grid, etc.)

**External References:**

- KLU solver: [SuiteSparse documentation](https://people.engr.tamu.edu/davis/suitesparse.html)
- BLAS/LAPACK: [Netlib reference](https://www.netlib.org/)
