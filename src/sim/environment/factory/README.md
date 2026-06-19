# Environment Factory Library

**Namespace:** `sim::environment`
**Platform:** Linux
**C++ Standard:** C++23
**Library:** `sim_environment_factory`

Cross-subsystem factory that builds gravity, terrain, and atmosphere
models from a `(Body, fidelity)` pair and returns them polymorphically
through their respective base interfaces. It is the single assembly
point that turns a high-level environment description into a set of
ready-to-query model objects, so a caller can swap fidelity levels (or
swap a whole subsystem in and out) without recompiling consumer code.

The factory depends on `sim_environment_gravity`,
`sim_environment_terrain`, and `sim_environment_atmosphere`; it does not
add new physics, only dispatch and per-body default selection.

---

## Public surface

| Header                        | Purpose                                                                                                                    |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `inc/Body.hpp`                | `Body` enum (EARTH, MOON, OTHER) + `toString`                                                                              |
| `inc/EnvironmentFidelity.hpp` | `GravityFidelity` / `TerrainFidelity` / `AtmosphereFidelity` ladders + `toString`                                          |
| `inc/EnvironmentFactory.hpp`  | `makeGravityModel` / `makeTerrainModel` / `makeAtmosphereModel`, `EnvironmentSpec`, `EnvironmentModels`, `makeEnvironment` |

Each `make*Model` returns a `std::unique_ptr` to the subsystem's base
class -- ownership transfers to the caller, and the model is destroyed
when the pointer goes out of scope. There is no raw `new`/`delete` and
no shared global state.

---

## Fidelity ladders

Each subsystem has its own monotonic ladder (higher = more accurate /
more expensive). The ladders are independent: a spec can mix a coarse
gravity model with a high-fidelity atmosphere.

| Subsystem  | Ladder (low -> high)                                    |
| ---------- | ------------------------------------------------------- |
| Gravity    | `CONSTANT` -> `J2` -> `SPHERICAL`                       |
| Terrain    | `CONSTANT` -> `SPHERE` -> `ELLIPSOID` -> `HTILE`        |
| Atmosphere | `CONSTANT` -> `EXPONENTIAL` -> `LAYERED` -> `EMPIRICAL` |

---

## Initialization contract

The factory returns two classes of model:

- **Ready-to-use (analytic + body-default):** `CONSTANT` / `SPHERE` /
  `ELLIPSOID`, gravity `J2` for a known body, atmosphere
  `EXPONENTIAL`, and the Earth/Moon wrappers (`SrtmTerrainModel`,
  `LolaTerrainModel`, `Ussa76AtmosphereModel`, `VacuumAtmosphereModel`).
  The factory has already populated the body constants; the model can be
  queried immediately.
- **Returned uninitialized (file/coeff-backed):** gravity `SPHERICAL`
  (needs a `CoeffSource`), terrain `HTILE` for `Body::OTHER` (generic
  `HtileTile`, needs `load(path)`), and atmosphere `LAYERED` for
  `Body::OTHER` (generic `LayeredAtmosphere`, needs `load(path)`). The
  caller must complete setup before querying.

For `Body::OTHER` the factory has no body defaults to apply, so the J2
gravity model is also returned uninitialized and the caller supplies the
parameters.

### Body-specific dispatch

| Fidelity                 | EARTH                     | MOON                    | OTHER                                |
| ------------------------ | ------------------------- | ----------------------- | ------------------------------------ |
| Gravity `CONSTANT`       | Earth surface g0          | Moon surface g0         | default g0 (Earth value)             |
| Gravity `J2`             | WGS84/EGM2008 defaults    | GRGM1200A defaults      | uninitialized                        |
| Gravity `SPHERICAL`      | shell (needs CoeffSource) | shell                   | shell                                |
| Terrain `SPHERE`         | WGS84 equatorial radius   | lunar reference radius  | unit radius                          |
| Terrain `ELLIPSOID`      | WGS84 oblate spheroid     | sphere (rEq == rPol)    | unit radii                           |
| Terrain `HTILE`          | `SrtmTerrainModel`        | `LolaTerrainModel`      | generic `HtileTile` (uninit)         |
| Atmosphere `CONSTANT`    | sea-level ISA             | vacuum                  | vacuum                               |
| Atmosphere `EXPONENTIAL` | Earth troposphere         | `VacuumAtmosphereModel` | Earth-tropo defaults                 |
| Atmosphere `LAYERED`     | `Ussa76AtmosphereModel`   | `VacuumAtmosphereModel` | generic `LayeredAtmosphere` (uninit) |
| Atmosphere `EMPIRICAL`   | nullptr (reserved)        | nullptr                 | nullptr                              |

