# sim_sensors

Sensor measurement models for atmospheric vehicles. Each model maps ground truth
to a measurement carrying physical error (noise + bias); the noise is drawn from
a deterministic, portable sampler so replay and Monte Carlo reproduce across
toolchains and targets. Header-only, parameterized, and vehicle-agnostic -- a
specific sensor's error figures come from the application.

## Contents

1. [Design](#design)
2. [Module reference](#module-reference)
   - [GaussianSampler](#gaussiansampler) - deterministic noise source
   - [SensorBase](#sensorbase) - shared base (kind, name, sampler)
   - [GPS](#gps) - GNSS position + velocity
   - [Pitot](#pitot) - indicated airspeed
   - [RadarAltimeter](#radaraltimeter) - height above ground
   - [BoxClearanceLidar](#boxclearancelidar) - six-axis clearance to a box
3. [Determinism](#determinism)
4. [Integration](#integration)

## Design

A sensor model is a pure measurement: `truth -> Reading{value, error}`. Two
boundaries keep the models reusable and the eventual hardware path an easy lift:

- **Device-agnostic.** A model reports a physical quantity (a geodetic fix, an
  indicated airspeed, an AGL altitude), never a wire representation. Protocol,
  framing, message cadence, and receiver-reported latency belong to a
  hardware-emulation layer, not here.
- **Timing-agnostic.** A model reports "the measurement if sampled now." When to
  sample (cadence) is the scheduler's concern.

Sensors measure different physical quantities, so each concrete sensor defines
its own `measure()` signature and its own flat measurement struct. `SensorBase`
shares the common machinery -- the noise sampler, seeding, and a kind/name
identity. A new sensor type is a new `SensorBase` subclass; `SensorKind::External`
is the catch-all for user-defined sensors.

## Module reference

### GaussianSampler

**Header:** `inc/GaussianSampler.hpp`

A standard-normal sampler: the standardized `std::mt19937` engine plus an explicit
Box-Muller transform built only from its output and basic arithmetic. Given a
seed, the sequence is identical on every platform. Box-Muller yields two normals
per pair of uniforms; the second is cached for the next call.

```cpp
GaussianSampler s(seed);
double n = s.gaussian();          // N(0, 1)
double x = s.gaussian(mean, sd);  // N(mean, sd)
s.seed(seed);                     // restart the identical sequence
```

### SensorBase

**Header:** `inc/SensorBase.hpp`

Shared base: holds the sampler, exposes `kind()` / `name()` and `reseed(seed)`
for reproducible replay, and tags each sensor with a `SensorKind`. Concrete
sensors add their own `measure()`.

### GPS

**Header:** `inc/GPS.hpp`

Civil GNSS receiver. `measure(lat, lon, alt, Vn, Ve, Vd) -> GPSMeasurement`
(geodetic position + NED velocity). Constant bias + zero-mean noise per axis;
representative civil magnitudes (sigma ~ 3 m horizontal, ~ 5 m vertical, ~ 0.1
m/s velocity). Position error is generated in meters and converted to degrees
with a local-flat approximation (scaled by `cos(lat)` for longitude).

### Pitot

**Header:** `inc/Pitot.hpp`

Pitot-static indicated airspeed. `indicatedAirspeed(V_true, rho) -> IAS`. Measures
dynamic pressure `q = 0.5*rho*V^2` with multiplicative noise + bias, then derives
IAS assuming sea-level density: `IAS = sqrt(2*q/rho_SL)`. IAS reads below true
airspeed at altitude -- the airspeed a pressure-based controller naturally sees.

### RadarAltimeter

**Header:** `inc/RadarAltimeter.hpp`

Radar altimeter. `measureAGL(agl_true) -> RadarAltimeterMeasurement{agl_m, valid}`.
Multiplicative noise (~1% of true AGL) + bias, floored at the ground; beyond the
range limit (~760 m civil) the measurement is flagged `valid == false` rather
than overloading the altitude with a sentinel.

### BoxClearanceLidar

**Header:** `inc/BoxClearanceLidar.hpp`

A six-beam lidar against an axis-aligned box centered at the origin, with two
measurement modes over the same closed-form geometry (no ray-march, no mesh):

- `measure(sx, sy, sz, box)` -- a point sensor ranging along the WORLD axes:
  `clr_pos_axis = half_axis - sensor_axis`, `clr_neg_axis = half_axis +
  sensor_axis`, clamped non-negative. Yaw-independent.
- `measureMounted(sx, sy, sz, yaw, mount_radius, box)` -- six pods mounted at
  `mount_radius` from the body center, each ranging outward along its own BODY
  axis (the X/Y pairs yaw with the body; Z stays vertical). Each reading is the
  slab ray-to-wall distance minus the mount offset -- "pod tip to wall",
  reaching 0 at contact. `rayToWall(p, d, box)` exposes the slab primitive for
  arbitrary interior rays.

Optional per-beam Gaussian range noise (default ideal). Reusable for any
body-in-a-box proximity scenario; the box geometry is a `measure()` argument.

## Determinism

Noise is reproducible by construction: every sensor owns a seeded
`GaussianSampler`, `reseed(seed)` restarts its stream, and the Box-Muller-over-
`mt19937` path is platform-independent (unlike `std::normal_distribution`, which
is implementation-defined). This is what makes deterministic replay,
cross-platform Monte Carlo, and assurance runs possible.

## Integration

The models are pure and framework-free. A scheduled component reads truth from
the dynamics/environment state, calls `measure(...)`, and publishes the flat
measurement struct; a hardware-emulation layer (out of scope here) adds the wire
protocol and cadence when real-device fidelity is needed.
