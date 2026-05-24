/**
 * @file GravityUtilities_pTest.cpp
 * @brief Performance tests for utility gravity models (J2, zonal, geoid).
 *
 * Measures:
 *  - J2GravityModel potential / acceleration (O(1))
 *  - ZonalGravityModel potential / acceleration (O(N))
 *  - GeoidModel undulation and height conversion (O(N^2))
 *  - Throughput comparison across model families
 *
 * Usage:
 *   ./SimEnvironmentGravity_PTEST --gtest_filter="J2*:Zonal*:Geoid*"
 *   ./SimEnvironmentGravity_PTEST --quick
 *   ./SimEnvironmentGravity_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/J2GravityModel.hpp"
#include "src/sim/environment/gravity/inc/earth/GeoidModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"
#include "src/sim/environment/gravity/inc/earth/ZonalGravityModel.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ub = vernier::bench;
using namespace sim::environment::gravity;

/* ----------------------------- Test Data ----------------------------- */

namespace {

/**
 * @brief Synthetic coefficient source for geoid model tests.
 */
class GeoidSynthSource final : public CoeffSource {
public:
  explicit GeoidSynthSource(std::int16_t nMax) : nMax_(nMax) {}
  std::int16_t minDegree() const noexcept override { return 0; }
  std::int16_t maxDegree() const noexcept override { return nMax_; }
  bool get(std::int16_t n, std::int16_t m, double& c, double& s) const noexcept override {
    if (n < 0 || m < 0 || m > n || n > nMax_) {
      c = 0.0;
      s = 0.0;
      return false;
    }
    if (n == 0 && m == 0) {
      c = 1.0;
      s = 0.0;
      return true;
    }
    if (n == 2 && m == 0) {
      c = egm2008::C20;
      s = 0.0;
      return true;
    }
    const double K = 1e-6;
    const double DENOM = static_cast<double>((n + 1) * (n + 1));
    c = K * std::sin(0.30 * n + 0.70 * m) / DENOM;
    s = K * std::cos(0.50 * n + 0.20 * m) / DENOM;
    return true;
  }

private:
  std::int16_t nMax_;
};

/**
 * @brief Generate sample ECEF positions distributed across latitude bands.
 */
std::vector<std::array<double, 3>> sampleEcef(double radiusMeters, int samples) {
  std::vector<std::array<double, 3>> pts;
  pts.reserve(static_cast<std::size_t>(samples));

  const double DEG = M_PI / 180.0;
  const double LATS[] = {-60.0, -30.0, 0.0, 30.0, 60.0};

  std::vector<std::array<double, 3>> base;
  for (double latDeg : LATS) {
    const double PHI = latDeg * DEG;
    const double CPHI = std::cos(PHI);
    const double SPHI = std::sin(PHI);
    for (int k = 0; k < 6; ++k) {
      const double LAM = (k * 60.0) * DEG;
      const double CLAM = std::cos(LAM);
      const double SLAM = std::sin(LAM);
      const double X = radiusMeters * CPHI * CLAM;
      const double Y = radiusMeters * CPHI * SLAM;
      const double Z = radiusMeters * SPHI;
      base.push_back({X, Y, Z});
    }
  }

  for (int i = 0; i < samples; ++i) {
    pts.push_back(base[static_cast<std::size_t>(i % base.size())]);
  }
  return pts;
}

/**
 * @brief Generate sample geodetic positions (lat, lon pairs in radians).
 */
std::vector<std::array<double, 2>> sampleGeodetic(int samples) {
  std::vector<std::array<double, 2>> pts;
  pts.reserve(static_cast<std::size_t>(samples));

  const double DEG = M_PI / 180.0;
  const double LATS[] = {-60.0, -30.0, 0.0, 30.0, 60.0};
  const double LONS[] = {-150.0, -90.0, -30.0, 30.0, 90.0, 150.0};

  for (double latDeg : LATS) {
    for (double lonDeg : LONS) {
      pts.push_back({latDeg * DEG, lonDeg * DEG});
    }
  }

  while (pts.size() < static_cast<std::size_t>(samples)) {
    pts.push_back(pts[pts.size() % 30]);
  }
  pts.resize(static_cast<std::size_t>(samples));
  return pts;
}

constexpr double RADIUS = 7000e3; // 7000 km altitude
constexpr int SAMPLES = 30;

} // namespace

