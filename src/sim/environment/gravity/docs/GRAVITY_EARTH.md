# Earth Gravity Models

**Namespace:** `sim::environment::gravity`
**Platform:** Linux-only
**C++ Standard:** C++17

Earth gravity field models including EGM2008 spherical harmonics (degree/order up to 2190), J2/zonal approximations, geodetic coordinate utilities, and geoid undulation computation.

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

| Question                                              | Module                              |
| ----------------------------------------------------- | ----------------------------------- |
| What is the gravitational acceleration at a position? | `Egm2008Model`, `J2GravityModel`    |
| What is the gravitational potential?                  | `Egm2008Model`, `ZonalGravityModel` |
| How do I convert ECEF to geodetic coordinates?        | `Geodetic`                          |
| What is the geoid undulation at a location?           | `GeoidModel`                        |
| What are the WGS84 ellipsoid parameters?              | `Wgs84Constants`                    |
| How do I convert GPS height to orthometric height?    | `GeoidModel`                        |
| What is normal gravity at a latitude?                 | `Geodetic`                          |
| How do I transform between ECEF and NED frames?       | `Geodetic`                          |

---

## 2. Quick Reference

### Headers

```cpp
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/ZonalGravityModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"
#include "src/sim/environment/gravity/inc/earth/GeoidModel.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
```

### One-Shot Query

```cpp
using namespace sim::environment::gravity;

// J2 gravity (fast, ~99% accuracy)
J2GravityModel model;
J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
model.init(params);

const double R[3] = {7000e3, 0.0, 0.0};  // ECEF position [m]
double a[3] = {};
model.acceleration(R, a);
// a ~ [-8.14, 0, 0] m/s^2 (toward Earth center)
```

### Full Spherical Harmonics

```cpp
using namespace sim::environment::gravity;

FullTableCoeffSource src;
src.open("egm2008_full.bin");

Egm2008Model model;
Egm2008Params params;
params.N = 360;  // Degree (max 2190)
model.init(src, params);

const double R[3] = {7000e3, 0.0, 0.0};
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

### Model Hierarchy

| Model                  | Complexity | Accuracy | Use Case              |
| ---------------------- | ---------- | -------- | --------------------- |
| `ConstantGravityModel` | O(1)       | Low      | Testing, fallback     |
| `J2GravityModel`       | O(1)       | ~99%     | Fast LEO propagation  |
| `ZonalGravityModel`    | O(N)       | ~99.9%   | Intermediate fidelity |
| `Egm2008Model`         | O(N^2)     | Full     | High-precision        |

### Coordinate System

All gravity models use **ECEF** (Earth-Centered Earth-Fixed) coordinates:

- Input: position `r[3]` in meters
- Output: acceleration `a[3]` in m/s^2, potential `V` in m^2/s^2

---

## 4. Module Reference

### Egm2008Model

**Header:** `inc/earth/Egm2008Model.hpp`
**Purpose:** Full EGM2008 spherical harmonics gravity model.

#### Key Types

```cpp
struct Egm2008Params {
  double GM = wgs84::GM;   ///< Gravitational parameter [m^3/s^2].
  double a = wgs84::A;     ///< WGS84 semi-major axis [m].
  int16_t N = 180;         ///< Max degree (default 180, max 2190).
};
```

#### API

```cpp
/**
 * @brief Initialize with Earth-specific parameters.
 * @note NOT RT-safe: Allocates scratch buffers.
 */
bool init(const CoeffSource& src, const Egm2008Params& p) noexcept;

/**
 * @brief Compute potential and acceleration.
 * @note RT-safe after init(): No allocation, O(N^2).
 */
bool evaluate(const double r[3], double& V, double a[3]) const noexcept;
```

#### Usage

```cpp
FullTableCoeffSource src;
src.open("egm2008_full.bin");

Egm2008Model model;
Egm2008Params params;
params.N = 360;
model.init(src, params);

// Use analytic derivatives (~4.8x faster than numeric)
model.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

const double R[3] = {7000e3, 0.0, 0.0};
double V = 0.0, a[3] = {};
model.evaluate(R, V, a);
```

### J2GravityModel

**Header:** `inc/J2GravityModel.hpp`
**Purpose:** Fast J2-only gravity capturing ~99% of oblateness effect.

#### Key Types

```cpp
struct J2Params {
  double GM;   ///< Gravitational parameter [m^3/s^2].
  double a;    ///< Reference radius [m].
  double J2;   ///< J2 coefficient (un-normalized).
};
```

#### API

```cpp
/**
 * @brief Initialize with body parameters.
 * @note RT-safe: No allocation.
 */
