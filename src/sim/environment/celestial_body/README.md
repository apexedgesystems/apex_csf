# Celestial Body Component

`CelestialBody` is an apex `SwModelBase`-derived component that represents one
celestial body in a simulation -- Earth, Moon, Sun, an asteroid, or a
procedural fictional planet. It owns a bundle of environment models (gravity +
terrain + atmosphere) assembled by `sim_environment_factory`, exposes them to
other components via the apex data registry, and publishes a one-shot OUTPUT
telemetry snapshot of the body's identity and key physical summary.

It is a **passive** component: it registers no scheduled tasks. Active
components (for example a future propagator) query a `CelestialBody` on demand
for gravity acceleration, terrain elevation, and atmosphere density at the
spacecraft position. Queries through the underlying models are RT-safe after
init; init itself is not RT-safe (it may perform file I/O).

## Public surface

| Member                      | Purpose                                                                        |
| --------------------------- | ------------------------------------------------------------------------------ |
| `tunables()`                | Mutable handle on `CelestialBodyTunables`; set body/fidelity/paths before init |
| `init()` (framework)        | Runs `doInit()`: validate -> build env -> load -> register data -> telemetry   |
| `gravity()`                 | Polymorphic `GravityModelBase*` (valid after a successful init)                |
| `terrain()`                 | Polymorphic `TerrainModelBase*` (valid after a successful init)                |
| `atmosphere()`              | Polymorphic `AtmosphereModelBase*` (valid after a successful init)             |
| `telemetry()`               | `CelestialBodyTelemetry` OUTPUT snapshot                                       |
| `bodyState()`               | `CelestialBodyState` lifecycle bookkeeping                                     |
| `isReady()`                 | True after a successful init                                                   |
| `componentId()` / `label()` | Component identity (`220` / `CELESTIAL_BODY`)                                  |

## Configuration

`CelestialBodyTunables` is a trivially-copyable struct (no `std::string`; file
paths are fixed-size char buffers) so it can be carried by `TunableParam<T>`
and packed into a `.tprm` file. It selects the body and a per-subsystem
fidelity, plus a data path for each file-backed fidelity:

| Subsystem  | Fidelities                                | Needs a data path |
| ---------- | ----------------------------------------- | ----------------- |
| Gravity    | CONSTANT, J2, SPHERICAL                   | SPHERICAL         |
| Terrain    | CONSTANT, SPHERE, ELLIPSOID, HTILE        | HTILE             |
| Atmosphere | CONSTANT, EXPONENTIAL, LAYERED, EMPIRICAL | LAYERED           |

Analytic fidelities ignore their data-path field. A file-backed fidelity with
an empty path is rejected at init (validation, before any model is built).

## Lifecycle and data registry

1. The caller constructs the component and pre-populates tunables via
   `tunables().set(struct)` (the executive may instead drive the optional
   `.tprm` load hook between log init and `init()`).
2. The executive registers the component, which calls `init()` -> `doInit()`.
3. `doInit()` validates the tunables, builds the env bundle with
   `makeEnvironment(spec)`, loads any file-backed models, registers the
   tunables / state / telemetry blocks with the data registry, populates the
   telemetry snapshot, and returns `SUCCESS`.
4. After init, other components query `gravity()` / `terrain()` /
   `atmosphere()` (RT-safe).

`doInit()` registers three data blocks under the component's full UID:
`TUNABLE_PARAM` (`tunables`), `STATE` (`state`), and `OUTPUT` (`telemetry`).

## Wrapping the environment models

The component holds each model behind its base-class pointer and checks every
return value from the models it drives:

- **Gravity** returns `bool` from `acceleration()`. Surface-gravity telemetry
  is computed only when the body has a canonical reference radius (Earth, Moon)
  and the `acceleration()` call succeeds; a procedural (OTHER) body has no
  canonical radius, so surface gravity is reported as 0.
- **Terrain** and **atmosphere** return strongly-typed `Status` enums
  (`env::terrain::Status`, `env::atmosphere::Status`). Their `load()` calls are
  treated as fatal init errors on any non-`SUCCESS` code (logged with the
  specific status). The surface-atmosphere telemetry sample is taken only when
  `query()` returns `SUCCESS`; a vacuum body is short-circuited via
  `isVacuum()` before any query, and a non-success result leaves the snapshot
  at 0.

A failed init leaves `init_status == 2`, `isReady() == false`, and the
polymorphic accessors return `nullptr` for a never-built environment.

## Telemetry (OUTPUT)

`CelestialBodyTelemetry` is a trivially-copyable, one-shot snapshot taken at
init: mirrored body / fidelity discriminators, an `is_vacuum_atmosphere` flag,
the gravity reference radius and max degree, surface gravity magnitude, and
surface atmosphere density / temperature. It lets subscribers read the body's
identity and physical summary without `dynamic_cast`-ing through the
polymorphic accessors. It is not updated per tick (the component is passive); a
re-init is required to refresh it.

## Quick start

```cpp
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"

using namespace sim::environment::celestial_body;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::AtmosphereFidelity;

CelestialBody earth;
CelestialBodyTunables t{};
t.body = Body::EARTH;
t.gravity_fidelity = GravityFidelity::J2;
t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
earth.tunables().set(t);

if (earth.init() == 0u) {       // 0 == SUCCESS
  double r[3] = {7.0e6, 0.0, 0.0};
  double a[3] = {0.0, 0.0, 0.0};
  earth.gravity()->acceleration(r, a);          // bool API
  double rho = 0.0;
  earth.atmosphere()->density(0.0, 0.0, 0.0, rho); // Status API
}
```

## Real-time considerations

- `init()` is not RT-safe: it builds models, performs file I/O for file-backed
  fidelities, and registers data blocks. Run it at scenario setup, never on the
  step.
- After init, the polymorphic queries route directly into the underlying
  models, whose RT-safety is documented by each model library (analytic
  fidelities are O(1); HTILE elevation is O(1); LAYERED query is O(log N)).
- The component registers no tasks and adds no per-tick work of its own; it is
  a thin, query-on-demand wrapper over the environment models.

## Testing

`TestSimCelestialBody` covers default (not-ready) construction, component
identity, analytic Earth/Moon/OTHER init, telemetry (including the vacuum and
no-canonical-radius cases), CONSTANT-gravity max-degree, the validation
rejections for empty file-backed paths (gravity / terrain / atmosphere), the
file-backed load-failure propagation, init idempotency, and the gravity /
terrain / atmosphere query routing through the live models.

## See also

- `src/sim/environment/factory/` -- builds the (gravity, terrain, atmosphere)
  bundle this component wraps.
- `src/sim/environment/{gravity,terrain,atmosphere}/` -- the model libraries
  and their per-model READMEs / Status APIs.
- `src/system/core/infrastructure/system_component/` -- the apex component
  framework (`SwModelBase`, the init lifecycle, the data registry).
