# Terrain Library

**Namespace:** `sim::environment::terrain`
**Platform:** Linux
**C++ Standard:** C++23
**Library:** `sim_environment_terrain`

Digital elevation queries for aerospace simulation. Hierarchy mirrors
`sim::environment::gravity`: an abstract base, a fidelity ladder of
analytic models, a body-agnostic full-DEM consumer, and Earth/Moon
convenience wrappers that bake in body-specific defaults.

This library reads the **`.htile`** binary terrain tile format (see
[`inc/Htile.hpp`](inc/Htile.hpp) for the wire spec). The format is a
documented byte layout -- any external tool can produce a conforming
`.htile` file. One such producer is the `horizon_world` CLI in the
horizon repo (`convert-hgt` for SRTM `.hgt` data, `convert-pds` for
PDS IMG+LBL data, `gen-terrain` / `gen-pyramid` / `gen-system` for
procedural data).

---

## Model hierarchy

| Model                     | Backing            | RT-safe (after init) | Use case                                   |
| ------------------------- | ------------------ | -------------------- | ------------------------------------------ |
| `ConstantTerrain`         | analytic           | yes                  | Flat-ground baseline; testing              |
| `SphereTerrain`           | analytic           | yes                  | Spherical body, no DEM                     |
| `EllipsoidTerrain`        | analytic           | yes                  | Oblate body (Earth, Mars), no DEM          |
| `HtileTile`               | htile file         | yes (after `load`)   | Body-agnostic full-DEM consumer            |
| `earth::SrtmTerrainModel` | htile (Earth-vali) | yes (after `load`)   | Earth wrapper with WGS84 + EGM96 defaults  |
| `moon::LolaTerrainModel`  | htile (Moon-vali)  | yes (after `load`)   | Moon wrapper with lunar reference defaults |

This mirrors gravity's ladder
(`Constant` -> `J2` -> `SphericalHarmonic` -> `Egm2008` / `Grail`):
analytic baselines first, body-agnostic full model in the middle,
body-specific named wrappers on top.

---

## Public surface

| Header                                | Purpose                                                     |
| ------------------------------------- | ----------------------------------------------------------- |
| `inc/TerrainModelBase.hpp`            | Abstract base; geodetic + ECEF lookup, coverage, resolution |
| `inc/TerrainStatus.hpp`               | `Status` enum + helpers (mirrors `GravityStatus`)           |
| `inc/Htile.hpp`                       | htile format header + R/W (imported from horizon)           |
| `inc/HtileTile.hpp`                   | Body-agnostic htile-backed terrain model                    |
| `inc/ConstantTerrain.hpp`             | Constant-elevation analytic baseline                        |
| `inc/SphereTerrain.hpp`               | Spherical-body analytic baseline                            |
| `inc/EllipsoidTerrain.hpp`            | Oblate-spheroid analytic (Bowring iteration for ECEF->H)    |
| `inc/earth/SrtmTerrainModel.hpp`      | Earth wrapper subclass of `HtileTile` (WGS84 validation)    |
| `inc/earth/Wgs84TerrainConstants.hpp` | `R_EQ_M`, `R_POL_M`, `R_TOLERANCE_M`, `REF_SURFACE_NAME`    |
| `inc/moon/LolaTerrainModel.hpp`       | Moon wrapper subclass of `HtileTile` (lunar validation)     |
| `inc/moon/LunarTerrainConstants.hpp`  | `R_REF_M`, `R_TOLERANCE_M`, `REF_SURFACE_NAME`              |

---

## Quick start

### Analytic baselines

```cpp
#include "src/sim/environment/terrain/inc/ConstantTerrain.hpp"

sim::environment::terrain::ConstantTerrain flat(/*h0=*/0.0);
double H = 0.0;
flat.elevationAt(/*latRad=*/0.0, /*lonRad=*/0.0, H);  // -> 0.0
```

```cpp
#include "src/sim/environment/terrain/inc/EllipsoidTerrain.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"

namespace wgs84 = sim::environment::terrain::earth::wgs84;
sim::environment::terrain::EllipsoidTerrain wgs(wgs84::R_EQ_M, wgs84::R_POL_M);
const double ECEF[3] = {wgs84::R_EQ_M + 400000.0, 0.0, 0.0}; // 400 km equator
double H = 0.0;
wgs.elevationAtEcef(ECEF, H);  // -> ~400000.0 m
```

### Full DEM (htile)

