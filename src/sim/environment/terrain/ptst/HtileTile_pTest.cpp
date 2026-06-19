/**
 * @file HtileTile_pTest.cpp
 * @brief Performance tests for the `.htile` terrain consumer.
 *
 * Measures:
 *  - Load time (HtileReader header + full body read) for a representative
 *    tile, across two tile sizes.
 *  - elevationAt query throughput over a sweep of in-coverage geodetic
 *    positions on a loaded tile.
 *
 * A synthetic tile is written to a temp file once per test and removed on
 * teardown; the data is deterministic so runs are comparable.
 *
 * Usage:
 *   ./SimEnvironmentTerrain_PTEST --gtest_filter="Htile*"
 *   ./SimEnvironmentTerrain_PTEST --quick --gtest_filter="Htile*"
 *   ./SimEnvironmentTerrain_PTEST --profile perf --gtest_filter="*Query*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ub = vernier::bench;
using sim::environment::terrain::HtileHeader;
using sim::environment::terrain::htileHeaderInit;
using sim::environment::terrain::HtileTile;
using sim::environment::terrain::HtileWriter;
using sim::environment::terrain::isSuccess;
using sim::environment::terrain::Status;

namespace {

constexpr double K_DEG_PER_RAD = 57.295779513082320876798154814105;
constexpr int SAMPLES = 32; ///< Query positions per throughput workload.

/// Write a deterministic dim x dim synthetic int16 tile to `path` covering
/// the equatorial [-1, 1] x [-1, 1] degree box.
std::string writeSyntheticTile(const char* hint, std::uint32_t dim) {
  static int counter = 0;
  ++counter;
  std::string path = "/tmp/apex_terrain_pTest_" + std::string(hint) + "_" +
                     std::to_string(::getpid()) + "_" + std::to_string(counter) + ".htile";

  HtileHeader hdr{};
  htileHeaderInit(hdr);
  std::strncpy(hdr.body, "bench", sizeof(hdr.body) - 1);
  std::strncpy(hdr.ref_surface, "sphere", sizeof(hdr.ref_surface) - 1);
  hdr.ref_radius_m = 6.0e6;
  hdr.lat_min_deg = -1.0;
  hdr.lat_max_deg = 1.0;
  hdr.lon_min_deg = -1.0;
  hdr.lon_max_deg = 1.0;
  hdr.dim_lat = dim;
  hdr.dim_lon = dim;

  std::vector<std::int16_t> samples(static_cast<std::size_t>(dim) * dim);
  for (std::uint32_t r = 0; r < dim; ++r) {
    for (std::uint32_t c = 0; c < dim; ++c) {
      // Smooth ramp so bilinear lookups touch distinct values.
      samples[static_cast<std::size_t>(r) * dim + c] = static_cast<std::int16_t>((r + c) % 1000);
    }
  }
  HtileWriter w;
  if (!w.open(path.c_str(), hdr)) {
    return std::string{};
  }
  if (!w.writeAllSamples(samples.data(), samples.size() * sizeof(std::int16_t))) {
    return std::string{};
  }
  return path;
}

/// Build a set of in-coverage query positions (radians) for the synthetic
/// tile's [-1, 1] degree box.
std::vector<std::array<double, 2>> sampleQueries(int samples) {
  std::vector<std::array<double, 2>> pts;
  pts.reserve(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    // Spread across the interior, avoiding the exact edges.
    const double FRAC = (static_cast<double>(i) + 0.5) / samples; // (0, 1)
    const double LAT_DEG = -0.9 + 1.8 * FRAC;
    const double LON_DEG = -0.9 + 1.8 * (1.0 - FRAC);
    pts.push_back({LAT_DEG / K_DEG_PER_RAD, LON_DEG / K_DEG_PER_RAD});
  }
  return pts;
}

} // namespace

/* ----------------------------- Load Time ----------------------------- */

// Load = open + header validate + full body read + buffer allocation. Driven
// through the harness (re-loading the same temp tile each cycle) so it emits
// CSV and honors --profile/--repeats/--csv like every other ptest. One case
// per tile size gives a size-scaling read, mirroring gravity's per-degree
// ptest cases.
PERF_TEST(HtileLoad, Dim256) {
  UB_PERF_GUARD(perf);

  const std::string PATH = writeSyntheticTile("load256", 256);
  ASSERT_FALSE(PATH.empty());
  perf.warmup([&] {
    HtileTile t;
    (void)t.load(PATH);
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        HtileTile t;
        (void)t.load(PATH);
        sink += t.resolutionMeters();
      },
      "load_256");
  (void)sink;
  // Per-load time derived from callsPerSecond (lambdas/sec; one load per lambda)
  // to stay independent of the stats field's time unit.
  std::printf("\n[dim=256] load: %.0f loads/s (%.2f us/load)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

PERF_TEST(HtileLoad, Dim1024) {
  UB_PERF_GUARD(perf);

  const std::string PATH = writeSyntheticTile("load1024", 1024);
  ASSERT_FALSE(PATH.empty());
  perf.warmup([&] {
    HtileTile t;
    (void)t.load(PATH);
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        HtileTile t;
        (void)t.load(PATH);
        sink += t.resolutionMeters();
      },
      "load_1024");
  (void)sink;
  std::printf("\n[dim=1024] load: %.0f loads/s (%.2f us/load)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

/* ----------------------------- Query Throughput ----------------------------- */

PERF_TEST(HtileQuery, ElevationThroughput) {
  UB_PERF_GUARD(perf);

  const std::string PATH = writeSyntheticTile("query", 1024);
  ASSERT_FALSE(PATH.empty());
  HtileTile t;
  ASSERT_TRUE(isSuccess(t.load(PATH)));

  const auto QUERIES = sampleQueries(SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& q : QUERIES) {
        double H = 0.0;
        (void)t.elevationAt(q[0], q[1], H);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& q : QUERIES) {
          double H = 0.0;
          (void)t.elevationAt(q[0], q[1], H);
          sink += H;
        }
      },
      "elevationAt_1024");

  // calls/s and per-call time derived from callsPerSecond (lambdas/sec) and the
  // SAMPLES queries per lambda -- unit-independent of the stats field.
  const double CALLS_PER_SEC = result.callsPerSecond * SAMPLES;
  std::printf("\n[dim=1024] elevationAt: %.0f calls/s (%.4f us/call)\n", CALLS_PER_SEC,
              1.0e6 / CALLS_PER_SEC);

  std::error_code ec;
  std::filesystem::remove(PATH, ec);
}

PERF_MAIN()
