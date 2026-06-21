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
   - [Mass Properties](#mass-properties) - MassAccumulator, FuelTankMassSource
   - [Force / Moment](#force--moment) - ForceMomentAccumulator, ForceMomentSource
   - [Disturbance](#disturbance) - DrydenTurbulence
   - [Vehicle Step](#vehicle-step) - aggregate-consuming 6-DOF overload
5. [Real-Time Considerations](#real-time-considerations)
6. [See Also](#see-also)

## Overview

| Question                                                           | Module                                |
| ------------------------------------------------------------------ | ------------------------------------- |
| How do I advance a state vector one tick?                          | `stepRK4` / `stepForwardEuler`        |
| Which integrator should I use for attitude dynamics?               | `RK4` (4th order)                     |
| How do I model a translating point mass?                           | `PointMass3D`                         |
| How do I model full 6-DOF flight with attitude?                    | `RigidBody6DOF`                       |
| How do I apply forces and moments in the body frame?               | `rigidBody6DOFDerivative`             |
| How do I solve `I * omega_dot = b` for an aircraft inertia tensor? | `InertiaTensor::solve`                |
| How do I combine per-part mass / CG / inertia into a whole body?   | `MassAccumulator`                     |
| How does mass / CG / inertia change as fuel burns?                 | `FuelTankMassSource`                  |
| How do I sum forces-at-points into a net force + moment?           | `ForceMomentAccumulator`              |
| How do I step 6-DOF straight from the aggregates?                  | `vehicle::step(state, mp, fm, t, dt)` |
| How do I inject realistic atmospheric turbulence?                  | `DrydenTurbulence`                    |
| How do I keep turbulence reproducible across runs?                 | `DrydenRng` (seeded)                  |

## Quick Reference

| Subdomain          | Library                        | Provides                                                                                                                                   |
| ------------------ | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `integrators/`     | `sim_dynamics_integrators`     | `stepForwardEuler` (1st order), `stepRK4` (4th order) -- generic ODE step functions templated on a `State` with `operator+` / `operator*`  |
| `rigid_body/`      | `sim_dynamics_rigid_body`      | `PointMass3D` (6-state translational), `RigidBody6DOF` (13-state: position, body velocity, quaternion attitude, body angular velocity)     |
| `mass_properties/` | `sim_dynamics_mass_properties` | `MassAccumulator` (parallel-axis whole-body mass props), `MassPropsSource` / `StaticMassSource` / `FuelTankMassSource` (composition layer) |
| `force_moment/`    | `sim_dynamics_force_moment`    | `ForceMomentAccumulator` (net force + moment about a point), `ForceMomentSource` / `StaticForceMomentSource` / `DynamicForceMomentSource`  |
| `disturbance/`     | `sim_dynamics_disturbance`     | `DrydenTurbulence` -- 3-axis MIL-HDBK-1797 gust model                                                                                      |
| `vehicle/`         | `sim_dynamics_vehicle`         | `vehicle::step` -- the assembled vehicle: 6-DOF step consuming the mass + force/moment aggregates (`VehicleStep.hpp`)                      |

All libraries are header-only (interface) targets. Headers live in each
subdomain's `inc/` under the `sim::dynamics::<subdomain>` namespace. Each
subdomain owns its own `utst/` and `ptst/`; the domain root holds only this
README and the subdirectory list.

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
the quaternion rate, and Euler's equation with a full symmetric
`InertiaTensor`. The tensor stores `{Ixx, Iyy, Izz, Ixz, Ixy, Iyz}` (Ixz
fourth, so legacy xz-symmetric `{Ixx, Iyy, Izz, Ixz}` inits still mean
Ixz with Ixy = Iyz = 0); `solve` uses the symmetric 3x3 adjugate. The
attitude quaternion is renormalized once per step to recover from the
small additive drift the RK4 stages introduce.

```cpp
RigidBody6DOFState s;
s.velocity_body = Vec3{235.0, 0.0, 0.0};
InertiaTensor I{Ixx, Iyy, Izz, Ixz}; // or full: {Ixx,Iyy,Izz,Ixz,Ixy,Iyz}
auto force  = [](double, const RigidBody6DOFState&) { return Vec3{thrust, 0, 0}; };
auto moment = [](double, const RigidBody6DOFState&) { return Vec3{0, 0, 0}; };
stepRigidBody6DOF(s, force, moment, mass_kg, I, t, dt);  // 50 Hz tick
```

### Mass Properties

**Headers:** `mass_properties/inc/MassProperties.hpp`,
`mass_properties/inc/FuelBurnMassProperties.hpp`
**Purpose:** Compositional whole-body mass / CG / inertia, with a fuel
tank as one time-varying contributor.

`MassAccumulator` stacks `MassContributor`s (each with its mass, its CG in
a common body frame, and its inertia about its _own_ CG) and/or
`MassPropsSource`s; `result()` combines them into the total mass, the
mass-weighted CG, and the inertia about that net CG via the parallel-axis
theorem (which is why the tensor carries the full Ixy / Iyz cross terms).

Add fixed parts with `add(const MassContributor&)` and live parts with
`add(const MassPropsSource&)`. A `MassPropsSource` reports its `current()`
contribution; `StaticMassSource` wraps a fixed part (dry structure,
ballast), and `DynamicMassSource` is the base for time- or state-varying
parts. `result()` samples each source's `current()` afresh on every call,
so a draining tank or shifting payload is reflected between ticks. Sources
are referenced non-owningly.

`FuelTankMassSource` is a `DynamicMassSource`: it holds the tank
parameters plus the current fuel state; `step(thrust_N, dt)` drains fuel
at `mdot = TSFC * thrust` (clamped at zero, burn stops at exhaustion), and
`current()` reports the fuel as a `MassContributor` (mass = current fuel,
CG = tank location, inertia scaled by fuel fraction). The tank never
pretends to be the whole vehicle. Struct defaults are illustrative,
notional values; set them per the vehicle being simulated.

```cpp
StaticMassSource dry;  dry.c = {/* mass, cg, own inertia */};
FuelTankMassSource tank;  tank.params = {/* TSFC, capacity, cg, I_full */};
tank.step(thrust, dt);                  // advance the burn
MassAccumulator mass;
mass.add(dry);
mass.add(tank);
auto vehicle = mass.result();           // whole-body mass props
// vehicle.mass_kg, vehicle.cg_m, vehicle.inertia_about_cg
```

### Force / Moment

**Header:** `force_moment/inc/ForceMoment.hpp`
**Purpose:** Compositional force/moment aggregation -- the symmetric
pattern to mass properties.

`ForceMomentAccumulator` stacks `AppliedForce`s (each a force at a point,
plus an optional pure couple) and/or `ForceMomentSource`s; `resultAbout(about)`
combines them into a net `ForceMoment` (force + moment about a reference
point):

```
force  = sum( part.force )
moment = sum( part.moment + (part.point_m - about) x part.force )
```

A force applied off the reference point induces a moment `r x F`; pure
couples add directly and are point-independent. As with mass properties, a
source layer sits on top: `ForceMomentSource::current()` reports the applied
load now, `StaticForceMomentSource` wraps a fixed load, `DynamicForceMomentSource`
is the base for varying loads (a throttled engine, an aero force).
`resultAbout()` samples each source afresh on every call. Take the moment
about the same CG used for the inertia tensor to feed Euler's equations
consistently.

```cpp
StaticForceMomentSource engine; engine.f = {Vec3{thrust,0,0}, cg, Vec3{}};
ForceMomentAccumulator loads;
loads.add(engine);
loads.add(aero);
auto fm = loads.resultAbout(mp.cg_m);
// fm.force, fm.moment about mp.cg_m
```

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

### Vehicle Step

**Header:** `vehicle/inc/VehicleStep.hpp` (library `sim_dynamics_vehicle`)
**Purpose:** Step 6-DOF straight from the compositional aggregates.

`rigid_body` deliberately depends on neither `mass_properties` nor
`force_moment`. The `vehicle` module sits above all three (the assembled
vehicle) and adds `vehicle::step`, taking the aggregated mass properties and
net force/moment, forwarding to the callback-based step with `force = fm.force`,
`moment = fm.moment`, `mass = mp.mass_kg`, `I = mp.inertia_about_cg`.

```cpp
mass_properties::MassAccumulator mass;
mass.add(dry);
mass.add(tank);
const auto mp = mass.result();

force_moment::ForceMomentAccumulator loads;
loads.add(engine);
loads.add(aero);
const auto fm = loads.resultAbout(mp.cg_m);

vehicle::step(state, mp, fm, t, dt);  // one RK4 tick
```

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
- `FuelTankMassSource::step` -- ~21 ns/step (~47 M steps/s)
- `stepDryden` -- ~387 ns/step (~3 M steps/s); dominated by the three Gaussian
  RNG draws per step, not the filter arithmetic

The compositional accumulators are also RT-safe and **allocate nothing per
call** -- wire the vehicle once, then re-sample each tick. Costs scale with the
number of contributors/loads (figures for a five-part vehicle):

- `MassAccumulator::result` -- ~0.11 us (~9 M stacks/s); single-pass
  parallel-axis combine, no temporary container
- `ForceMomentAccumulator::resultAbout` -- ~0.09 us (~11 M stacks/s)
- full composed tick (re-stack mass + forces, then one 6-DOF step) -- ~0.49 us
  (~2 M ticks/s)

At a 100 Hz tick a full 6-DOF + fuel + turbulence vehicle update is well under
a microsecond, a negligible fraction of the frame budget.

### Configuration Notes

- `DrydenRng` is per-instance; give each vehicle its own seeded generator for
  independent, reproducible gust streams.
- Mass and the inertia tensor are held constant across a single integrator
  step; recompute them between ticks via `FuelTankMassSource::step` +
  `MassAccumulator::result` if modeling fuel burn.

## See Also

- `environment/README.md` -- sibling simulation subsystem
- MIL-HDBK-1797 -- Dryden turbulence specification (public)