---

## Error handling

A `make*Model` call returns `nullptr` only when the requested fidelity
cannot be satisfied: the reserved atmosphere `EMPIRICAL` slot (not yet
implemented) and any enum value outside the defined ladder (e.g. a
corrupted or forward-compatible config). Every defined `(body,
fidelity)` pair returns a non-null model. `nullptr` is the factory's
single, unambiguous failure signal; because the return type is a
`unique_ptr`, a failed call allocates nothing and leaks nothing, and the
caller's existing pointers are untouched.

The factory does not introduce its own `Status` enum: a `unique_ptr`
whose `nullptr` value means "unsupported request" is the idiomatic,
allocation-free factory result, and it already satisfies the
"leave outputs untouched on failure" contract that the subsystem query
APIs express through their `Status` enums. The models the factory
returns keep their own `Status`-returning query APIs unchanged.

---

## Quick start

### One subsystem at a time

```cpp
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"

using namespace sim::environment;

auto gravity = makeGravityModel(Body::EARTH, GravityFidelity::J2);
double r[3] = {7.0e6, 0.0, 0.0};
double a[3] = {0.0, 0.0, 0.0};
gravity->acceleration(r, a);   // ready to use: Earth GM/a/J2 baked in
```

### A whole environment in one call

```cpp
#include "src/sim/environment/factory/inc/EnvironmentFactory.hpp"

using namespace sim::environment;

EnvironmentSpec spec;
spec.body       = Body::EARTH;
spec.gravity    = GravityFidelity::J2;
spec.terrain    = TerrainFidelity::ELLIPSOID;
spec.atmosphere = AtmosphereFidelity::LAYERED;

EnvironmentModels env = makeEnvironment(spec);
// env.gravity / env.terrain / env.atmosphere are unique_ptrs to the
// subsystem base classes. A default-constructed EnvironmentSpec yields
// Body::OTHER with every subsystem at CONSTANT -- a zero-cost baseline.
```

`EnvironmentSpec` fields each default to `CONSTANT`, so a caller opts in
only to the subsystems it needs. Adding a future subsystem (magnetic,
solar, ...) adds one opt-in field here without changing existing callers.

---

## Real-time considerations

The factory runs at scenario-initialization time, not on the real-time
step. A `make*Model` call is a single enum `switch` plus one
`std::make_unique` (one heap allocation and a constructor that copies a
few body-constant doubles) -- bounded, no loop, no I/O. The expensive
per-model setup (`load()` / `init(coeffSource)`) is deliberately left to
the caller and runs off the real-time path.

Because the call is one-shot and trivial, a throughput benchmark is not
meaningful for this library; see
[`docs/optimization/2026-06-17/factory/baseline_analysis.md`](../../../../docs/optimization/2026-06-17/factory/baseline_analysis.md)
for the rationale. The returned models carry their own real-time
guarantees, documented in the gravity / terrain / atmosphere READMEs.

---

## Testing

| Suite                               | What it covers                                                                                                                                                                                                                                                                                                 |
| ----------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `utst/EnvironmentFactory_uTest.cpp` | Every `(body, fidelity)` dispatch path; ready-vs-uninitialized contract; typed-wrapper identity (`dynamic_cast`); the reserved `EMPIRICAL` and out-of-range-fidelity `nullptr` paths; bundled `makeEnvironment` + `EnvironmentSpec` defaults; `toString` for every enum value and its `UNKNOWN_*` fall-through |

Run unit tests:

```bash
make compose-testp
```

---

## See also

- `sim::environment::gravity` / `sim::environment::terrain` /
  `sim::environment::atmosphere` -- the subsystems this factory
  dispatches into; each documents its own model ladder and RT-safety.