bool init(const J2Params& p) noexcept;

/**
 * @brief Compute acceleration at body-fixed position.
 * @note RT-safe: No allocation, O(1).
 */
bool acceleration(const double r[3], double a[3]) const noexcept override;
```

#### Usage

```cpp
J2GravityModel model;
J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
model.init(params);

const double R[3] = {7000e3, 0.0, 0.0};
double a[3] = {};
model.acceleration(R, a);
```

### ZonalGravityModel

**Header:** `inc/earth/ZonalGravityModel.hpp`
**Purpose:** Zonal harmonics (m=0 terms only) with built-in coefficients up to N=20.

#### Usage

```cpp
ZonalGravityModel model;
ZonalParams params;
params.N = 10;  // Use J2-J10
model.init(params);

const double R[3] = {7000e3, 0.0, 0.0};
double V = 0.0;
model.potential(R, V);
```

### Geodetic

**Header:** `inc/earth/Geodetic.hpp`
**Purpose:** Coordinate conversions and normal gravity computation.

#### API

```cpp
/// Convert geodetic to ECEF. RT-safe.
void geodeticToEcef(double lat, double lon, double alt, double ecef[3]) noexcept;

/// Convert ECEF to geodetic (Bowring's method). RT-safe.
void ecefToGeodetic(const double ecef[3], GeodeticCoord& out) noexcept;

/// Normal gravity at latitude and altitude (Somigliana). RT-safe.
double normalGravity(double lat, double alt) noexcept;

/// NED to ECEF rotation. RT-safe.
void nedToEcef(double lat, double lon, const double ned[3], double ecef[3]) noexcept;

/// ECEF to NED rotation. RT-safe.
void ecefToNed(double lat, double lon, const double ecef[3], double ned[3]) noexcept;
```

### GeoidModel

**Header:** `inc/earth/GeoidModel.hpp`
**Purpose:** Geoid undulation and height conversions.

#### Usage

```cpp
FullTableCoeffSource src;
src.open("egm2008_full.bin");

GeoidModel geoid;
GeoidParams params;
params.N = 360;
geoid.init(src, params);

double N = geoid.undulation(lat, lon);  // meters
double H = geoid.ellipsoidToOrthometric(lat, lon, h_ellipsoid);
```

### Wgs84Constants

**Header:** `inc/earth/Wgs84Constants.hpp`
**Purpose:** WGS84 ellipsoid and EGM2008 gravitational constants.

#### Constants

```cpp
namespace wgs84 {
  constexpr double A = 6378137.0;           // Equatorial radius [m]
  constexpr double B = 6356752.314245;      // Polar radius [m]
  constexpr double F = 1.0 / 298.257223563; // Flattening
  constexpr double E2 = 0.00669437999014;   // First eccentricity squared
  constexpr double GM = 3.986004418e14;     // Gravitational parameter [m^3/s^2]
  constexpr double OMEGA = 7.292115e-5;     // Angular velocity [rad/s]
}

namespace egm2008 {
  constexpr double J2 = 1.0826359e-3;       // Un-normalized J2
  constexpr double C20 = -0.484165143e-3;   // Normalized C20
}
```

---

## 5. Common Patterns

### Periodic Orbit Propagation

```cpp
J2GravityModel gravity;
J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
gravity.init(params);

// In propagation loop (RT-safe)
double r[3] = {state.x, state.y, state.z};
double a[3] = {};
gravity.acceleration(r, a);
// Apply acceleration to state derivative
```

### High-Precision Analysis

```cpp
FullTableCoeffSource src;
src.open("egm2008_full.bin");

Egm2008Model gravity;
Egm2008Params params;
params.N = 2190;  // Full resolution
gravity.init(src, params);
gravity.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

// Single evaluation (still RT-safe after init)
double V = 0.0, a[3] = {};
gravity.evaluate(r, V, a);
```

### Geodetic Position Processing

```cpp
// GPS receiver gives geodetic coordinates
double lat = degToRad(40.0);
double lon = degToRad(-75.0);
double alt = 1000.0;

