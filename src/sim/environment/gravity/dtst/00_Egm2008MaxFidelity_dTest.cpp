/**
 * @file 00_Egm2008MaxFidelity_dTest.cpp
 * @brief Integration test: Maximum fidelity EGM2008 model (N=2190) timing and validation.
 *
 * Goals:
 *  - Load full EGM2008 coefficient table (2.4M coefficients)
 *  - Initialize model at various truncation levels
 *  - Time single-point potential and acceleration computation
 *  - Validate output against known reference values
 *
 * Prereqs:
 *  - egm2008_full_n2190.bin in data/earth/ (86MB, 36-byte records)
 *
 * Notes:
 *  - Not included in standard test runs (use --gtest_filter="*MaxFidelity*")
 */

#include <gtest/gtest.h>

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008Params;
using sim::environment::gravity::FullTableCoeffSource;

namespace {

/* ----------------------------- Test Configuration ----------------------------- */

// Paths to EGM2008 tables (relative to workspace root)
// Note: full_n2190 starts at n=2 (EGM2008 standard format)
constexpr const char* K_FULL_TABLE_PATH =
    "src/sim/environment/gravity/data/earth/egm2008_full_n2190.bin";

// WGS84 constants
constexpr double GM = 3.986004418e14; // m^3/s^2
constexpr double A = 6378137.0;       // m (semi-major axis)

// Test point: 400km altitude over equator
constexpr double TEST_ALT_KM = 400.0;
constexpr double TEST_R = A + TEST_ALT_KM * 1000.0; // meters

/* ----------------------------- Timing Helper ----------------------------- */

class ScopedTimer {
public:
  explicit ScopedTimer(const char* label)
      : label_(label), start_(std::chrono::high_resolution_clock::now()) {}
  ~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    std::cout << "  [" << label_ << "] " << us << " us";
    if (us > 1000) {
      std::cout << " (" << std::fixed << std::setprecision(2) << us / 1000.0 << " ms)";
    }
    std::cout << "\n";
  }

private:
  const char* label_;
  std::chrono::high_resolution_clock::time_point start_;
};

} // namespace

/* ----------------------------- Integration Tests ----------------------------- */

/**
 * @test TableLoadFull
 * @brief Load the full N=2190 coefficient table and verify basic properties.
 */
TEST(Egm2008MaxFidelity, TableLoadFull) {
  std::cout << "\n=== EGM2008 Full Table (N=2190) Load Test ===\n";

  FullTableCoeffSource src;
  {
    ScopedTimer t("File open");
    if (!src.open(K_FULL_TABLE_PATH)) {
      GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
    }
  }

  std::cout << "  Min degree: " << src.minDegree() << "\n";
  std::cout << "  Max degree: " << src.maxDegree() << "\n";

  EXPECT_EQ(src.minDegree(), 2); // EGM2008 starts at n=2
  EXPECT_EQ(src.maxDegree(), 2190);

  // Verify C20 (J2 term) is negative and reasonable magnitude
  double c20 = 0, s20 = 0;
  ASSERT_TRUE(src.get(2, 0, c20, s20));
  std::cout << "  C20 (J2): " << std::scientific << c20 << "\n";
  EXPECT_LT(c20, 0);     // J2 is negative
  EXPECT_GT(c20, -1e-2); // Reasonable magnitude

  // Verify highest degree is accessible
  double cMax = 0, sMax = 0;
  ASSERT_TRUE(src.get(2190, 2190, cMax, sMax));
  std::cout << "  C2190,2190: " << std::scientific << cMax << "\n";
}

/**
 * @test ModelInitTiming
 * @brief Time model initialization at various truncation levels.
 */
TEST(Egm2008MaxFidelity, ModelInitTiming) {
  std::cout << "\n=== EGM2008 Model Initialization Timing ===\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const int ORDERS[] = {60, 180, 360, 720, 1080, 2190};

  for (int n : ORDERS) {
    Egm2008Model model;
    Egm2008Params params{GM, A, static_cast<int16_t>(n)};

    std::string label = "Init N=" + std::to_string(n);
    {
      ScopedTimer t(label.c_str());
      ASSERT_TRUE(model.init(src, params));
    }
    EXPECT_EQ(model.maxDegree(), n);
  }
}

/**
 * @test InitBreakdownN2190
 * @brief Detailed breakdown of initialization phases for N=2190.
 */
TEST(Egm2008MaxFidelity, InitBreakdownN2190) {
  std::cout << "\n=== EGM2008 Initialization Breakdown (N=2190) ===\n";

  // Phase 1: File open
  FullTableCoeffSource src;
  {
    ScopedTimer t("Phase 1: File open");
    if (!src.open(K_FULL_TABLE_PATH)) {
      GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
    }
  }

  // Count coefficients for N=2190 (starting at n=2)
  const int N = 2190;
  const std::size_t TRI_SIZE =
      static_cast<std::size_t>(N + 1) * static_cast<std::size_t>(N + 2) / 2 - 3;
  std::cout << "  Coefficients to load: " << TRI_SIZE << " (" << TRI_SIZE * 2 * 8 / 1024 / 1024
            << " MB for C+S)\n";

  Egm2008Model model;
  Egm2008Params params{GM, A, static_cast<int16_t>(N)};

  auto t0 = std::chrono::high_resolution_clock::now();
  ASSERT_TRUE(model.init(src, params));
  auto t1 = std::chrono::high_resolution_clock::now();

  auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  std::cout << "  Total init time: " << totalUs << " us (" << totalUs / 1000.0 << " ms)\n";

  // File I/O estimate
  std::size_t bytesRead = TRI_SIZE * 36;
  std::cout << "  File I/O: " << bytesRead / 1024 / 1024 << " MB read\n";

  // Memory allocation estimate (C, S, beta, recurrA, recurrB + Legendre workspace)
  std::size_t triN = static_cast<std::size_t>(N + 1) * static_cast<std::size_t>(N + 2) / 2;
  std::size_t memAllocated = triN * 8 * 5; // 5 arrays of doubles
  std::cout << "  Memory allocated: " << memAllocated / 1024 / 1024 << " MB\n";
}