```cpp
#include "src/sim/environment/terrain/inc/HtileTile.hpp"

sim::environment::terrain::HtileTile tile;
tile.load("data/earth/srtm_global.htile");
double H = 0.0;
tile.elevationAt(/*latRad=*/0.683, /*lonRad=*/-1.85, H); // bilinear lookup
```

### Earth / Moon wrappers (defaults + validation)

```cpp
#include "src/sim/environment/terrain/inc/earth/SrtmTerrainModel.hpp"

sim::environment::terrain::earth::SrtmTerrainModel earth;
earth.loadDefault();          // opens default SRTM htile path; validates WGS84/EGM96
// ...or...
earth.loadEarth("data/earth/N39W106.htile"); // validates Earth-class metadata
```

```cpp
#include "src/sim/environment/terrain/inc/moon/LolaTerrainModel.hpp"

sim::environment::terrain::moon::LolaTerrainModel moon;
moon.loadMoon("data/moon/ldem_global.htile"); // validates lunar metadata
```

The wrappers call through to `HtileTile::load()` then run a small
metadata check: `ref_radius_m` must be within tolerance of the body's
known reference radius, and `ref_surface` must match the body's
expected name (`"egm96"` for Earth, `"sphere"` for Moon).

---

## htile format

128-byte self-describing header followed by row-major `int16` samples
(north-to-south rows). Heights in meters above the body's declared
reference surface; `sample * scale_m_per_dn`. Voids are `-32768`.

The format spec is documented inline at the top of `inc/Htile.hpp`.
This library implements its own reader/writer (`HtileReader`,
`HtileWriter`, `HtileHeader`) under the `sim::environment::terrain`
namespace. Producers (such as horizon's `horizon_world` CLI) implement
the same wire spec independently. The contract between producer and
consumer is the byte layout, not shared code.

---

## Real-time considerations

`elevationAt` / `elevationAtEcef` are RT-safe once a model is loaded: bounded
work, no allocation, no I/O. `load()` is **not** RT-safe (file I/O + one
allocation) and must run during init, off the real-time path.

Measured on the bilinear `HtileTile` consumer (see
[`docs/optimization/`](../../../../docs/optimization/) for the full baseline):

| operation              | throughput      | latency      | notes                           |
| ---------------------- | --------------- | ------------ | ------------------------------- |
| `elevationAt` (1024^2) | ~26 M queries/s | ~39 ns/query | compute-bound, IPC 3.45         |
| `load` 256^2 (128 KB)  | ~27 K loads/s   | ~37 us/load  | I/O + one allocation, init-only |
| `load` 1024^2 (2 MB)   | ~1.8 K loads/s  | ~570 us/load | scales linearly with tile size  |

The tile is held in a single contiguous `dim^2 * 2` byte buffer; queries do not
allocate and reloading does not churn the heap.

---

## Testing

| Suite                             | What it covers                                                                                         |
| --------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `utst/Htile_uTest.cpp`            | Reader/writer: round-trip, bad magic/version, malformed header/body, truncation, void/scale validation |
| `utst/HtileTile_uTest.cpp`        | Bilinear (row + column); coverage; wraparound longitude; voids; ECEF; resolution                       |
| `utst/TerrainStatus_uTest.cpp`    | `toString` + `isSuccess`/`isWarning`/`isError` for every code                                          |
| `utst/ConstantTerrain_uTest.cpp`  | Constant elevation; global coverage; ECEF parity                                                       |
| `utst/SphereTerrain_uTest.cpp`    | Surface == 0; ECEF radial offset                                                                       |
| `utst/EllipsoidTerrain_uTest.cpp` | Surface == 0; ECEF Bowring iteration; flattening / e^2                                                 |
| `utst/SrtmTerrainModel_uTest.cpp` | WGS84 metadata validation; Earth-class accept/reject                                                   |
| `utst/LolaTerrainModel_uTest.cpp` | Lunar metadata validation; Moon-class accept/reject                                                    |
| `ptst/HtileTile_pTest.cpp`        | Load time (256/1024) + `elevationAt` query throughput                                                  |
| `dtst/*_Horizon*_dTest.cpp`       | Ground-truth: procedural htile + real SRTM (USGS) + real LOLA (lunar)                                  |

Run unit tests:

```bash
make compose-testp
```

Run integration dtest (requires horizon-generated htile fixture):

```bash
./build/hosted-x86_64-debug/bin/dtests/SimEnvironmentTerrain_DTEST \
    --gtest_filter="HorizonProceduralHtile*"
```

---

## See also

- `sim::environment::gravity` -- mirror hierarchy for the gravity side
  of the environment subsystem.
- horizon `tools/world_data/` -- canonical home for converters,
  procedural generators, and sample world specs.
