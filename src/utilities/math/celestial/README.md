# Celestial: Canonical Body Constants

**Namespace:** `apex::math::celestial`
**Platform:** Hosted + bare-metal (BAREMETAL interface library)
**C++ Standard:** C++17 (see lib.manifest for the platform contract)

One value per physical quantity: Earth's WGS-84 ellipsoid geometry and
rotation rate, and the Moon's IAU geometry and tidally-locked rotation. The tree's per-library constant copies (gravity,
terrain, atmosphere, sensors, factory) migrate onto this leaf so a value can
never drift between siblings again.

## Scope rule: geometry, not fields

This library carries what a FRAME needs -- shape and rotation. Gravity-FIELD
parameters (GM, J2 / spherical-harmonic tables, normal-gravity coefficients,
geoid models, model normalization radii like GRAIL's 1738000 m) stay with
the gravity models that define them. That split is the resolution of the
tree's two-lunar-radii conflict: 1737400 m is the Moon's mean radius
(geometry, here); 1738000 m is a gravity-model expansion parameter (not
here, and not a radius of the Moon).

## Module reference

| Header               | Provides                                                                                  |
| -------------------- | ----------------------------------------------------------------------------------------- |
| `Angles.hpp`         | `PI`, `TWO_PI`, `HALF_PI`, `DEG_TO_RAD`, `RAD_TO_DEG`, `degToRad()`, `radToDeg()`         |
| `EarthConstants.hpp` | WGS-84 `A`, `B`, `F`, `E2`, `EP2`, rotation `OMEGA`, standard gravity `G0` (BIPM defined) |
| `MoonConstants.hpp`  | IAU `R_MEAN`, sidereal period `T_SIDEREAL`, rotation `OMEGA` (tidally locked)             |

Sources: WGS-84 defining/derived parameters per NIMA TR8350.2; lunar values
per the IAU 2015 recommendations. Derived values are spelled as literals so
the headers read as fact tables; the unit tests enforce the derivations
(E2 = F(2-F), B = A(1-F), OMEGA = 2\*pi/T) and pin equality with the sim-side
copies this leaf is canon for, so the adoption migration is value-identical
by construction.