// Convert to ECEF for gravity computation
double ecef[3];
geodeticToEcef(lat, lon, alt, ecef);

// Compute gravity
double a_ecef[3];
model.acceleration(ecef, a_ecef);

// Convert result to local NED frame
double a_ned[3];
ecefToNed(lat, lon, a_ecef, a_ned);
```

---

## 6. Real-Time Considerations

### RT-Safe Functions (after init)

- `Egm2008Model::potential()`, `acceleration()`, `evaluate()`
- `J2GravityModel::potential()`, `acceleration()`
- `ZonalGravityModel::potential()`, `acceleration()`
- `GeoidModel::undulation()`
- All `Geodetic` functions
- All `Wgs84Constants` (compile-time)

### NOT RT-Safe Functions

- `Model::init()` - Allocates coefficient buffers
- `FullTableCoeffSource::open()` - File I/O
- `FullTableCoeffSource::get()` - File seek/read

### Recommended Configuration

1. **Initialize at startup** - Call `init()` before entering RT context
2. **Pre-load coefficients** - Use `Egm2008Model` which caches coefficients
3. **Choose appropriate degree** - N=180 is often sufficient for LEO (~1mm accuracy)
4. **Use analytic mode** - `setAccelMode(Analytic)` is ~4.8x faster

---

## 7. Data Files

### EGM2008 Source Data

Available from NGA: <https://earth-info.nga.mil/>

| File                          | Size   | Description                          |
| ----------------------------- | ------ | ------------------------------------ |
| `EGM2008_to2190_TideFree.txt` | 242 MB | Gravitational potential coefficients |
| `Zeta-to-N_to2160_egm2008`    | 142 MB | Geoid undulation conversion          |

### Binary Format

36-byte packed records (no file header):

```
int16_t n       (2 bytes) - degree
int16_t m       (2 bytes) - order
double  Cbar    (8 bytes) - normalized C coefficient
double  Sbar    (8 bytes) - normalized S coefficient
double  sigmaC  (8 bytes) - uncertainty in C
double  sigmaS  (8 bytes) - uncertainty in S
```

---

## 8. Example

```cpp
/**
 * @file example_earth_gravity.cpp
 * @brief Example: Earth gravity computation at various fidelity levels.
 */

#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"

#include <cstdio>

using namespace sim::environment::gravity;

int main() {
  // Position: 400 km altitude over equator
  const double LAT = 0.0;
  const double LON = 0.0;
  const double ALT = 400e3;

  double ecef[3];
  geodeticToEcef(LAT, LON, ALT, ecef);

  std::printf("Position: ECEF = [%.1f, %.1f, %.1f] km\n",
              ecef[0] / 1e3, ecef[1] / 1e3, ecef[2] / 1e3);

  // J2 Model (fast)
  J2GravityModel j2Model;
  J2Params j2Params{wgs84::GM, wgs84::A, egm2008::J2};
  j2Model.init(j2Params);

  double a_j2[3] = {};
  j2Model.acceleration(ecef, a_j2);
  std::printf("J2 Model:     a = [%.6f, %.6f, %.6f] m/s^2\n",
              a_j2[0], a_j2[1], a_j2[2]);

  // EGM2008 Model (high fidelity)
  FullTableCoeffSource src;
  if (src.open("egm2008_full.bin")) {
    Egm2008Model egmModel;
    Egm2008Params egmParams;
    egmParams.N = 360;
    egmModel.init(src, egmParams);
    egmModel.setAccelMode(SphericalHarmonicModel::AccelMode::Analytic);

    double V = 0.0, a_egm[3] = {};
    egmModel.evaluate(ecef, V, a_egm);
    std::printf("EGM2008 N=360: a = [%.6f, %.6f, %.6f] m/s^2\n",
                a_egm[0], a_egm[1], a_egm[2]);
    std::printf("               V = %.3f m^2/s^2\n", V);
  }

  // Normal gravity (reference)
  const double GAMMA = normalGravity(LAT, ALT);
  std::printf("Normal gravity: %.6f m/s^2\n", GAMMA);

  return 0;
}
```

---

## 9. See Also

- **[../README.md](../README.md)** - Gravity library overview
- **[GRAVITY_MOON.md](GRAVITY_MOON.md)** - Lunar gravity models
- **`src/utilities/math/legendre/`** - Legendre polynomial library
