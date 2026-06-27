# sim_gnc

Guidance, navigation, and control laws for atmospheric vehicles. A generic PID
primitive plus the named autopilot loops it composes. Header-only, parameterized,
vehicle-agnostic, and MCU-safe -- a specific aircraft's gains come from the
application.

## Contents

1. [Design](#design)
2. [Module reference](#module-reference)
   - [PIDLoop](#pidloop) - the shared primitive
   - [Longitudinal loops](#longitudinal-loops) - pitch / altitude / speed
   - [Lateral loops](#lateral-loops) - roll / heading / yaw damper
   - [GustAlleviation](#gustalleviation) - vertical-gust feedforward
3. [MCU compatibility](#mcu-compatibility)
4. [Integration](#integration)

## Design

`PIDLoop` is the shared building block: a single-input single-output PID with
backward-Euler integration, first-difference derivative, output clamping, and
conditional-integration anti-windup. The named loops compose it (and add their
own clamps and sign conventions) rather than inheriting -- composition over a
forced base, since each loop has its own physical inputs.

Loops cascade: an outer loop generates a reference an inner loop tracks
(`AltitudeHold -> PitchAttitudeHold -> elevator`; `HeadingHold -> RollController
-> aileron`). Sign conventions match the `StabilityDerivativeAero` model so the
control outputs feed the aero `ControlInputs` directly. Default gains are
illustrative transport-class starting values; re-tune per-aircraft via the gain
setters.

## Module reference

### PIDLoop

**Header:** `inc/PIDLoop.hpp`. `step(error, dt) -> u`. Gains `{Kp, Ki, Kd}` and
optional `{u_min, u_max}` clamps; on saturation the integral update is reverted
to prevent windup. `reset()` clears the integral + derivative history.

### Longitudinal loops

**Header:** `inc/LongitudinalControllers.hpp`.

- `PitchAttitudeHold` -- `step(pitch_ref, pitch_actual, dt) -> elevator` (inner).
- `AltitudeHold` -- `step(alt_ref, alt_actual, dt) -> pitch_ref` (outer).
- `SpeedHold` -- `step(V_ref, V_actual, dt) -> throttle` in [0, 1] (auto-throttle).

### Lateral loops

**Header:** `inc/LateralControllers.hpp`.

- `RollController` -- `step(bank_ref, bank_actual, dt) -> aileron` (inner).
- `HeadingHold` -- `step(heading_ref, heading_actual, dt) -> bank_ref` (outer),
  shortest-path wrapped to avoid the +/-pi discontinuity.
- `YawDamper` -- `step(yaw_rate, dt) -> rudder` with a washout high-pass that
  passes the Dutch-roll band but rejects a steady turn rate (`tau = 0` disables it).

### GustAlleviation

**Header:** `inc/GustAlleviation.hpp`. `step(w_g, V) -> elevator`. Open-loop
feedforward: a vertical gust induces `d_alpha = w_g/V`, cancelled by
`elevator = -(CL_alpha/CL_delta_e) * d_alpha`. An authority fraction scales or
disables it.

## MCU compatibility

The control laws are intended to run unchanged on a microcontroller (the
long-term goal is closed-loop flight control on an MCU target). The library is
therefore kept portable to the most conservative MCU toolchain in the tree:

- **C++17-clean** -- no C++20/23 features, so it compiles on the AVR (C++17) and
  C2000 (C++17) toolchains as well as STM32 (C++20) and ESP32 (C++23).
- **No heap, no exceptions, no RTTI, no virtuals** -- fixed-size members,
  arithmetic only; compiles under `-fno-exceptions -fno-rtti`.
- **Header-only with zero STL / system includes** -- nothing to link, no
  `<random>`/`<vector>`/`<string>` dependency.

Scalars are `double`; on a single-precision FPU (e.g. Cortex-M4F) double is
software-emulated, so a `float` or templated-scalar path is a possible future
optimization -- a performance choice, not a compatibility one.

## Integration

The laws are pure: a scheduled control component reads the estimated state (from
the navigation filter over sensor measurements), calls the loops, and publishes
the `ControlInputs` (surface deflections) + throttle the aero/propulsion models
consume -- closing the simulation loop.
