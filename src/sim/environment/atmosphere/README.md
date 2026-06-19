# Atmosphere Library

**Namespace:** `sim::environment::atmosphere`
**Platform:** Linux
**C++ Standard:** C++23
**Library:** `sim_environment_atmosphere`

Local fluid-state queries (density, pressure, temperature, speed of
sound) at a position above a celestial body. Hierarchy mirrors
`sim::environment::gravity` and `sim::environment::terrain`: an
abstract base, a fidelity ladder of analytic models, a body-agnostic
file-backed model, and Earth/Moon convenience wrappers that bake in
body-specific defaults.

This library reads the **`.atm`** binary atmosphere parameter format
(see [`inc/Atm.hpp`](inc/Atm.hpp) for the wire spec). The format is a
documented byte layout -- any external tool can produce a conforming
`.atm` file. One such producer is the `horizon_world` CLI in the
horizon repo (`convert-ussa76` for the canonical Earth USSA76 table,
`gen-atmosphere` for procedural constant/exponential models, with
`gen-pyramid` / `gen-system` auto-emitting per-body `.atm` files when
the source spec carries an `[atmosphere]` section).

All query and load methods return a `Status` enum (`SUCCESS`, `WARN_*`,
`ERROR_*`), not a bool, so callers distinguish a valid sample from a
vacuum query, an out-of-range altitude, an uninitialized model, or a
specific parameter error. Output buffers are left unmodified on any
non-`SUCCESS` result.

---

## Model hierarchy

| Model                          | Backing           | RT-safe (after init) | Use case                                   |
| ------------------------------ | ----------------- | -------------------- | ------------------------------------------ |
| `ConstantAtmosphere`           | analytic          | yes                  | Vacuum sentinel; held-fixed conditions     |
| `ExponentialAtmosphere`        | analytic          | yes                  | rho(h) = rho0\*exp(-h/H), isothermal       |
| `LayeredAtmosphere`            | .atm file         | yes (after `load`)   | Body-agnostic hydrostatic N-layer model    |
| `earth::Ussa76AtmosphereModel` | hardcoded USSA76  | yes                  | Earth wrapper; sea level -> 86 km          |
| `moon::VacuumAtmosphereModel`  | analytic (vacuum) | yes                  | Moon wrapper; rho == 0, isVacuum() == true |

This mirrors gravity's ladder
(`Constant` -> `J2` -> `SphericalHarmonic` -> `Egm2008` / `Grail`) and
terrain's ladder (`Constant` -> `Sphere` -> `Ellipsoid` -> `HtileTile`
-> `SrtmTerrainModel` / `LolaTerrainModel`): analytic baselines first,
body-agnostic full model in the middle, body-specific named wrappers
on top. The `EnvironmentFactory` exploits this symmetry across all
three subdomains.

---

## Public surface

| Header                                  | Purpose                                                                                        |
| --------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `inc/AtmosphereModelBase.hpp`           | Abstract base; `query(alt, lat, lon, AtmosphereState&)` + convenience accessors + `isVacuum()` |
| `inc/AtmosphereStatus.hpp`              | `Status` enum + helpers (mirrors `GravityStatus` / `TerrainStatus`)                            |
| `inc/Atm.hpp`                           | `.atm` format header + R/W (imported from horizon)                                             |
| `inc/ConstantAtmosphere.hpp`            | Constant rho/T/P; defaults to vacuum                                                           |
| `inc/ExponentialAtmosphere.hpp`         | Isothermal exponential decay; Earth tropo defaults                                             |
| `inc/LayeredAtmosphere.hpp` + `src/...` | Hydrostatic N-layer (USSA76-class); loads from `.atm`                                          |
| `inc/earth/Ussa76Constants.hpp`         | `R_SPECIFIC`, `GAMMA`, `G0`, 7-layer USSA76 table                                              |
| `inc/earth/Ussa76AtmosphereModel.hpp`   | Earth wrapper subclass of `LayeredAtmosphere`                                                  |
| `inc/moon/VacuumAtmosphereModel.hpp`    | Moon wrapper subclass of `ConstantAtmosphere` (vacuum)                                         |

---

## Quick start

### Analytic baselines

