/**
 * @file GrailModel_pTest.cpp
 * @brief Performance tests for the GRAIL lunar gravity model.
 *
 * Measures:
 *  - Potential throughput at N=60, 180 (O(N^2) order scaling)
 *  - Numeric vs analytic acceleration throughput at N=60
 *  - Initialization overhead across model orders
 *
 * Usage:
 *   ./SimEnvironmentGravity_PTEST --gtest_filter="Grail*"
 *   ./SimEnvironmentGravity_PTEST --quick --gtest_filter="Grail*"
 *   ./SimEnvironmentGravity_PTEST --profile perf --gtest_filter="*GrailPotential*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/moon/GrailModel.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"

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
using sim::environment::gravity::GrailModel;
using sim::environment::gravity::GrailParams;
using sim::environment::gravity::lunar::GM;
using sim::environment::gravity::lunar::R_REF;

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
 * @brief Generate sample MCMF (Moon-Centered Moon-Fixed) positions.
 */
std::vector<std::array<double, 3>> sampleMcmf(double radiusMeters, int samples) {
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

// Lunar constants
constexpr double LUNAR_GM = GM;   // m^3/s^2
constexpr double LUNAR_A = R_REF; // m (reference radius)
constexpr double RADIUS = 2000e3; // 2000 km altitude above surface
constexpr int SAMPLES = 30;       // MCMF points per workload

} // namespace

/* ----------------------------- Potential Throughput ----------------------------- */

/**
 * @brief Potential computation at N=60 (moderate fidelity).
 */
PERF_TEST(GrailPotential, N60) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(60);
  GrailModel model;
  GrailParams params{LUNAR_GM, LUNAR_A, 60};
  ASSERT_TRUE(model.init(src, params));

  const auto MCMF = sampleMcmf(LUNAR_A + RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : MCMF) {
        double pot = 0.0;
        (void)model.potential(p.data(), pot);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : MCMF) {
          double pot = 0.0;
          (void)model.potential(p.data(), pot);
          sink += pot;
        }
      },
      "potential_n60");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=60] Potential: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Potential computation at N=180 (high fidelity).
 */
PERF_TEST(GrailPotential, N180) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(180);
  GrailModel model;
  GrailParams params{LUNAR_GM, LUNAR_A, 180};
  ASSERT_TRUE(model.init(src, params));

  const auto MCMF = sampleMcmf(LUNAR_A + RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : MCMF) {
        double pot = 0.0;
        (void)model.potential(p.data(), pot);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : MCMF) {
          double pot = 0.0;
          (void)model.potential(p.data(), pot);
          sink += pot;
        }
      },
      "potential_n180");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=180] Potential: %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/* ----------------------------- Acceleration Throughput ----------------------------- */

/**
 * @brief Numeric acceleration at N=60.
 */
PERF_TEST(GrailAcceleration, NumericN60) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(60);
  GrailModel model;
  GrailParams params{LUNAR_GM, LUNAR_A, 60};
  ASSERT_TRUE(model.init(src, params));

  const auto MCMF = sampleMcmf(LUNAR_A + RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : MCMF) {
        double acc[3] = {};
        (void)model.acceleration(p.data(), acc);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : MCMF) {
          double acc[3] = {};
          (void)model.acceleration(p.data(), acc);
          sink += std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        }
      },
      "accel_numeric_n60");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=60] Acceleration (numeric): %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/**
 * @brief Analytic acceleration at N=60.
 */
PERF_TEST(GrailAcceleration, AnalyticN60) {
  UB_PERF_GUARD(perf);

  DenseSynthSource src(60);
  GrailModel model;
  GrailParams params{LUNAR_GM, LUNAR_A, 60};
  ASSERT_TRUE(model.init(src, params));
  model.setAccelMode(GrailModel::AccelMode::Analytic);

  const auto MCMF = sampleMcmf(LUNAR_A + RADIUS, SAMPLES);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (const auto& p : MCMF) {
        double acc[3] = {};
        (void)model.acceleration(p.data(), acc);
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (const auto& p : MCMF) {
          double acc[3] = {};
          (void)model.acceleration(p.data(), acc);
          sink += std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        }
      },
      "accel_analytic_n60");

  double callsPerSec = result.callsPerSecond * SAMPLES;
  std::printf("\n[N=60] Acceleration (analytic): %.0f calls/s (%.2f us/call)\n", callsPerSec,
              result.stats.median * 1000 / SAMPLES);
}

/* ----------------------------- Initialization ----------------------------- */

/**
 * @brief Model initialization overhead across orders.
 */
PERF_TEST(GrailInit, OrderScaling) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== GRAIL Initialization Time vs Order ===\n");
  std::printf("%-6s %12s\n", "Order", "us/init");
  std::printf("%s\n", std::string(20, '-').c_str());

  for (std::int16_t n : {static_cast<std::int16_t>(60), static_cast<std::int16_t>(180)}) {
    DenseSynthSource src(n);
    GrailParams params{LUNAR_GM, LUNAR_A, n};

    const int REPS = 10;
    std::vector<double> times;
    times.reserve(REPS);

    for (int r = 0; r < REPS; ++r) {
      GrailModel m;
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
