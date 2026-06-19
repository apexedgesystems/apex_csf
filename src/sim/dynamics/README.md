# Dynamics Module

**Namespace:** `sim::dynamics::*`
**Platform:** Linux-only
**C++ Standard:** C++23

Vehicle-agnostic state-integration primitives -- numerical ODE integrators,
rigid-body equations of motion, mass-property tracking, and atmospheric
disturbance -- shared across the simulation's aircraft, rocket, and spacecraft
models.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [Integrators](#integrators) - ForwardEuler, RK4
   - [Rigid Body](#rigid-body) - PointMass3D, RigidBody6DOF
   - [Mass Properties](#mass-properties) - FuelBurnMassProperties
   - [Disturbance](#disturbance) - DrydenTurbulence
5. [Real-Time Considerations](#real-time-considerations)
6. [See Also](#see-also)

## Overview

| Question                                                           | Module                         |
| ------------------------------------------------------------------ | ------------------------------ |
| How do I advance a state vector one tick?                          | `stepRK4` / `stepForwardEuler` |
| Which integrator should I use for attitude dynamics?               | `RK4` (4th order)              |
| How do I model a translating point mass?                           | `PointMass3D`                  |
| How do I model full 6-DOF flight with attitude?                    | `RigidBody6DOF`                |
| How do I apply forces and moments in the body frame?               | `rigidBody6DOFDerivative`      |
| How do I solve `I * omega_dot = b` for an aircraft inertia tensor? | `InertiaTensor::solve`         |
| How does mass / CG / inertia change as fuel burns?                 | `FuelBurnMassProperties`       |
| How do I inject realistic atmospheric turbulence?                  | `DrydenTurbulence`             |
| How do I keep turbulence reproducible across runs?                 | `DrydenRng` (seeded)           |

## Quick Reference

| Subdomain          | Library                        | Provides                                                                                                                                  |
| ------------------ | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `integrators/`     | `sim_dynamics_integrators`     | `stepForwardEuler` (1st order), `stepRK4` (4th order) -- generic ODE step functions templated on a `State` with `operator+` / `operator*` |
| `rigid_body/`      | `sim_dynamics_rigid_body`      | `PointMass3D` (6-state translational), `RigidBody6DOF` (13-state: position, body velocity, quaternion attitude, body angular velocity)    |
| `mass_properties/` | `sim_dynamics_mass_properties` | `FuelBurnMassProperties` -- TSFC-driven fuel burn with linear CG and inertia interpolation                                                |
| `disturbance/`     | `sim_dynamics_disturbance`     | `DrydenTurbulence` -- 3-axis MIL-HDBK-1797 gust model                                                                                     |

All libraries are header-only (interface) targets. Headers live in each
subdomain's `inc/` under the `sim::dynamics::<subdomain>` namespace.

## Design Principles

| Principle              | Rationale                                                                      |
| ---------------------- | ------------------------------------------------------------------------------ |
| RT-safe step path      | Step functions allocate nothing and have bounded, branch-light control flow    |
| Header-only templates  | The math is small and templated on the caller's `State`; no link-time cost     |
| Vehicle-agnostic state | The EOM operate on plain state vectors; vehicles inject force/moment functions |
| Composable integrators | Any rigid-body model passes its derivative lambda to a generic integrator      |
| Caller owns the state  | The integrator state lives in the caller; the step mutates it in place         |

## Module Reference

### Integrators

**Header:** `integrators/inc/RK4.hpp`, `integrators/inc/ForwardEuler.hpp`
**Purpose:** Single-step explicit ODE integrators.

`stepForwardEuler` is first order (`y_{n+1} = y_n + dt * f`), cheap and stable
for slow dynamics. `stepRK4` is the classical fourth-order Runge-Kutta method
(four derivative evaluations per step) and is the default for any system with
appreciable rotation rates. Both are templated on a `State` type supporting
`operator+(State)` and `operator*(double)` and a derivative callable
`f(t, state) -> dstate/dt`.

```cpp
struct Scalar { double v; Scalar operator+(Scalar o) const; Scalar operator*(double k) const; };
auto f = [](double t, Scalar y) { return Scalar{-0.5 * y.v}; };  // dy/dt = -k y
Scalar y{1.0};
stepRK4(y, f, /*t*/ 0.0, /*dt*/ 0.01);   // RT-safe: no allocation
```

### Rigid Body

**Header:** `rigid_body/inc/PointMass3D.hpp`, `rigid_body/inc/RigidBody6DOF.hpp`
**Purpose:** Translational and full 6-DOF equations of motion.

`PointMass3D` integrates position and velocity under Newton's second law
(`p_dot = v`, `v_dot = F/m`). `RigidBody6DOF` carries a 13-element state
(inertial position, body velocity, body-to-inertial unit quaternion, body
angular velocity) and integrates the full nonlinear body-axis EOM: the
body-to-inertial kinematic transform, the transport-theorem velocity term,
the quaternion rate, and Euler's equation with an xz-symmetric
`InertiaTensor`. The attitude quaternion is renormalized
once per step to recover from the small additive drift the RK4 stages
introduce.

```cpp
RigidBody6DOFState s;
s.velocity_body = Vec3{235.0, 0.0, 0.0};
InertiaTensor I{Ixx, Iyy, Izz, Ixz};
auto force  = [](double, const RigidBody6DOFState&) { return Vec3{thrust, 0, 0}; };
auto moment = [](double, const RigidBody6DOFState&) { return Vec3{0, 0, 0}; };
stepRigidBody6DOF(s, force, moment, mass_kg, I, t, dt);  // 50 Hz tick
```

### Mass Properties

**Header:** `mass_properties/inc/FuelBurnMassProperties.hpp`
**Purpose:** Time-varying mass / CG / inertia from fuel burn.

`stepFuelBurn` drains fuel at the thrust-specific rate `mdot = TSFC * thrust`
and linearly interpolates mass, CG offset, and inertia between full-fuel and
empty-fuel reference endpoints. Fuel is clamped at zero
(no negative fuel) and burn stops at exhaustion. Struct defaults are
illustrative, notional values; set them per the vehicle being simulated.

### Disturbance

**Header:** `disturbance/inc/DrydenTurbulence.hpp`
**Purpose:** Three-axis continuous turbulence (MIL-HDBK-1797).

`stepDryden` produces body-frame gust velocities `(u_g, v_g, w_g)` by passing
seeded Gaussian white noise through filters shaped to the Dryden power spectral
densities. The longitudinal axis is a first-order filter; the lateral and
vertical axes use a two-stage cascade calibrated so the steady-state RMS of
each axis matches its `sigma` intensity. `DrydenRng` holds the generator
separately so runs are reproducible from a seed. Output freezes (no division by
airspeed) when `V < 1 m/s` or `dt <= 0`.

## Real-Time Considerations

### RT-Safe Functions

Every step function is RT-safe: bounded computation, no heap allocation, no
I/O. Measured single-step cost on a hosted x86-64 release-class build (per-op
derived from benchmark throughput):

- `stepRK4` (scalar) -- ~58 ns/step (~17 M steps/s)
- `stepPointMass3D` -- ~110 ns/step (~9 M steps/s)
- `stepRigidBody6DOF` -- ~341 ns/step (~3 M steps/s); the heaviest step
  (13 states, quaternion + Euler equations evaluated at four RK4 stages),
  compute-bound at IPC ~3.4 with negligible branch/cache misses
- `stepFuelBurn` -- ~21 ns/step (~47 M steps/s)
- `stepDryden` -- ~387 ns/step (~3 M steps/s); dominated by the three Gaussian
  RNG draws per step, not the filter arithmetic

At a 100 Hz tick a full 6-DOF + fuel + turbulence vehicle update is well under
a microsecond, a negligible fraction of the frame budget.

### Configuration Notes

- `DrydenRng` is per-instance; give each vehicle its own seeded generator for
  independent, reproducible gust streams.
- Mass and the inertia tensor are held constant across a single integrator
  step; recompute them between ticks via `FuelBurnMassProperties` if modeling
  fuel burn.

## See Also

- `environment/README.md` -- sibling simulation subsystem
- MIL-HDBK-1797 -- Dryden turbulence specification (public)
