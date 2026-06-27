# Propulsion Module

**Namespace:** `sim::propulsion`
**Platform:** Linux-only
**C++ Standard:** C++23

Engine thrust models for atmospheric vehicles. Each model turns a throttle
command and the ambient density into thrust (and, for the turbofan, spool state
and rotor angular momentum). The thrust feeds the net force on the body, and
the same thrust drives fuel burn through `mass_properties::FuelTankMassSource`.

The models are **parameterized and vehicle-agnostic** -- the core ships the
equations; a specific engine's parameters come from the application.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [DensityScaledThrust](#densityscaledthrust) - empirical baseline
   - [Turbofan2Spool](#turbofan2spool) - two-spool dynamics
   - [PropulsionModel ladder + PropulsionSystem](#propulsionmodel-ladder--propulsionsystem)
5. [Real-Time Considerations](#real-time-considerations)
6. [See Also](#see-also)

## Overview

| Question                                                         | Function             |
| ---------------------------------------------------------------- | -------------------- |
| What thrust at this throttle and altitude (cheap)?               | `evaluateThrust`     |
| What thrust + spool state + rotor momentum this tick (turbofan)? | `stepTurbofan2Spool` |

## Quick Reference

| Model                 | Library          | Provides                                                                                                                                                   |
| --------------------- | ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DensityScaledThrust` | `sim_propulsion` | `T = T_max * throttle * (rho/rho0)^n`; throttle clamp; zero in vacuum                                                                                      |
| `Turbofan2Spool`      | `sim_propulsion` | N1/N2 first-order spool dynamics, throttle lag, `T ~ (N1/N1_max)^2 * (rho/rho0)^n`, idle floors, per-engine rotor angular momentum for the gyroscopic term |

Header-only (interface) library under the `sim::propulsion` namespace.

## Design Principles

| Principle                          | Rationale                                                                                                    |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Parameterized, not engine-specific | The core ships the model; T_max, spool time constants, and inertia are inputs. No engine's data is baked in. |
| Stateless vs stateful              | `DensityScaledThrust` is pure; `Turbofan2Spool` carries spool state advanced once per tick.                  |
| Unconditionally stable             | The spool dynamics use a backward-Euler (semi-implicit) update, stable at any `dt`.                          |
| Allocation-free, RT-safe           | Each evaluation/step is bounded arithmetic with no heap use.                                                 |

## Module Reference

### DensityScaledThrust

**Header:** `inc/DensityScaledThrust.hpp`
**Purpose:** Cheap empirical thrust.

`evaluateThrust` returns `T_max_sl * throttle * (rho/rho_ref)^n_density`, with
throttle clamped to `[throttle_min, throttle_max]` and zero thrust when
`rho <= 0`. The density exponent spans turbojets (`n ~ 1`), high-bypass
turbofans (`n ~ 0.7`), and rockets (`n ~ 0`).

### Turbofan2Spool

**Header:** `inc/Turbofan2Spool.hpp`
**Purpose:** Two-spool turbofan with spool dynamics.

`stepTurbofan2Spool` advances the N2 (HP) spool toward the throttle target and
the N1 (LP) spool toward N2, each as a first-order lag (backward Euler), with
idle floors. Thrust scales as `(N1/100)^2 * (rho/rho0)^n` -- the `N1^2`
nonlinearity matters for speed-hold stability. The result also reports the
per-engine rotor angular momentum `H_rotor` for the gyroscopic moment the EOM
add as `H_rotor x omega_body`.

```cpp
Turbofan2SpoolParams p;              // illustrative high-bypass defaults
Turbofan2SpoolState s;               // starts at idle
const auto r = stepTurbofan2Spool(s, p, /*throttle*/ 0.9, /*rho*/ 0.41, /*dt*/ 0.02);
// r.thrust_N (per engine), r.N1_pct, r.N2_pct, r.H_rotor_kgm2_s
```

### PropulsionModel ladder + PropulsionSystem

**Header:** `inc/PropulsionModel.hpp`
**Purpose:** Wrap the engines behind one interface and link propulsion's two
contributions.

`PropulsionModel::step` advances an engine one tick and reports per-engine
thrust (+ rotor angular momentum). The rungs are `ConstantThrust`,
`DensityScaledThrustModel`, and `Turbofan2SpoolModel`; a caller adds a rung by
implementing `PropulsionModel`.

Propulsion spans **both** dynamics stacks, so `PropulsionSystem` is the link:

- it **is** a `wrench::WrenchSource` (tagged `Propulsion`) -- the thrust force
  at the mount plus the gyroscopic moment `H_rotor x omega_body`;
- its `step()` computes thrust once and **drives the fuel tank's burn** from
  that same thrust, so one quantity feeds the wrench and the mass side.

The mass models stay in `mass_properties` (the depleting `FuelTankMassSource`,
and a `StaticMassSource` for the engine/tank structure). The caller adds the
system to the `WrenchAccumulator` and the fuel tank to the `MassAccumulator`.

```cpp
Turbofan2SpoolModel engine;
FuelTankMassSource tank;             // the depleting fuel (mass side)
PropulsionSystem prop;
prop.model = &engine; prop.fuel = &tank;
prop.omega_body = &state.angular_velocity_body;   // for the gyroscopic moment
prop.engine_count = 4; prop.mount_m = {-30.0, 0.0, 2.0};

prop.step(throttle, rho, dt);        // thrust -> wrench cache + fuel burn
wrenchAcc.add(prop);                 // force side
massAcc.add(tank);                   // mass side (same tank)
```

## Real-Time Considerations

Both models are RT-safe: bounded arithmetic, no allocation, no I/O. The
turbofan step is a few multiply-adds plus one `pow`; evaluated once per tick.

## See Also

- `environment/atmosphere/` -- supplies the ambient density
- `dynamics/wrench/` -- consumes thrust as a body-frame force (the contributor)
- `dynamics/mass_properties/` -- `FuelTankMassSource` burns fuel from thrust
