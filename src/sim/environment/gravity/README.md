# Gravity Model Library

**Namespace:** `sim::environment::gravity`
**Platform:** Linux-only
**C++ Standard:** C++23

High-performance spherical harmonic gravity models for aerospace simulation. Supports Earth (EGM2008, degree 2190) and Moon (GRGM1200A, degree 1200) with O(N^2) RT-safe evaluation after initialization.

---

## Table of Contents

1. [Quick Links](#1-quick-links)
2. [Design Principles](#2-design-principles)
3. [Domains](#3-domains)
4. [Building](#4-building)
5. [Dependencies](#5-dependencies)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Links

| Domain | Documentation                                  | Key Classes                                               |
| ------ | ---------------------------------------------- | --------------------------------------------------------- |
| Earth  | [docs/GRAVITY_EARTH.md](docs/GRAVITY_EARTH.md) | `Egm2008Model`, `Geodetic`, `GeoidModel`                  |
| Moon   | [docs/GRAVITY_MOON.md](docs/GRAVITY_MOON.md)   | `GrailModel`, `LunarConstants`                            |
| Common | (this README)                                  | `SphericalHarmonicModel`, `J2GravityModel`, `CoeffSource` |

---

## 2. Design Principles

| Principle                     | Rationale                                                       |
| ----------------------------- | --------------------------------------------------------------- |
| **RT-safe after init**        | No allocation or file I/O in evaluation hot path                |
| **Body-agnostic core**        | SphericalHarmonicModel works for any body with C/S coefficients |
| **Analytic derivatives**      | ~15-20% faster than numeric gradient at N=50                    |
| **Pre-computed coefficients** | File I/O only at init, not in hot path                          |
| **CUDA acceleration**         | GPU batch evaluation for Earth model (100x speedup at N=2190)   |

### Model Hierarchy

| Model                    | Complexity | RT-Safe    | Use Case                                 |
| ------------------------ | ---------- | ---------- | ---------------------------------------- |
| `ConstantGravityModel`   | O(1)       | Yes        | Testing, fallback                        |
| `J2GravityModel`         | O(1)       | Yes        | Fast orbit propagation (~99% accuracy)   |
| `ZonalGravityModel`      | O(N)       | Yes        | Zonal harmonics only (symmetric bodies)  |
| `SphericalHarmonicModel` | O(N^2)     | After init | Full spherical harmonics (body-agnostic) |
| `Egm2008Model`           | O(N^2)     | After init | Earth (WGS84/EGM2008, N up to 2190)      |
| `GrailModel`             | O(N^2)     | After init | Moon (GRGM1200A, N up to 1200)           |
| `GeoidModel`             | O(N^2)     | After init | Geoid undulation computation             |

---

## 3. Domains

### J2 Gravity (Any Body)

```cpp
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

using namespace sim::environment::gravity;

J2GravityModel model;
J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
model.init(params);

const double R[3] = {7000e3, 0.0, 0.0};
double a[3] = {};
model.acceleration(R, a);
```

### Earth - Full EGM2008

```cpp
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"

using namespace sim::environment::gravity;

FullTableCoeffSource src;
src.open("egm2008_full.bin");

Egm2008Model model;
Egm2008Params params;
params.N = 360;
model.init(src, params);

const double R[3] = {7000e3, 0.0, 0.0};  // ECEF [m]
double V = 0.0, a[3] = {};
model.evaluate(R, V, a);
```

### Moon - Full GRAIL

```cpp
#include "src/sim/environment/gravity/inc/moon/GrailModel.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"

using namespace sim::environment::gravity;

FullTableCoeffSource src;
src.open("grgm1200a_full.bin");

GrailModel model;
GrailParams params;
params.N = 360;
model.init(src, params);

const double R[3] = {2000e3, 0.0, 0.0};  // MCMF [m]
double V = 0.0, a[3] = {};
model.evaluate(R, V, a);
```

---

## 4. Building

```bash
# Build
make compose-debug

# Run all tests
make compose-testp
```

### Architecture

#### Class Hierarchy

```
GravityModelBase (interface)
+-- ConstantGravityModel (O(1) radial gravity)
+-- J2GravityModel (O(1) J2 approximation)
+-- SphericalHarmonicModel (generic spherical harmonics)
    +-- Egm2008Model (Earth: WGS84/EGM2008)
    +-- GrailModel (Moon: GRGM1200A)
```

#### Coefficient Sources

```
CoeffSource (interface)
+-- FullTableCoeffSource (36-byte binary records, file I/O)
+-- SlimCoeffSourceD (20-byte double-precision, memory-mapped)
+-- SlimCoeffSourceF (12-byte single-precision, memory-mapped)
```

#### Binary Formats

**Full Format (36-byte records)** - Includes uncertainties, for analysis:

| Field  | Type   | Size | Description              |
| ------ | ------ | ---- | ------------------------ |
| n      | int16  | 2    | Degree                   |
| m      | int16  | 2    | Order                    |
| Cbar   | double | 8    | Normalized C coefficient |
| Sbar   | double | 8    | Normalized S coefficient |
| sigmaC | double | 8    | Uncertainty in C         |
| sigmaS | double | 8    | Uncertainty in S         |

**Slim-Double Format (20-byte records)** - Compact, full precision:

| Field | Type   | Size | Description              |
| ----- | ------ | ---- | ------------------------ |
| n     | int16  | 2    | Degree                   |
| m     | int16  | 2    | Order                    |
| Cbar  | double | 8    | Normalized C coefficient |
| Sbar  | double | 8    | Normalized S coefficient |

**Slim-Float Format (12-byte records)** - Minimum footprint:

| Field | Type  | Size | Description              |
| ----- | ----- | ---- | ------------------------ |
| n     | int16 | 2    | Degree                   |
| m     | int16 | 2    | Order                    |
| Cbar  | float | 4    | Normalized C coefficient |
| Sbar  | float | 4    | Normalized S coefficient |

**File Sizes for EGM2008 N=2190:**

| Format | Size  | Use Case                 |
| ------ | ----- | ------------------------ |
| Full   | 86 MB | Analysis, uncertainty    |
| Slim-D | 48 MB | Runtime, full precision  |
| Slim-F | 29 MB | Embedded, reduced memory |

---

## 5. Dependencies

### Required

- C++23 compiler
- `utilities/math/legendre` - Legendre polynomial computation

### Optional

- CUDA Toolkit 11.0+ - GPU acceleration for Earth model

### Data Sources

| Body  | Source                                               |
| ----- | ---------------------------------------------------- |
| Earth | [NGA EGM2008](https://earth-info.nga.mil/)           |
| Moon  | [NASA GRAIL](https://pgda.gsfc.nasa.gov/products/50) |

---

## 6. Testing

Run tests using the standard Docker workflow:

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run gravity tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L gravity
```

### Test Organization

| Directory | Tests | Description       |
| --------- | ----- | ----------------- |
| `utst/`   | 53    | Unit tests        |
| `tst/`    | 10    | Dev tests         |
| `ptst/`   | 20    | Performance tests |

### Expected Output

```
100% tests passed, 0 tests failed out of 83
```

---

## 7. See Also

- **[docs/GRAVITY_EARTH.md](docs/GRAVITY_EARTH.md)** - Earth models, geodetic utilities, geoid
- **[docs/GRAVITY_MOON.md](docs/GRAVITY_MOON.md)** - Lunar models, GRAIL data
- **`src/utilities/math/legendre/`** - Legendre polynomial library