/* ----------------------------- J2 Model Benchmarks ----------------------------- */

/**
 * @brief J2 potential computation (O(1)).
 */
PERF_TEST(J2Potential, Throughput) {
  UB_PERF_GUARD(perf);

  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));

  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : ECEF) {
        double pot = 0.0;
        (void)model.potential(p.data(), pot);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : ECEF) {
          double pot = 0.0;
          (void)model.potential(p.data(), pot);
          sink += pot;
        }
      },
      "j2_potential");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[J2] Potential: %.0f calls/s (%.2f ns/call)\n", callsPerSec,
              result.stats.median * 1e6 / SAMPLES);
}

/**
 * @brief J2 acceleration computation (O(1)).
 */
PERF_TEST(J2Acceleration, Throughput) {
  UB_PERF_GUARD(perf);

  J2GravityModel model;
  J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
  ASSERT_TRUE(model.init(params));

  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : ECEF) {
        double acc[3] = {};
        (void)model.acceleration(p.data(), acc);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : ECEF) {
          double acc[3] = {};
          (void)model.acceleration(p.data(), acc);
          sink += acc[0];
        }
      },
      "j2_acceleration");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[J2] Acceleration: %.0f calls/s (%.2f ns/call)\n", callsPerSec,
              result.stats.median * 1e6 / SAMPLES);
}

/* ----------------------------- Zonal Model Benchmarks ----------------------------- */

/**
 * @brief Zonal potential at N=6 (default, O(N)).
 */
PERF_TEST(ZonalPotential, N6) {
  UB_PERF_GUARD(perf);

  ZonalGravityModel model;
  ZonalParams params;
  params.N = 6;
  ASSERT_TRUE(model.init(params));

  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : ECEF) {
        double pot = 0.0;
        (void)model.potential(p.data(), pot);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : ECEF) {
          double pot = 0.0;
          (void)model.potential(p.data(), pot);
          sink += pot;
        }
      },
      "zonal_potential_n6");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[Zonal N=6] Potential: %.0f calls/s (%.2f ns/call)\n", callsPerSec,
              result.stats.median * 1e6 / SAMPLES);
}

/**
 * @brief Zonal potential at N=20 (max builtin, O(N)).
 */
PERF_TEST(ZonalPotential, N20) {
  UB_PERF_GUARD(perf);

  ZonalGravityModel model;
  ZonalParams params;
  params.N = 20;
  ASSERT_TRUE(model.init(params));

  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : ECEF) {
        double pot = 0.0;
        (void)model.potential(p.data(), pot);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : ECEF) {
          double pot = 0.0;
          (void)model.potential(p.data(), pot);
          sink += pot;
        }
      },
      "zonal_potential_n20");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[Zonal N=20] Potential: %.0f calls/s (%.2f ns/call)\n", callsPerSec,
              result.stats.median * 1e6 / SAMPLES);
}

/**
 * @brief Zonal acceleration at N=6 (uses numeric gradient).
 */
PERF_TEST(ZonalAcceleration, N6) {
  UB_PERF_GUARD(perf);

  ZonalGravityModel model;
  ZonalParams params;
  params.N = 6;
  ASSERT_TRUE(model.init(params));

  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : ECEF) {
        double acc[3] = {};
        (void)model.acceleration(p.data(), acc);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : ECEF) {
          double acc[3] = {};
          (void)model.acceleration(p.data(), acc);
          sink += acc[0];
        }
      },
      "zonal_acceleration_n6");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[Zonal N=6] Acceleration: %.0f calls/s (%.2f ns/call)\n", callsPerSec,
              result.stats.median * 1e6 / SAMPLES);
}

