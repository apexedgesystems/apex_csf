/**
 * @file Egm2008Model_pTest.cpp
 * @brief Performance tests for the EGM2008 Earth gravity model.
 *
 * Measures:
 *  - Potential throughput at N=10, 50, 100 (O(N^2) order scaling)
 *  - Numeric vs analytic acceleration throughput
 *  - Initialization overhead across model orders
 *
 * Usage:
 *   ./SimEnvironmentGravity_PTEST --csv results.csv
 *   ./SimEnvironmentGravity_PTEST --quick
 *   ./SimEnvironmentGravity_PTEST --profile perf --gtest_filter="*N180*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ub = vernier::bench;
using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008Params;

/* ----------------------------- Test Data ----------------------------- */

namespace {

/**
 * @brief Deterministic, fully-normalized coefficient source up to nMax.
 *
 * Generates synthetic but physically plausible coefficients for benchmarking.
 * C00 = 1.0 (normalized), higher orders decay as 1/(n+1)^2.
 */
class DenseSynthSource final : public CoeffSource {
public:
  explicit DenseSynthSource(std::int16_t nMax) : nMax_(nMax) {}
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

// WGS84 constants
constexpr double GM = 3.986004418e14; // m^3/s^2
constexpr double A = 6378137.0;       // m (semi-major axis)
constexpr double RADIUS = 7000e3;     // 7000 km altitude for benchmarks
constexpr int SAMPLES = 30;           // ECEF points per workload

} // namespace

/* ----------------------------- Potential Throughput ----------------------------- */

/**
 * @brief Potential computation at N=10 (trivial baseline).
 */
PERF_TEST(Egm2008Potential, N10) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(10);
  Egm2008Model model;
  Egm2008Params params{GM, A, 10};
  ASSERT_TRUE(model.init(src, params));

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
      "potential_n10");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=10] Potential: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Potential computation at N=50.
 */
PERF_TEST(Egm2008Potential, N50) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(50);
  Egm2008Model model;
  Egm2008Params params{GM, A, 50};
  ASSERT_TRUE(model.init(src, params));

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
      "potential_n50");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=50] Potential: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Potential computation at N=100.
 */
PERF_TEST(Egm2008Potential, N100) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(100);
  Egm2008Model model;
  Egm2008Params params{GM, A, 100};
  ASSERT_TRUE(model.init(src, params));

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
      "potential_n100");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=100] Potential: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/* ----------------------------- Acceleration Throughput ----------------------------- */

/**
 * @brief Numeric acceleration at N=50.
 */
PERF_TEST(Egm2008Acceleration, NumericN50) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(50);
  Egm2008Model model;
  Egm2008Params params{GM, A, 50};
  ASSERT_TRUE(model.init(src, params));

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
          sink += std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        }
      },
      "accel_numeric_n50");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=50] Acceleration (numeric): %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Analytic acceleration at N=50.
 */
PERF_TEST(Egm2008Acceleration, AnalyticN50) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(50);
  Egm2008Model model;
  Egm2008Params params{GM, A, 50};
  ASSERT_TRUE(model.init(src, params));
  model.setAccelMode(Egm2008Model::AccelMode::Analytic);

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
          sink += std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        }
      },
      "accel_analytic_n50");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=50] Acceleration (analytic): %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/* ----------------------------- Numeric vs Analytic Comparison ----------------------------- */

/**
 * @brief Compare numeric vs analytic acceleration speedup at N=50.
 */
PERF_TEST(Egm2008Comparison, NumericVsAnalytic) {
  UB_PERF_GUARD(perf);

  const std::int16_t N = 50;
  DenseSynthSource src(N);
  const auto ECEF = sampleEcef(RADIUS, SAMPLES);

  std::printf("\n=== Numeric vs Analytic Acceleration (N=%d) ===\n", N);

  double numericCallsPerSec = 0.0;
  double analyticCallsPerSec = 0.0;

  {
    Egm2008Model model;
    Egm2008Params params{GM, A, N};
    ASSERT_TRUE(model.init(src, params));

    volatile double sink = 0.0;
    auto result = perf.throughputLoop(
        [&] {
          for (const auto& p : ECEF) {
            double acc[3] = {};
            (void)model.acceleration(p.data(), acc);
            sink += acc[0];
          }
        },
        "numeric");

    numericCallsPerSec = result.callsPerSecond * SAMPLES;
    std::printf("Numeric:  %.0f calls/s\n", numericCallsPerSec);
  }

  {
    Egm2008Model model;
    Egm2008Params params{GM, A, N};
    ASSERT_TRUE(model.init(src, params));
    model.setAccelMode(Egm2008Model::AccelMode::Analytic);

    volatile double sink = 0.0;
    auto result = perf.throughputLoop(
        [&] {
          for (const auto& p : ECEF) {
            double acc[3] = {};
            (void)model.acceleration(p.data(), acc);
            sink += acc[0];
          }
        },
        "analytic");

    analyticCallsPerSec = result.callsPerSecond * SAMPLES;
    std::printf("Analytic: %.0f calls/s\n", analyticCallsPerSec);
  }

  double speedup = analyticCallsPerSec / numericCallsPerSec;
  std::printf("Analytic speedup: %.2fx\n", speedup);
}

/* ----------------------------- Initialization ----------------------------- */

/**
 * @brief Model initialization overhead across orders.
 */
PERF_TEST(Egm2008Init, OrderScaling) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Initialization Time vs Order ===\n");
  std::printf("%-6s %12s\n", "Order", "us/init");
  std::printf("%s\n", std::string(20, '-').c_str());

  for (std::int16_t n : {static_cast<std::int16_t>(10), static_cast<std::int16_t>(50),
                         static_cast<std::int16_t>(100), static_cast<std::int16_t>(180)}) {
    DenseSynthSource src(n);
    Egm2008Params params{GM, A, n};

    const int REPS = 10;
    std::vector<double> times;
    times.reserve(REPS);

    for (int r = 0; r < REPS; ++r) {
      Egm2008Model m;
      auto start = std::chrono::steady_clock::now();
      (void)m.init(src, params);
      auto end = std::chrono::steady_clock::now();
      double us = std::chrono::duration<double, std::micro>(end - start).count();
      times.push_back(us);
    }

    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];
    std::printf("N=%-4d %12.1f\n", n, median);
  }
}

PERF_MAIN()
