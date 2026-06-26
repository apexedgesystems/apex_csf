# Aerodynamics Module

**Namespace:** `sim::aerodynamics`
**Platform:** Linux-only
**C++ Standard:** C++23

Body-frame aerodynamic force / moment models for atmospheric vehicles
(aircraft, the atmospheric phase of a rocket ascent, re-entry). Each model
turns a flight condition into the aerodynamic load on the body; it pairs with
`environment/atmosphere/` (which supplies density at altitude) and feeds the
net force / moment into `dynamics` (the rigid-body EOM integrate it).

The models are **parameterized and vehicle-agnostic** -- the core ships the
equations; a specific aircraft's coefficients and reference geometry are
supplied by the application.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Reference](#quick-reference)
3. [Design Principles](#design-principles)
4. [Module Reference](#module-reference)
   - [PolarAero](#polaraero) - parabolic drag polar
   - [StabilityDerivativeAero](#stabilityderivativeaero) - linearized derivatives
5. [Real-Time Considerations](#real-time-considerations)
6. [See Also](#see-also)

## Overview

| Question                                                                | Function                      |
| ----------------------------------------------------------------------- | ----------------------------- |
| What lift and drag does a wing make at this alpha and speed?            | `evaluatePolar`               |
| What is the finite-wing lift-curve slope?                               | `finiteWingLiftSlope`         |
| What alpha trims level flight (lift = weight)?                          | `trimAlphaForLevelFlight`     |
| What are the full body-axis forces and moments (with rates + controls)? | `evaluateStabilityDerivative` |
| How do wind-axis lift/drag/side rotate to body axes?                    | `windToBodyForces`            |

## Quick Reference

| Model                     | Library            | Provides                                                                                                                                     |
| ------------------------- | ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `PolarAero`               | `sim_aerodynamics` | parabolic drag polar `CD = CD0 + CL^2/(pi e AR)` + linear lift; lift/drag at a flight condition, finite-wing lift slope, level-flight trim   |
| `StabilityDerivativeAero` | `sim_aerodynamics` | linearized small-perturbation model (~30 derivatives) producing body-frame force + moment from velocity, body rates, and control deflections |

Header-only (interface) library under the `sim::aerodynamics` namespace. The
result structs carry body-frame `force_body` / `moment_body` vectors that feed
the 6-DOF net load.

## Design Principles

| Principle                            | Rationale                                                                                                 |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------- |
| Parameterized, not aircraft-specific | The core ships the equations; coefficients are an input. No airframe data is baked in.                    |
| Body-frame output                    | Models own the wind->body rotation, so every aerodynamic load is reported in body axes ready for the EOM. |
| Fidelity ladder                      | A cheap parabolic polar and a richer derivative model share the domain; a caller picks the rung it needs. |
| Allocation-free, RT-safe             | Each evaluation is bounded arithmetic with no heap use.                                                   |

## Module Reference

### PolarAero

**Header:** `inc/PolarAero.hpp`
**Purpose:** Cheap parabolic-polar lift and drag.

`evaluatePolar` computes `CL = CL0 + CL_a*alpha`, the parabolic drag polar
`CD = CD0 + CL^2/(pi*e*AR)`, and the lift/drag forces `L = q*S*CL`,
`D = q*S*CD` with `q = 0.5*rho*V^2`. `finiteWingLiftSlope` reduces a section
lift slope to the finite-wing value; `trimAlphaForLevelFlight` solves the alpha
that makes lift equal weight (returning NaN when no air is present).

```cpp
PolarAeroParams p;                  // illustrative jet-transport defaults
auto r = evaluatePolar(p, /*alpha*/ 0.04, /*rho*/ 0.41, /*V*/ 240.0);
// r.L_N, r.D_N, r.CL, r.CD, r.q_Pa
```

### StabilityDerivativeAero

**Header:** `inc/StabilityDerivativeAero.hpp`
**Purpose:** Linearized stability-derivative forces and moments.

`evaluateStabilityDerivative` builds each aerodynamic coefficient as a linear
sum over angle of attack, sideslip, the nondimensional body rates
(`p*b/2V`, `q*c/2V`, `r*b/2V`), and the control deflections, then forms the
wind-axis forces and body-axis moments and rotates the forces to body axes via
`windToBodyForces`. The result carries body-frame `force_body` / `moment_body`
ready for the 6-DOF net load. Below 1 m/s it returns zero (the linearization
and nondimensional rates are undefined at rest).

```cpp
StabilityDerivativeAeroParams p;    // illustrative jet-transport defaults
ControlInputs delta;                // elevator / aileron / rudder (rad)
auto r = evaluateStabilityDerivative(p, v_body, w_body, delta, /*rho*/ 0.41);
// r.force_body, r.moment_body  -> the body-frame aerodynamic wrench
```

## Real-Time Considerations

Both models are RT-safe: bounded arithmetic, no allocation, no I/O. `PolarAero`
is a handful of multiply-adds; `StabilityDerivativeAero` is ~30 derivative
terms plus a 3x3 rotation -- both well under a microsecond, evaluated once per
tick alongside the integration step.

## See Also

- `environment/atmosphere/` -- supplies density (and pressure / temperature) at altitude
- `dynamics/rigid_body/` + `dynamics/wrench/` -- consume the body-frame force and moment