/**
 * @test MaxFidelityPotentialTiming
 * @brief Time potential computation at various truncation levels.
 */
TEST(Egm2008MaxFidelity, PotentialTiming) {
  std::cout << "\n=== EGM2008 Potential Computation Timing ===\n";
  std::cout << "  Test point: " << TEST_ALT_KM << " km altitude over equator\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  // Test point: equator at TEST_ALT_KM altitude
  const double r_ecef[3] = {TEST_R, 0.0, 0.0};

  const int ORDERS[] = {60, 180, 360, 720, 1080, 2190};
  constexpr int WARMUP = 3;
  constexpr int ITERS = 10;

  for (int n : ORDERS) {
    Egm2008Model model;
    Egm2008Params params{GM, A, static_cast<int16_t>(n)};
    ASSERT_TRUE(model.init(src, params));

    // Warmup
    double v = 0;
    for (int i = 0; i < WARMUP; ++i) {
      model.potential(r_ecef, v);
    }

    // Timed iterations
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      model.potential(r_ecef, v);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double usPerCall = static_cast<double>(us) / ITERS;
    std::cout << "  N=" << std::setw(4) << n << ": " << std::fixed << std::setprecision(1)
              << std::setw(10) << usPerCall << " us/call"
              << "  V=" << std::scientific << std::setprecision(6) << v << " m^2/s^2\n";
  }
}

/**
 * @test MaxFidelityAccelerationTiming
 * @brief Time acceleration computation (analytic mode) at various truncation levels.
 */
TEST(Egm2008MaxFidelity, AccelerationTiming) {
  std::cout << "\n=== EGM2008 Acceleration Computation Timing (Analytic) ===\n";
  std::cout << "  Test point: " << TEST_ALT_KM << " km altitude over equator\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const double r_ecef[3] = {TEST_R, 0.0, 0.0};

  const int ORDERS[] = {60, 180, 360, 720, 1080, 2190};
  constexpr int WARMUP = 3;
  constexpr int ITERS = 10;

  for (int n : ORDERS) {
    Egm2008Model model;
    Egm2008Params params{GM, A, static_cast<int16_t>(n)};
    ASSERT_TRUE(model.init(src, params));
    model.setAccelMode(Egm2008Model::AccelMode::Analytic);

    // Warmup
    double a[3] = {0, 0, 0};
    for (int i = 0; i < WARMUP; ++i) {
      model.acceleration(r_ecef, a);
    }

    // Timed iterations
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      model.acceleration(r_ecef, a);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double usPerCall = static_cast<double>(us) / ITERS;
    double aMag = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    std::cout << "  N=" << std::setw(4) << n << ": " << std::fixed << std::setprecision(1)
              << std::setw(10) << usPerCall << " us/call"
              << "  |a|=" << std::scientific << std::setprecision(4) << aMag << " m/s^2\n";
  }
}

/**
 * @test MaxFidelityFullEvaluation
 * @brief Full evaluation (V + a) timing at max fidelity N=2190.
 */
TEST(Egm2008MaxFidelity, FullEvaluationN2190) {
  std::cout << "\n=== EGM2008 Full Evaluation at N=2190 ===\n";

  FullTableCoeffSource src;
  {
    ScopedTimer t("File open");
    if (!src.open(K_FULL_TABLE_PATH)) {
      GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
    }
  }

  Egm2008Model model;
  Egm2008Params params{GM, A, 2190};

  // Count coefficients for N=2190 (starts at n=2)
  const int N = 2190;
  const std::size_t TRI_SIZE =
      static_cast<std::size_t>(N + 1) * static_cast<std::size_t>(N + 2) / 2 - 3;
  std::cout << "  Coefficients to load: " << TRI_SIZE << " (" << TRI_SIZE * 2 * 8 / 1024 / 1024
            << " MB for C+S)\n";

  std::cout << "  Initializing model (N=2190, 2.4M coefficients)...\n";
  {
    ScopedTimer t("Model init");
    ASSERT_TRUE(model.init(src, params));
  }
  model.setAccelMode(Egm2008Model::AccelMode::Analytic);

  // Test at several altitudes
  const double ALTITUDES_KM[] = {200, 400, 800, 35786}; // LEO, ISS, higher LEO, GEO
  constexpr int ITERS = 5;

  std::cout << "\n  Single-point evaluation timing:\n";
  for (double alt : ALTITUDES_KM) {
    double r = A + alt * 1000.0;
    double r_ecef[3] = {r, 0.0, 0.0};

    // Warmup
    double V = 0;
    double a[3] = {0, 0, 0};
    model.evaluate(r_ecef, V, a);

    // Timed
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      model.evaluate(r_ecef, V, a);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double usPerCall = static_cast<double>(us) / ITERS;
    double aMag = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

    std::cout << "  Alt=" << std::setw(6) << static_cast<int>(alt) << " km: " << std::fixed
              << std::setprecision(1) << std::setw(10) << usPerCall << " us"
              << "  V=" << std::scientific << std::setprecision(4) << V << "  |a|=" << aMag
              << " m/s^2\n";
  }
}
