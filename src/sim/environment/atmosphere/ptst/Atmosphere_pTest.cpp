/**
 * @file Atmosphere_pTest.cpp
 * @brief Performance tests for the atmosphere models.
 *
 * Measures:
 *  - Layered (USSA76) query throughput over a sweep of in-range altitudes
 *    that crosses every layer (binary-search + hydrostatic pow/exp path).
 *  - Exponential analytic query throughput (single exp per call).
 *  - Load time (AtmReader header + body read + layer-table build) for a
 *    representative layered `.atm` file.
 *
 * A synthetic USSA76 `.atm` is written to a temp file once per load test and
 * removed on teardown; the data is deterministic so runs are comparable.
 *
 * Usage:
 *   ./SimEnvironmentAtmosphere_PTEST --gtest_filter="Atm*"
 *   ./SimEnvironmentAtmosphere_PTEST --quick --gtest_filter="*Query*"
 *   ./SimEnvironmentAtmosphere_PTEST --profile perf --gtest_filter="*Layered*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/atmosphere/inc/Atm.hpp"
#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/ExponentialAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/LayeredAtmosphere.hpp"
#include "src/sim/environment/atmosphere/inc/earth/Ussa76AtmosphereModel.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ub = vernier::bench;
using sim::environment::atmosphere::AtmHeader;
using sim::environment::atmosphere::atmHeaderInit;
using sim::environment::atmosphere::atmMakeLayer;
using sim::environment::atmosphere::AtmModelType;
using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::AtmRecord;
using sim::environment::atmosphere::AtmWriter;
using sim::environment::atmosphere::ExponentialAtmosphere;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::LayeredAtmosphere;
using sim::environment::atmosphere::earth::Ussa76AtmosphereModel;

namespace {

constexpr int SAMPLES = 32; ///< Query altitudes per throughput workload.

/// Build a set of in-range query altitudes (meters) sweeping 0..76 km so the
/// binary search and every layer's pressure path are exercised.
std::array<double, SAMPLES> sampleAltitudes() {
  std::array<double, SAMPLES> alts{};
  for (int i = 0; i < SAMPLES; ++i) {
    const double FRAC = (static_cast<double>(i) + 0.5) / SAMPLES; // (0, 1)
    alts[static_cast<std::size_t>(i)] = FRAC * 76000.0;
  }
  return alts;
}

/// Write a deterministic USSA76 layered `.atm` to `path`. Returns "" on error.
std::string writeUssa76File(const char* hint) {
  static int counter = 0;
  ++counter;
  std::string path = "/tmp/apex_atmosphere_pTest_" + std::string(hint) + "_" +
                     std::to_string(::getpid()) + "_" + std::to_string(counter) + ".atm";

  AtmHeader hdr{};
  atmHeaderInit(hdr);
  std::strncpy(hdr.body, "earth", sizeof(hdr.body) - 1);
  hdr.model_type = static_cast<std::uint8_t>(AtmModelType::kLayered);

  const std::vector<AtmRecord> recs = {
      atmMakeLayer(0.0, 288.15, 101325.0, -0.0065),
      atmMakeLayer(11000.0, 216.65, 22632.06, 0.0),
      atmMakeLayer(20000.0, 216.65, 5474.889, 0.001),
      atmMakeLayer(32000.0, 228.65, 868.0187, 0.0028),
      atmMakeLayer(47000.0, 270.65, 110.9063, 0.0),
      atmMakeLayer(51000.0, 270.65, 66.93887, -0.0028),
      atmMakeLayer(71000.0, 214.65, 3.95642, -0.002),
  };
  hdr.n_records = static_cast<std::uint16_t>(recs.size());

  AtmWriter w;
  if (!w.open(path.c_str(), hdr)) {
    return std::string{};
  }
  if (!w.writeAllRecords(recs.data(), recs.size())) {
    return std::string{};
  }
  return path;
}

} // namespace

/* ----------------------------- Query Throughput ----------------------------- */

PERF_TEST(AtmLayeredQuery, Ussa76Throughput) {
  UB_PERF_GUARD(perf);

  Ussa76AtmosphereModel atm;
  ASSERT_TRUE(atm.isLoaded());
  const auto ALTS = sampleAltitudes();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const double h : ALTS) {
        AtmosphereState s;
        (void)atm.query(h, 0.0, 0.0, s);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const double h : ALTS) {
          AtmosphereState s;
          (void)atm.query(h, 0.0, 0.0, s);
          sink += s.rho;
        }
      },
      "ussa76_query");

  // calls/s and per-call time derived from callsPerSecond (lambdas/sec) and the
  // SAMPLES queries per lambda -- unit-independent of the stats field.
  const double CALLS_PER_SEC = result.callsPerSecond * SAMPLES;
  std::printf("\n[USSA76] query: %.0f calls/s (%.4f us/call)\n", CALLS_PER_SEC,
              1.0e6 / CALLS_PER_SEC);
}

PERF_TEST(AtmExponentialQuery, EarthIsaThroughput) {
  UB_PERF_GUARD(perf);

  ExponentialAtmosphere atm; // Earth ISA defaults
  ASSERT_TRUE(atm.isInitialized());
  const auto ALTS = sampleAltitudes();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const double h : ALTS) {
        AtmosphereState s;
        (void)atm.query(h, 0.0, 0.0, s);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const double h : ALTS) {
          AtmosphereState s;
          (void)atm.query(h, 0.0, 0.0, s);
          sink += s.rho;
        }
      },
      "exponential_query");

  const double CALLS_PER_SEC = result.callsPerSecond * SAMPLES;
  std::printf("\n[exponential] query: %.0f calls/s (%.4f us/call)\n", CALLS_PER_SEC,
              1.0e6 / CALLS_PER_SEC);
}

/* ----------------------------- Load Time ----------------------------- */

// Load = open + header validate + full body read + layer-table build. Driven
// through the harness (re-loading the same temp file each cycle) so it emits
// CSV and honors --profile/--repeats/--csv like every other ptest.
PERF_TEST(AtmLoad, Ussa76Layered) {
  UB_PERF_GUARD(perf);

  const std::string PATH = writeUssa76File("load");
  ASSERT_FALSE(PATH.empty());

  perf.warmup([&] {
    LayeredAtmosphere t;
    (void)t.load(PATH);
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        LayeredAtmosphere t;
        (void)t.load(PATH);
        sink += static_cast<double>(t.numLayers());
      },
      "load_ussa76");
  (void)sink;
  // Per-load time derived from callsPerSecond (one load per lambda) to stay
  // independent of the stats field's time unit.
  std::printf("\n[USSA76] load: %.0f loads/s (%.2f us/load)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

PERF_MAIN()