```cpp
#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp"

sim::environment::atmosphere::ConstantAtmosphere vacuum; // default vacuum
// vacuum.isVacuum() == true; AtmosphereState query returns all zeros.

sim::environment::atmosphere::ConstantAtmosphere isa_sea_level(
    /*rho=*/1.225, /*T=*/288.15, /*P=*/101325.0);
sim::environment::atmosphere::AtmosphereState s;
isa_sea_level.query(/*alt=*/0.0, /*lat=*/0.0, /*lon=*/0.0, s);
// s.rho == 1.225, s.T == 288.15, s.P == 101325, s.a == 340.294 m/s
```

```cpp
#include "src/sim/environment/atmosphere/inc/ExponentialAtmosphere.hpp"

sim::environment::atmosphere::ExponentialAtmosphere exp; // Earth tropo defaults
double rho = 0.0;
exp.density(/*alt=*/8500.0, 0.0, 0.0, rho);  // rho ~= rho0 / e
```

### Layered (USSA76 / procedural)

```cpp
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"

sim::environment::atmosphere::LayeredAtmosphere atm;
atm.load("data/earth/usa76.atm"); // produced by `convert-ussa76` upstream
sim::environment::atmosphere::AtmosphereState s;
atm.query(/*alt=*/30000.0, 0.0, 0.0, s); // hydrostatic interpolation
```

### Earth / Moon wrappers (defaults)

```cpp
#include "src/sim/environment/atmosphere/inc/earth/Ussa76AtmosphereModel.hpp"

sim::environment::atmosphere::earth::Ussa76AtmosphereModel earth;
// Default-constructed -- table + thermo constants already populated.
sim::environment::atmosphere::AtmosphereState s;
earth.query(0.0, 0.0, 0.0, s); // T=288.15, P=101325, rho=1.225
```

```cpp
#include "src/sim/environment/atmosphere/inc/moon/VacuumAtmosphereModel.hpp"

sim::environment::atmosphere::moon::VacuumAtmosphereModel moon;
moon.isVacuum(); // true -- drag computations can short-circuit
```

The wrappers follow the same defaults-baked-in pattern as
`Egm2008Model` (gravity) and `SrtmTerrainModel` (terrain).

---

## .atm format

64-byte self-describing header followed by N x 32-byte parameter
records. Header carries body name, model_type discriminator, thermo
constants (R_specific, gamma, g0), record count, and a provenance
hash. Payload schema depends on `model_type`:

| Model       | n_records | Record schema                                             |
| ----------- | --------- | --------------------------------------------------------- |
| Constant    | 1         | `{ rho0_kg_m3, T0_K, P0_Pa, _ }`                          |
| Exponential | 1         | `{ rho0_kg_m3, T0_K, scale_height_m, _ }`                 |
| Layered     | N         | `{ base_alt_m, base_T_K, base_P_Pa, lapse_K_per_m }`      |
| Empirical   | 1         | `{ model_id_hash, _, _, _ }` (deferred; e.g. NRLMSISE-00) |

