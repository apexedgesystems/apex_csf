# ODE Integration Math Library

**Namespace:** `apex::math::integration`
**Platform:** Cross-platform
**C++ Standard:** C++20

High-performance ODE integrators with CRTP-based zero-allocation design. Provides explicit and implicit methods for real-time and embedded use.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [RT-Safety](#3-rt-safety)
4. [API Reference](#4-api-reference)
5. [Performance](#5-performance)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Start

**Explicit Euler (simplest):**

```cpp
#include "src/utilities/math/integration/inc/ExplicitEuler.hpp"

using apex::math::integration::ExplicitEuler;
using apex::math::integration::EulerOptions;
using apex::math::integration::Status;

// Define state and derivative
using State = double;
auto f = [](const State& y, double t) -> State { return -y; };

ExplicitEuler<State> integrator;
integrator.initialize(f, /*y0=*/1.0, /*t0=*/0.0, EulerOptions{});

State y = 1.0;
double dt = 0.01;
for (int i = 0; i < 100; ++i) {
  integrator.step(y, dt, EulerOptions{});
}
// y now approximates exp(-1.0)
```

**RK4 (high accuracy):**

```cpp
#include "src/utilities/math/integration/inc/RungeKutta4.hpp"

using apex::math::integration::RungeKutta4;
using apex::math::integration::RungeKutta4Options;

using State = double;
auto f = [](const State& y, double t) -> State { return t * t; };

RungeKutta4<State> integrator;
integrator.initialize(f, /*y0=*/0.0, /*t0=*/0.0, RungeKutta4Options{});

State y = 0.0;
for (int i = 0; i < 10; ++i) {
  integrator.step(y, 0.1, RungeKutta4Options{});
}
// y = t^3/3 at t=1.0
```

**Implicit method (stiff systems):**

```cpp
#include "src/utilities/math/integration/inc/BackwardEuler.hpp"

using apex::math::integration::BackwardEuler;
using apex::math::integration::BackwardEulerOptions;

using State = double;
BackwardEulerOptions<State> opts;
opts.tolerance = 1e-8;
opts.maxIterations = 10;
opts.computeJacobian = [](const State& x, double t) -> State {
  return 1.0 - 0.1 * 2.0;  // I - dt * df/dx
};
opts.linearSolve = [](const State& J, const State& rhs) -> State {
  return rhs / J;
};
opts.converged = [](const State& delta, const State& F) -> bool {
  return std::abs(delta) < 1e-10;
};

BackwardEuler<State> integrator;
auto f = [](const State& y, double t) -> State { return 2.0 * y; };
integrator.initialize(f, 1.0, 0.0, opts);

State y = 1.0;
integrator.step(y, 0.1, opts);
```

**StateVector for multi-dimensional state:**

```cpp
#include "src/utilities/math/integration/inc/StateVector.hpp"
#include "src/utilities/math/integration/inc/RungeKutta4.hpp"

using apex::math::integration::State6;
using apex::math::integration::RungeKutta4;
using apex::math::integration::RungeKutta4Options;

// 6DOF state: position (x,y,z) + velocity (vx,vy,vz)
auto f = [](const State6& s, double t) -> State6 {
  // dx/dt = v, dv/dt = -g (simple projectile)
  return State6{s[3], s[4], s[5], 0.0, 0.0, -9.81};
};

RungeKutta4<State6> integrator;
State6 state{0, 0, 100, 10, 0, 20};  // x,y,z, vx,vy,vz
integrator.initialize(f, state, 0.0, RungeKutta4Options{});

for (int i = 0; i < 100; ++i) {
  integrator.step(state, 0.01, RungeKutta4Options{});
}
```

**Accumulator for sensor fusion:**

```cpp
#include "src/utilities/math/integration/inc/Accumulator.hpp"
#include "src/utilities/math/integration/inc/StateVector.hpp"

using apex::math::integration::Accumulator;
using apex::math::integration::MultiRateAccumulator;
using apex::math::integration::State3;

// Simple velocity accumulator from accelerometer
Accumulator<State3> velocity(State3{0, 0, 0});

// Integrate acceleration at 400 Hz
State3 accel = readAccelerometer();
velocity.accumulate(accel, 1.0 / 400.0);

// Multi-rate: IMU at 400Hz, GPS at 10Hz
MultiRateAccumulator<State3, 2> nav(State3{0, 0, 0});
nav.accumulate(0, imuAccel, 1.0 / 400.0);    // Source 0: IMU
nav.accumulate(1, gpsCorrection, 1.0 / 10.0); // Source 1: GPS
```

---

## 2. Key Features

### Integrator Methods

**Explicit Methods:**

| Method         | Order | F-evals/step | Best For                 |
| -------------- | ----- | ------------ | ------------------------ |
| ExplicitEuler  | 1     | 1            | Prototyping, non-stiff   |
| RungeKutta4    | 4     | 4            | General non-stiff        |
| RungeKutta45   | 4/5   | 7            | Adaptive non-stiff       |
| AdamsBashforth | 1-4   | 1            | Efficient multi-step     |
| Leapfrog       | 2     | 2            | Symplectic, Hamiltonian  |
| VelocityVerlet | 2     | 2            | Symplectic, velocity-dep |

**Implicit Methods (Stiff ODEs):**

| Method          | Order | Stages | Best For                     |
| --------------- | ----- | ------ | ---------------------------- |
| BackwardEuler   | 1     | 1+     | Simple stiff systems         |
| TrapezoidalRule | 2     | 1+     | Energy-conserving            |
| BDF             | 1-6   | 1+     | General stiff (industry std) |
| AdamsMoulton    | 3     | 2+     | High-accuracy multi-step     |
| SDIRK2          | 2     | 2+     | L-stable, DAE-friendly       |
| ROS2            | 2     | 2      | Moderately stiff             |
| ROS3P           | 3     | 3      | Moderately stiff             |

**Second-Order ODE Methods (y'' = f):**

| Method | Order | F-evals/step | Best For               |
| ------ | ----- | ------------ | ---------------------- |
| RKN4   | 4     | 4            | Mechanical systems     |
| RKN6   | 6     | 7            | High-accuracy dynamics |
| RKN34  | 3/4   | 4            | Adaptive mechanics     |

**Specialized Methods:**

| Method               | Purpose             | Best For                 |
| -------------------- | ------------------- | ------------------------ |
| QuaternionIntegrator | Attitude dynamics   | 6DOF, spacecraft, drones |
| Quaternion           | Unit quaternion ops | Rotation, no gimbal lock |

### Design Goals

- **Zero-allocation hot path** - CRTP design, no virtual calls
- **Template-based** - State type is user-defined
- **RT-safe APIs** - No allocations during stepping
- **Built-in statistics** - Function, Jacobian, and solver call counts

---

## 3. RT-Safety

All integrators are RT-safe during stepping. Initialization may store callbacks.

| Function       | RT-Safe | Notes                              |
| -------------- | ------- | ---------------------------------- |
| `initialize()` | YES     | Stores derivative functor          |
| `step()`       | YES     | Zero-allocation, fixed computation |
| `time()`       | YES     | Inline accessor                    |
| `stats()`      | YES     | Inline accessor                    |

Implicit methods require user-provided callbacks for Jacobian computation and linear solving. These callbacks must be RT-safe for the integrator to remain RT-safe.

---

## 4. API Reference

### IntegratorBase

All integrators inherit from `IntegratorBase<Derived, State, Options>` which provides:

```cpp
template <Derivative<State> Func>
uint8_t initialize(Func&& f, const State& x0, double t0, const Options& opts);

uint8_t step(State& x, double dt, const Options& opts);

const Stats& stats() const noexcept;
double time() const noexcept;
```

### Status Codes

```cpp
enum class Status : uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_STEP = 1,     // dt <= 0
  ERROR_MAX_ITERATIONS = 2,   // Implicit solver exceeded max iterations
  ERROR_JACOBIAN_FAILURE = 3, // Jacobian evaluation failed
  ERROR_LINEAR_SOLVER = 4,    // Linear solver failed
  ERROR_CONVERGENCE = 5,      // Did not converge
  ERROR_UNKNOWN = 255
};
```

### Statistics

```cpp
struct Stats {
  std::size_t functionEvals;    // f(x,t) calls
  std::size_t jacobianEvals;    // Jacobian evaluations
  std::size_t linearSolveCalls; // Linear solver calls
  std::size_t stepRejections;   // Invalid/rejected steps
};
```

### ImplicitOptions

Implicit integrators use `ImplicitOptions<State>` (std::function-based, NOT RT-safe):

```cpp
template <typename State>
struct ImplicitOptions {
  double tolerance = 1e-6;
  std::size_t maxIterations = 10;

  std::function<State(const State& x, double t)> computeJacobian;
  std::function<State(const State& J, const State& rhs)> linearSolve;
  std::function<bool(const State& delta, const State& F)> converged;
};
```

### ImplicitOptionsRT (RT-Safe)

For real-time applications, use `ImplicitOptionsRT<State>` with zero-allocation Delegates:

```cpp
#include "src/utilities/math/integration/inc/IntegrationOptions.hpp"

using apex::math::integration::ImplicitOptionsRT;
using apex::math::integration::BackwardEuler;

struct SolverContext {
  double stiffness;
};

State computeJacobian(void* ctx, const State& x, double t) noexcept {
  auto* c = static_cast<SolverContext*>(ctx);
  return x * c->stiffness;
}

State linearSolve(void* ctx, const State& J, const State& rhs) noexcept {
  return rhs / J;  // Simplified for scalar case
}

bool converged(void* ctx, const State& delta, const State& F) noexcept {
  return std::abs(delta) < 1e-10;
}

SolverContext ctx{2.0};
ImplicitOptionsRT<double> opts;
opts.jacobianDelegate = {computeJacobian, &ctx};
opts.linearSolveDelegate = {linearSolve, &ctx};
opts.convergedDelegate = {converged, &ctx};

BackwardEuler<double, ImplicitOptionsRT<double>> integrator;
```

---

## 5. Performance

Single-step throughput on x86-64 (15 repeats):

### Scalar ODE

| Integrator    | Order | Median (ns) | Steps/s | CV%   |
| ------------- | ----- | ----------- | ------- | ----- |
| Leapfrog      | 2     | 17          | 58.8M   | 10.8% |
| ExplicitEuler | 1     | 23          | 43.5M   | 2.4%  |
| RKN4          | 4     | 23          | 43.1M   | 5.7%  |
| RungeKutta4   | 4     | 78          | 12.8M   | 5.8%  |
| RungeKutta45  | 4-5   | 126         | 7.9M    | 18.0% |

### State Dimension Scaling (RungeKutta4)

| State Dim | Median (ns) | Steps/s |
| --------- | ----------- | ------- |
| Scalar    | 78          | 12.8M   |
| 3D        | 566         | 1.8M    |
| 6D        | 607         | 1.6M    |

### Sustained Throughput (RK4, 3D Lorenz, 10K steps)

- 38 ns/step sustained (26.6M steps/s)
- CV: 1.0%

Symplectic integrators (Leapfrog, RKN4) are 3-5x faster than general-purpose
methods (RK4, RK45) for second-order ODEs due to fewer function evaluations.

---

## 6. Testing

Run tests using the standard Docker workflow:

```bash
# Build
docker compose run --rm -T dev-cuda make debug

# Run unit tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L integration

# Run specific test
docker compose run --rm -T dev-cuda ./build/native-linux-debug/bin/tests/TestMathIntegration
```

### Test Organization

| Test File                     | Purpose                            | Tests |
| ----------------------------- | ---------------------------------- | ----- |
| `ExplicitEuler_uTest.cpp`     | Forward Euler integrator           | 4     |
| `RungeKutta4_uTest.cpp`       | Classical RK4                      | 4     |
| `RungeKutta45_uTest.cpp`      | Adaptive RK4/5 (Dormand-Prince)    | 8     |
| `BackwardEuler_uTest.cpp`     | Implicit Euler                     | 4     |
| `TrapezoidalRule_uTest.cpp`   | Crank-Nicolson                     | 4     |
| `BDF2_uTest.cpp`              | 2-step BDF                         | 5     |
| `BDF_uTest.cpp`               | Variable-order BDF (1-6)           | 6     |
| `AdamsBashforth_uTest.cpp`    | Adams-Bashforth                    | 4     |
| `AdamsMoulton_uTest.cpp`      | Adams-Moulton                      | 5     |
| `SDIRK2_uTest.cpp`            | 2-stage SDIRK                      | 5     |
| `Rosenbrock_uTest.cpp`        | ROS2, ROS3P methods                | 7     |
| `Leapfrog_uTest.cpp`          | Symplectic integrators             | 7     |
| `Quaternion_uTest.cpp`        | Quaternion types and integration   | 14    |
| `RungeKuttaNystrom_uTest.cpp` | RKN4, RKN6, RKN34 methods          | 10    |
| `StateVector_uTest.cpp`       | Fixed-size state container         | 18    |
| `Accumulator_uTest.cpp`       | Direct integration / sensor fusion | 16    |

---

## 7. See Also

- `src/utilities/concurrency/inc/Delegate.hpp` - RT-safe callback pattern used by ImplicitOptionsRT
- `src/utilities/math/legendre/` - Legendre polynomials for spectral methods
- `docs/standards/CODE_STANDARD.md` - RT-safety annotation conventions
