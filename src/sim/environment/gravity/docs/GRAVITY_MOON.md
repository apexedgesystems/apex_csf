# Lunar Gravity Models

**Namespace:** `sim::environment::gravity`
**Platform:** Linux-only
**C++ Standard:** C++17

Lunar gravity field models based on the GRAIL (Gravity Recovery and Interior Laboratory) mission data, providing the highest-resolution gravity map of the Moon (degree/order up to 1200).

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Reference](#2-quick-reference)
3. [Design Principles](#3-design-principles)
4. [Module Reference](#4-module-reference)
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [Data Files](#7-data-files)
8. [Example](#8-example)
9. [See Also](#9-see-also)

---

## 1. Overview

| Question                                                            | Module                         |
| ------------------------------------------------------------------- | ------------------------------ |
| What is the gravitational acceleration at a selenocentric position? | `GrailModel`, `J2GravityModel` |
| What is the gravitational potential?                                | `GrailModel`                   |
| What are the lunar reference constants?                             | `LunarConstants`               |
| How do I use a fast J2 approximation for the Moon?                  | `J2GravityModel`               |

---

## 2. Quick Reference

### Headers

```cpp
#include "src/sim/environment/gravity/inc/moon/GrailModel.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
```

### One-Shot Query (J2 Approximation)

```cpp
using namespace sim::environment::gravity;

J2GravityModel model;
J2Params params;
params.GM = lunar::GM;
params.a = lunar::R_REF;
params.J2 = grgm1200a::J2;
model.init(params);

const double R[3] = {2000e3, 0.0, 0.0};  // MCMF position [m]
double a[3] = {};
model.acceleration(R, a);
```

### Full Spherical Harmonics

```cpp
using namespace sim::environment::gravity;

FullTableCoeffSource src;
src.open("grgm1200a_full.bin");

GrailModel model;
GrailParams params;
params.N = 360;  // Degree (max 1200)
model.init(src, params);

const double R[3] = {2000e3, 0.0, 0.0};  // MCMF position [m]
double V = 0.0, a[3] = {};
model.evaluate(R, V, a);
```

---

## 3. Design Principles

### RT-Safety Annotations

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

### Coordinate System

All lunar gravity models use **MCMF** (Moon-Centered Moon-Fixed) coordinates:

- Input: position `r[3]` in meters (selenocentric)
- Output: acceleration `a[3]` in m/s^2, potential `V` in m^2/s^2

### Model Selection

| Model            | Complexity | Accuracy | Use Case                     |
| ---------------- | ---------- | -------- | ---------------------------- |
| `J2GravityModel` | O(1)       | ~99%     | Fast lunar orbit propagation |
| `GrailModel`     | O(N^2)     | Full     | High-precision selenodesy    |

---

## 4. Module Reference

### GrailModel

**Header:** `inc/moon/GrailModel.hpp`
**Purpose:** Full GRGM1200A spherical harmonics gravity model for the Moon.

#### Key Types

```cpp
struct GrailParams {
  double GM = lunar::GM;       ///< Moon gravitational parameter [m^3/s^2].
  double a = lunar::R_REF;     ///< Lunar reference radius [m].
  int16_t N = 360;             ///< Max degree (default 360, max 1200).
};
```

#### API

```cpp
/**
 * @brief Initialize with Moon-specific parameters.
 * @param src GRAIL coefficient source (must outlive this model).
 * @param p Lunar model parameters (defaults to GRGM1200A).
 * @return false if invalid params or source.
 * @note NOT RT-safe: Allocates scratch buffers.
 */
bool init(const CoeffSource& src, const GrailParams& p) noexcept;

/**
 * @brief Initialize with default lunar parameters.
 * @param src GRAIL coefficient source.
 * @param maxDegree Maximum degree to use.
 * @return false if invalid source.
 * @note NOT RT-safe: Allocates scratch buffers.
 */
bool init(const CoeffSource& src, int16_t maxDegree = 360) noexcept;
```

#### Inherited from SphericalHarmonicModel

```cpp
/**
 * @brief Compute gravitational potential V at body-fixed position.
 * @note RT-safe after init(): No allocation, O(N^2).
 */
bool potential(const double r[3], double& V) const noexcept override;

/**
 * @brief Compute acceleration at body-fixed position.
 * @note RT-safe after init(): No allocation, O(N^2).
 */
bool acceleration(const double r[3], double a[3]) const noexcept override;

/**
 * @brief Combined evaluation: compute V and a in a single pass.
 * @note RT-safe after init(): No allocation, O(N^2).
 */
bool evaluate(const double r[3], double& V, double a[3]) const noexcept;

/**
 * @brief Set acceleration computation mode.
 * @param m Mode (Numeric or Analytic).
 * @note RT-safe: O(1).
 */
void setAccelMode(AccelMode m) noexcept;
```

#### Usage

```cpp
FullTableCoeffSource src;
src.open("grgm1200a_full.bin");

GrailModel model;
GrailParams params;
params.N = 360;
model.init(src, params);

// Use analytic derivatives (~4.8x faster than numeric)
model.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

const double R[3] = {2000e3, 0.0, 0.0};  // 2000 km from Moon center
double V = 0.0, a[3] = {};
model.evaluate(R, V, a);
```

### LunarConstants

**Header:** `inc/moon/LunarConstants.hpp`
**Purpose:** Lunar reference frame and GRGM1200A gravitational constants.

#### Constants

```cpp
namespace lunar {
  constexpr double R_MEAN = 1737400.0;      // Mean radius [m]
  constexpr double R_REF = 1738000.0;       // Reference radius (GRGM1200A) [m]
  constexpr double GM = 4.9028e12;          // Gravitational parameter [m^3/s^2]
  constexpr double OMEGA = 2.6617e-6;       // Angular velocity [rad/s]
  constexpr double G_SURFACE = 1.624;       // Surface gravity [m/s^2]
}

namespace grgm1200a {
  constexpr double C20 = 9.088e-5;          // Normalized C20 (J2 term)
  constexpr double C22 = 3.471e-5;          // Equatorial ellipticity
  constexpr double J2 = 2.032e-4;           // Un-normalized J2
  constexpr int16_t MAX_DEGREE = 1200;      // Maximum available degree
}
```

---

## 5. Common Patterns

### Lunar Orbit Propagation (Fast)

```cpp
J2GravityModel gravity;
J2Params params;
params.GM = lunar::GM;
params.a = lunar::R_REF;
params.J2 = grgm1200a::J2;
gravity.init(params);

// In propagation loop (RT-safe)
double r[3] = {state.x, state.y, state.z};
double a[3] = {};
gravity.acceleration(r, a);
```

### High-Precision Lunar Analysis

```cpp
FullTableCoeffSource src;
src.open("grgm1200a_full.bin");

GrailModel gravity;
GrailParams params;
params.N = 660;  // High resolution (< 10 km surface)
gravity.init(src, params);
gravity.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

// Single evaluation (RT-safe after init)
double V = 0.0, a[3] = {};
gravity.evaluate(r, V, a);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions (after init)

- `GrailModel::potential()`, `acceleration()`, `evaluate()`
- `J2GravityModel::potential()`, `acceleration()`
- All `LunarConstants` (compile-time)

### NOT RT-Safe Functions

- `GrailModel::init()` - Allocates coefficient buffers
- `FullTableCoeffSource::open()` - File I/O
- `FullTableCoeffSource::get()` - File seek/read

### Recommended Configuration

1. **Initialize at startup** - Call `init()` before entering RT context
2. **Pre-load coefficients** - `GrailModel` caches coefficients internally
3. **Choose appropriate degree** - N=360 is often sufficient for low lunar orbit
4. **Use analytic mode** - `setAccelMode(Analytic)` is ~4.8x faster

---

## 7. Data Files

### GRAIL Data Source

The lunar gravity coefficients come from NASA's Planetary Geodynamics Data Archive:

**GRGM1200A Model:**

- URL: <https://pgda.gsfc.nasa.gov/products/50>
- Resolution: Degree/Order 1200 (surface resolution < 5 km)
- Mission: GRAIL (2011-2012)
- Reference: Lemoine et al., 2014

### Source Files

| File                  | Size  | Description                        |
| --------------------- | ----- | ---------------------------------- |
| `gggrx_1200a_sha.tab` | 84 MB | Original SHA text file (source)    |
| `gggrx_1200a_sha.lbl` | 11 KB | PDS label with metadata            |
| `grgm1200a_full.bin`  | 25 MB | Converted binary (36-byte records) |

### Binary Format

Same 36-byte record format as EGM2008:

```
int16_t n       (2 bytes) - degree
int16_t m       (2 bytes) - order
double  Cbar    (8 bytes) - normalized C coefficient
double  Sbar    (8 bytes) - normalized S coefficient
double  sigmaC  (8 bytes) - uncertainty in C
double  sigmaS  (8 bytes) - uncertainty in S
```

### Downloading Source Data

```bash
# From PDS Geosciences Node
wget http://pds-geosciences.wustl.edu/grail/grail-l-lgrs-5-rdr-v1/grail_1001/shadr/gggrx_1200a_sha.tab
wget http://pds-geosciences.wustl.edu/grail/grail-l-lgrs-5-rdr-v1/grail_1001/shadr/gggrx_1200a_sha.lbl
```

---

## 8. Example

```cpp
/**
 * @file example_lunar_gravity.cpp
 * @brief Example: Lunar gravity computation at various fidelity levels.
 */

#include "src/sim/environment/gravity/inc/moon/GrailModel.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"

#include <cstdio>

using namespace sim::environment::gravity;

int main() {
  // Position: 100 km altitude above lunar equator
  const double ALT = 100e3;
  const double R[3] = {lunar::R_REF + ALT, 0.0, 0.0};

  std::printf("Position: %.1f km from Moon center\n",
              (lunar::R_REF + ALT) / 1e3);

  // J2 Model (fast)
  J2GravityModel j2Model;
  J2Params j2Params;
  j2Params.GM = lunar::GM;
  j2Params.a = lunar::R_REF;
  j2Params.J2 = grgm1200a::J2;
  j2Model.init(j2Params);

  double a_j2[3] = {};
  j2Model.acceleration(R, a_j2);
  std::printf("J2 Model:      a = [%.6f, %.6f, %.6f] m/s^2\n",
              a_j2[0], a_j2[1], a_j2[2]);

  // GRAIL Model (high fidelity)
  FullTableCoeffSource src;
  if (src.open("grgm1200a_full.bin")) {
    GrailModel grailModel;
    GrailParams grailParams;
    grailParams.N = 360;
    grailModel.init(src, grailParams);
    grailModel.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

    double V = 0.0, a_grail[3] = {};
    grailModel.evaluate(R, V, a_grail);
    std::printf("GRAIL N=360:   a = [%.6f, %.6f, %.6f] m/s^2\n",
                a_grail[0], a_grail[1], a_grail[2]);
    std::printf("               V = %.3f m^2/s^2\n", V);
  }

  // Reference: surface gravity
  std::printf("Surface gravity: %.3f m/s^2\n", lunar::G_SURFACE);

  return 0;
}
```

---

## 9. See Also

- **[../README.md](../README.md)** - Gravity library overview
- **[GRAVITY_EARTH.md](GRAVITY_EARTH.md)** - Earth gravity models
- **`src/utilities/math/legendre/`** - Legendre polynomial library
- **NASA GRAIL Mission:** <https://www.nasa.gov/mission_pages/grail/>
- **PDS Geosciences Node:** <https://pds-geosciences.wustl.edu/grail/>

---

## Earth vs Moon Comparison

| Property         | Earth (WGS84/EGM2008) | Moon (GRGM1200A) |
| ---------------- | --------------------- | ---------------- |
| GM               | 3.986e14 m^3/s^2      | 4.903e12 m^3/s^2 |
| Reference radius | 6,378,137 m           | 1,738,000 m      |
| Surface gravity  | 9.81 m/s^2            | 1.62 m/s^2       |
| J2               | 1.083e-3              | 2.03e-4          |
| Max degree       | 2190                  | 1200             |
| Model class      | `Egm2008Model`        | `GrailModel`     |

---

## References

1. Lemoine, F. G., et al. (2014). "GRGM900C: A degree 900 lunar gravity model from GRAIL primary and extended mission data." Geophysical Research Letters.

2. NASA GRAIL Mission: <https://www.nasa.gov/mission_pages/grail/>

3. PDS Geosciences Node: <https://pds-geosciences.wustl.edu/grail/>