The format spec is documented inline at the top of `inc/Atm.hpp`.
This library implements its own reader/writer (`AtmReader`, `AtmWriter`,
`AtmHeader`, `AtmRecord`) under the `sim::environment::atmosphere`
namespace. Producers (such as horizon's `horizon_world` CLI) implement
the same wire spec independently. The contract between producer and
consumer is the byte layout, not shared code.

---

## Vacuum sentinel

`AtmosphereModelBase::isVacuum()` returns true for any model with
identically-zero density. Drag computation should test this first and
skip the integration entirely when true:

```cpp
if (!atm.isVacuum()) {
  double rho = 0.0;
  atm.density(altitude, lat, lon, rho);
  // ... compute drag ...
}
```

The default is false on the base class; only `ConstantAtmosphere`
returns true (when its rho field is exactly 0). The Moon wrapper
`VacuumAtmosphereModel` is a default-constructed `ConstantAtmosphere`,
so it inherits this behavior automatically.

---

## Testing

| Suite                                    | What it covers                                                                                                                                            |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `utst/AtmosphereStatus_uTest.cpp`        | `Status` toString + isSuccess/isWarning/isError for every code                                                                                            |
| `utst/Atm_uTest.cpp`                     | `.atm` reader/writer directly: header validation, magic/version, truncated header + body, size-mismatch + null guards, round-trips                        |
| `utst/ConstantAtmosphere_uTest.cpp`      | Default vacuum; negative-input clamps; sound speed; query at any alt; vacuum-warning status                                                               |
| `utst/ExponentialAtmosphere_uTest.cpp`   | ISA defaults; rho/e at h=H; ideal gas P; isothermal sound speed; per-parameter error codes; NaN altitude; convenience-accessor output-unmodified-on-error |
| `utst/LayeredAtmosphere_uTest.cpp`       | initFromMemory + .atm load round-trip; per-error-code rejection; out-of-range warning; **USSA76 reference values at 7 standard altitudes < 1%**           |
| `utst/Ussa76AtmosphereModel_uTest.cpp`   | Earth wrapper is pre-loaded; canonical layer boundaries; sea-level reference                                                                              |
| `utst/VacuumAtmosphereModel_uTest.cpp`   | Moon wrapper is vacuum; query warns + zero-fills; valid at any altitude                                                                                   |
| `ptst/Atmosphere_pTest.cpp`              | Layered + exponential query throughput; `.atm` load time                                                                                                  |
| `dtst/00_HorizonProceduralAtm_dTest.cpp` | Loads a horizon-CLI-generated `.atm` (alpha exponential)                                                                                                  |
| `dtst/01_HorizonRealUssa76_dTest.cpp`    | Loads horizon `convert-ussa76` output; matches published USSA76 reference table                                                                           |

Run unit tests:

```bash
ctest --test-dir build/hosted-x86_64-coverage -R TestSimEnvironmentAtmosphere
```

Run integration dtests (require horizon-generated .atm fixtures):

```bash
./build/hosted-x86_64-debug/bin/devtests/SimEnvironmentAtmosphere_Dev
```

---

## Real-Time Considerations

`query()`, `density()`, `pressure()`, and `temperature()` are RT-safe after
the model is initialized: no allocation, no I/O, no locks. `load()` and
`initFromMemory()` allocate the layer table and are NOT RT-safe; call them
during setup, off the control loop.

Measured on the hosted x86-64 debug build (Vernier 1.0.2; per-op derived from
calls/s):

| Operation                    | Cost         | Notes                                                            |
| ---------------------------- | ------------ | ---------------------------------------------------------------- |
| Layered (USSA76) query       | ~55 ns/query | O(log N) layer search + one pow/exp; IPC 2.64, branch-miss 0.18% |
| Exponential query            | ~12 ns/query | one exp; IPC 3.57, branch-miss 0.13%                             |
| `.atm` load (7-layer USSA76) | ~2.5 us/load | syscall/I/O-bound; off the RT path                               |

The query path is transcendental-bound -- the `pow`/`exp` in the hydrostatic
pressure law are intrinsic to a reference-faithful USSA76 model -- with no
cache or misprediction pathology, so it is already at its useful floor. The
analytic constant/vacuum models are branch-only and faster still.

## Conventions and scope

The library follows the same structure as `gravity` and `terrain`: a base
class, an analytic fidelity ladder, a body-agnostic file-backed full model,
and body-specific wrappers, dispatched by the `EnvironmentFactory` on
(Body, fidelity). It shares the status-enum convention (`SUCCESS=0`, `WARN_*`,
`ERROR_*`, `EOE_*` marker), the wire-format-only contract with external
producers (its own reader/writer, `static_assert`-enforced byte layout), and
the `data/{earth,moon,procedural}/` fixture layout.

Two scope notes specific to atmosphere:

- **No multi-LOD pyramid.** Atmospheres are tiny (<= 288 bytes for USSA76);
  one file per body suffices.
- **No converter for raw input.** USSA76's "raw input" is a paper
  specification (NASA TM-X-74335); the reference table is emitted directly by
  the upstream `convert-ussa76`.

The model uses the altitude argument as geopotential altitude (the variable
the USSA76 hydrostatic formula expects). Published reference tables indexed by
geometric altitude differ by about 2% at 30 km, growing with altitude.

---

## See also

- `sim::environment::gravity` -- mirror hierarchy for gravity.
- `sim::environment::terrain` -- mirror hierarchy for terrain.
- `sim::environment::factory` -- cross-subsystem dispatch.
- horizon `tools/world_data/` -- canonical home for converters,
  procedural generators, and sample world specs.
