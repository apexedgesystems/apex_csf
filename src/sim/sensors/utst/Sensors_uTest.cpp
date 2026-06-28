/**
 * @file Sensors_uTest.cpp
 * @brief Tests for the GPS, Pitot, and RadarAltimeter measurement models.
 *
 * Each sensor is checked noise-free (the measurement reduces to the physical
 * transform + bias), statistically (mean ~ bias, std ~ sigma), and at its edge
 * cases (clamps, range limits). SensorBase identity + reseed are covered too.
 */

#include "src/sim/sensors/inc/GPS.hpp"
#include "src/sim/sensors/inc/Pitot.hpp"
#include "src/sim/sensors/inc/RadarAltimeter.hpp"
#include "src/sim/sensors/inc/SensorBase.hpp"

#include <cmath>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using sim::sensors::GPS;
using sim::sensors::GPSParams;
using sim::sensors::Pitot;
using sim::sensors::PitotParams;
using sim::sensors::RadarAltimeter;
using sim::sensors::RadarAltimeterParams;
using sim::sensors::SensorBase;
using sim::sensors::SensorKind;

namespace {
constexpr double kTol = 1e-9;
} // namespace

/* ----------------------------- SensorBase ----------------------------- */

/** @test A sensor reports its kind and name; reseed gives reproducible noise. */
TEST(SensorBaseTest, IdentityAndReseed) {
  GPS g;
  EXPECT_EQ(g.kind(), SensorKind::Gnss);
  EXPECT_STREQ(g.name(), "gps");

  const auto a = g.measure(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  g.reseed(g.params().seed);
  const auto b = g.measure(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  EXPECT_EQ(a.alt_m, b.alt_m); // identical stream after reseed
}

/** @test A sensor destructs cleanly through a SensorBase pointer. */
TEST(SensorBaseTest, DestructsThroughBasePointer) {
  std::unique_ptr<SensorBase> s = std::make_unique<Pitot>();
  EXPECT_EQ(s->kind(), SensorKind::AirData);
  s.reset(); // exercises ~SensorBase through the base pointer
  EXPECT_EQ(s, nullptr);
}

/* ----------------------------- GPS ----------------------------- */

/** @test Noise-free and bias-free, the measurement equals the truth. */
TEST(GPSTest, ZeroErrorReturnsTruth) {
  GPSParams p;
  p.sigma_horizontal_m = 0.0;
  p.sigma_vertical_m = 0.0;
  p.bias_horizontal_m = 0.0;
  p.bias_vertical_m = 0.0;
  p.sigma_velocity_m_s = 0.0;
  GPS g(p);
  const auto m = g.measure(37.5, -122.3, 1000.0, 50.0, -10.0, 2.0);
  EXPECT_NEAR(m.lat_deg, 37.5, kTol);
  EXPECT_NEAR(m.lon_deg, -122.3, kTol);
  EXPECT_NEAR(m.alt_m, 1000.0, kTol);
  EXPECT_NEAR(m.V_north_m_s, 50.0, kTol);
  EXPECT_NEAR(m.V_east_m_s, -10.0, kTol);
  EXPECT_NEAR(m.V_down_m_s, 2.0, kTol);
}

/** @test With noise off, only the constant bias offsets altitude. */
TEST(GPSTest, BiasOnlyIsDeterministic) {
  GPSParams p;
  p.sigma_horizontal_m = 0.0;
  p.sigma_vertical_m = 0.0;
  p.sigma_velocity_m_s = 0.0;
  p.bias_vertical_m = 1.0;
  GPS g(p);
  const auto m = g.measure(0.0, 0.0, 500.0, 0.0, 0.0, 0.0);
  EXPECT_NEAR(m.alt_m, 501.0, kTol); // 500 + bias
}

/** @test Altitude error has mean ~ bias and std ~ sigma over many samples. */
TEST(GPSTest, AltitudeErrorStatistics) {
  GPSParams p;
  p.sigma_vertical_m = 5.0;
  p.bias_vertical_m = 1.0;
  GPS g(p);
  std::vector<double> err;
  err.reserve(100000);
  for (int i = 0; i < 100000; ++i) {
    err.push_back(g.measure(0.0, 0.0, 0.0, 0.0, 0.0, 0.0).alt_m);
  }
  double s = 0.0;
  for (double x : err) {
    s += x;
  }
  const double mean = s / static_cast<double>(err.size());
  double acc = 0.0;
  for (double x : err) {
    acc += (x - mean) * (x - mean);
  }
  const double std = std::sqrt(acc / static_cast<double>(err.size()));
  EXPECT_NEAR(mean, 1.0, 0.1); // ~ bias
  EXPECT_NEAR(std, 5.0, 0.1);  // ~ sigma
}

/** @test A degenerate earth radius guards the deg<->m division (no NaN). */
TEST(GPSTest, ZeroEarthRadiusLeavesLatLonUnchanged) {
  GPSParams p;
  p.earth_radius_m = 0.0;
  p.sigma_horizontal_m = 0.0;
  p.bias_horizontal_m = 0.0;
  GPS g(p);
  const auto m = g.measure(10.0, 20.0, 0.0, 0.0, 0.0, 0.0);
  EXPECT_NEAR(m.lat_deg, 10.0, kTol); // division guarded -> truth unchanged
  EXPECT_NEAR(m.lon_deg, 20.0, kTol);
}

/* ----------------------------- Pitot ----------------------------- */

/** @test At sea-level density, IAS equals true airspeed (noise/bias off). */
TEST(PitotTest, SeaLevelIasEqualsTrue) {
  PitotParams p;
  p.noise_q_pct = 0.0;
  p.bias_q_Pa = 0.0;
  Pitot pitot(p);
  EXPECT_NEAR(pitot.params().rho_SL_kg_m3, 1.225, kTol);
  EXPECT_NEAR(pitot.indicatedAirspeed(100.0, 1.225), 100.0, 1e-6);
}

/** @test At altitude (low density), IAS reads below true airspeed. */
TEST(PitotTest, AltitudeIasBelowTrue) {
  PitotParams p;
  p.noise_q_pct = 0.0;
  p.bias_q_Pa = 0.0;
  Pitot pitot(p);
  const double ias = pitot.indicatedAirspeed(240.0, 0.3119); // ~12 km density
  EXPECT_LT(ias, 240.0);
  EXPECT_GT(ias, 0.0);
}

/** @test Non-positive dynamic pressure clamps IAS to zero. */
TEST(PitotTest, ZeroDynamicPressureClampsToZero) {
  PitotParams p;
  p.noise_q_pct = 0.0;
  p.bias_q_Pa = 0.0;
  Pitot pitot(p);
  EXPECT_NEAR(pitot.indicatedAirspeed(0.0, 1.225), 0.0, kTol); // q = 0 -> 0
}

/** @test A zero reference density guards the IAS division. */
TEST(PitotTest, ZeroReferenceDensityClampsToZero) {
  PitotParams p;
  p.rho_SL_kg_m3 = 0.0;
  Pitot pitot(p);
  EXPECT_NEAR(pitot.indicatedAirspeed(100.0, 1.225), 0.0, kTol);
}

/* ----------------------------- RadarAltimeter ----------------------------- */

/** @test In range and noise-free, the measurement tracks true AGL and is valid. */
TEST(RadarAltimeterTest, InRangeTracksTruth) {
  RadarAltimeterParams p;
  p.noise_pct = 0.0;
  RadarAltimeter ra(p);
  EXPECT_NEAR(ra.params().max_range_m, 760.0, kTol);
  const auto m = ra.measureAGL(100.0);
  EXPECT_TRUE(m.valid);
  EXPECT_NEAR(m.agl_m, 100.0, kTol);
}

/** @test Beyond the range limit the measurement is flagged invalid. */
TEST(RadarAltimeterTest, OutOfRangeIsInvalid) {
  RadarAltimeter ra; // default max_range 760 m
  const auto m = ra.measureAGL(1000.0);
  EXPECT_FALSE(m.valid);
  EXPECT_NEAR(m.agl_m, 0.0, kTol);
}

/** @test Negative true AGL is clamped to the ground before measuring. */
TEST(RadarAltimeterTest, NegativeAglClampsToZero) {
  RadarAltimeterParams p;
  p.noise_pct = 0.0;
  RadarAltimeter ra(p);
  const auto m = ra.measureAGL(-5.0);
  EXPECT_TRUE(m.valid);
  EXPECT_NEAR(m.agl_m, 0.0, kTol); // clamped truth, no noise
}

/** @test A negative bias cannot drive the reading below the floor. */
TEST(RadarAltimeterTest, MeasurementHeldAtFloor) {
  RadarAltimeterParams p;
  p.noise_pct = 0.0;
  p.bias_m = -50.0;
  p.min_floor_m = 0.0;
  RadarAltimeter ra(p);
  const auto m = ra.measureAGL(10.0); // 10 - 50 = -40 -> floored to 0
  EXPECT_TRUE(m.valid);
  EXPECT_NEAR(m.agl_m, 0.0, kTol);
}
