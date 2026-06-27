/**
 * @file Sensors_pTest.cpp
 * @brief Throughput benchmarks for the sensor measurement models.
 *
 * Measures the per-sample cost of the GaussianSampler draw and each sensor's
 * measurement path (the per-tick work a vehicle pays to sample a sensor).
 */

#include <cstdio>

#include "src/bench/inc/Perf.hpp"
#include "src/sim/sensors/inc/GPS.hpp"
#include "src/sim/sensors/inc/GaussianSampler.hpp"
#include "src/sim/sensors/inc/Pitot.hpp"
#include "src/sim/sensors/inc/RadarAltimeter.hpp"

/* ----------------------------- GaussianSampler ----------------------------- */

PERF_TEST(GaussianSamplerDraw, Throughput) {
  UB_PERF_GUARD(perf);
  sim::sensors::GaussianSampler s(12345u);
  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += s.gaussian(); }, "gaussian_draw");
  std::printf("\n[GaussianSampler] draw: %.0f draws/s (%.4f us/draw)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- GPS ----------------------------- */

PERF_TEST(GPSMeasure, Throughput) {
  UB_PERF_GUARD(perf);
  sim::sensors::GPS g;
  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto m = g.measure(37.5, -122.3, 1000.0, 50.0, -10.0, 2.0);
        sink += m.lat_deg + m.alt_m + m.V_north_m_s;
      },
      "gps_measure");
  std::printf("\n[GPS] measure: %.0f samples/s (%.4f us/sample)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- Pitot ----------------------------- */

PERF_TEST(PitotMeasure, Throughput) {
  UB_PERF_GUARD(perf);
  sim::sensors::Pitot p;
  volatile double sink = 0.0;
  auto result =
      perf.throughputLoop([&] { sink += p.indicatedAirspeed(120.0, 0.4135); }, "pitot_measure");
  std::printf("\n[Pitot] measure: %.0f samples/s (%.4f us/sample)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- RadarAltimeter ----------------------------- */

PERF_TEST(RadarAltimeterMeasure, Throughput) {
  UB_PERF_GUARD(perf);
  sim::sensors::RadarAltimeter ra;
  volatile double sink = 0.0;
  auto result =
      perf.throughputLoop([&] { sink += ra.measureAGL(150.0).agl_m; }, "radar_altimeter_measure");
  std::printf("\n[RadarAltimeter] measure: %.0f samples/s (%.4f us/sample)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