/* ----------------------------- Geoid Model Benchmarks ----------------------------- */

/**
 * @brief Geoid undulation at N=20 (O(N^2)).
 */
PERF_TEST(GeoidUndulation, N20) {
  UB_PERF_GUARD(perf);

  GeoidSynthSource src(20);
  GeoidModel model;
  GeoidParams params;
  params.N = 20;
  ASSERT_TRUE(model.init(src, params));

  const auto GEO = sampleGeodetic(SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : GEO) {
        (void)model.undulation(p[0], p[1]);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : GEO) {
          sink += model.undulation(p[0], p[1]);
        }
      },
      "geoid_undulation_n20");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[Geoid N=20] Undulation: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Height conversion (ellipsoid to orthometric) at N=20.
 */
PERF_TEST(GeoidHeightConversion, N20) {
  UB_PERF_GUARD(perf);

  GeoidSynthSource src(20);
  GeoidModel model;
  GeoidParams params;
  params.N = 20;
  ASSERT_TRUE(model.init(src, params));

  const auto GEO = sampleGeodetic(SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : GEO) {
        (void)model.ellipsoidToOrthometric(p[0], p[1], 100.0);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : GEO) {
          sink += model.ellipsoidToOrthometric(p[0], p[1], 100.0);
        }
      },
      "geoid_height_conversion_n20");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[Geoid N=20] Height Conversion: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/* ----------------------------- Model Comparison ----------------------------- */

/**
 * @brief Compare throughput across model types.
 */
PERF_TEST(ModelComparison, AccelerationThroughput) {
  UB_PERF_GUARD(perf);

  const auto ECEF = sampleEcef(RADIUS, 10);

  std::printf("\n=== Acceleration Throughput Comparison ===\n");
  std::printf("%-20s %15s %15s\n", "Model", "calls/s", "ns/call");
  std::printf("%s\n", std::string(50, '-').c_str());

  {
    J2GravityModel model;
    J2Params params{wgs84::GM, wgs84::A, egm2008::J2};
    ASSERT_TRUE(model.init(params));

    volatile double sink = 0.0;
    auto result = perf.throughputLoop(
        [&] {
          for (const auto& p : ECEF) {
            double acc[3] = {};
            (void)model.acceleration(p.data(), acc);
            sink += acc[0];
          }
        },
        "j2");

    double callsPerSec = result.callsPerSecond * 10;
    std::printf("%-20s %15.0f %15.1f\n", "J2 (O(1))", callsPerSec, 1e9 / callsPerSec);
  }

  {
    ZonalGravityModel model;
    ZonalParams params;
    params.N = 6;
    ASSERT_TRUE(model.init(params));

    volatile double sink = 0.0;
    auto result = perf.throughputLoop(
        [&] {
          for (const auto& p : ECEF) {
            double acc[3] = {};
            (void)model.acceleration(p.data(), acc);
            sink += acc[0];
          }
        },
        "zonal_n6");

    double callsPerSec = result.callsPerSecond * 10;
    std::printf("%-20s %15.0f %15.1f\n", "Zonal N=6 (O(N))", callsPerSec, 1e9 / callsPerSec);
  }

  {
    ZonalGravityModel model;
    ZonalParams params;
    params.N = 20;
    ASSERT_TRUE(model.init(params));

    volatile double sink = 0.0;
    auto result = perf.throughputLoop(
        [&] {
          for (const auto& p : ECEF) {
            double acc[3] = {};
            (void)model.acceleration(p.data(), acc);
            sink += acc[0];
          }
        },
        "zonal_n20");

    double callsPerSec = result.callsPerSecond * 10;
    std::printf("%-20s %15.0f %15.1f\n", "Zonal N=20 (O(N))", callsPerSec, 1e9 / callsPerSec);
  }
}
