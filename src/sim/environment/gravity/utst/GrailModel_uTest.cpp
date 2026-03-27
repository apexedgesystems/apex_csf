/**
 * @file GrailModel_uTest.cpp
 * @brief Unit tests for GrailModel (lunar gravity model).
 *
 * Tests:
 *  - Basic initialization and parameter validation
 *  - Potential computation sanity checks
 *  - Acceleration vs potential gradient consistency
 *  - Lunar-specific physical properties
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/moon/GrailModel.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::GrailModel;
using sim::environment::gravity::GrailParams;
namespace grgm1200a = sim::environment::gravity::grgm1200a;
namespace lunar = sim::environment::gravity::lunar;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/**
 * @brief Synthetic coefficient source for lunar gravity tests.
 *
 * Generates physically plausible coefficients with C00=1 and decaying
 * higher-order terms. Includes realistic C20 for Moon's J2.
 */
class LunarSynthSource final : public CoeffSource {
public:
  explicit LunarSynthSource(int16_t nMax) : nMax_(nMax) {}
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return nMax_; }
  bool get(int16_t n, int16_t m, double& c, double& s) const noexcept override {
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
      // Use realistic lunar C20 (fully normalized)
      c = grgm1200a::C20;
      s = 0.0;
      return true;
    }
    // Decay for higher orders
    const double K = 1e-6;
    const double DENOM = static_cast<double>((n + 1) * (n + 1));
    c = K * std::sin(0.30 * n + 0.70 * m) / DENOM;
    s = K * std::cos(0.50 * n + 0.20 * m) / DENOM;
    return true;
  }

private:
  int16_t nMax_;
};

} // namespace

/* ----------------------------- Initialization Tests ----------------------------- */

/** @test GrailModel initializes with default parameters. */
TEST(GrailModelTest, InitWithDefaults) {
  LunarSynthSource src(60);
  GrailModel model;

  ASSERT_TRUE(model.init(src, 60));
  EXPECT_EQ(model.maxDegree(), 60);
}

/** @test GrailModel initializes with explicit GrailParams. */
TEST(GrailModelTest, InitWithParams) {
  LunarSynthSource src(100);
  GrailModel model;

  GrailParams params;
  params.GM = lunar::GM;
  params.a = lunar::R_REF;
  params.N = 100;

  ASSERT_TRUE(model.init(src, params));
  EXPECT_EQ(model.maxDegree(), 100);
}

/** @test GrailModel clamps N to source max degree. */
TEST(GrailModelTest, ClampsToSourceMaxDegree) {
  LunarSynthSource src(50);
  GrailModel model;

  // Request N=100 but source only has N=50
  GrailParams params;
  params.N = 100;

  ASSERT_TRUE(model.init(src, params));
  EXPECT_EQ(model.maxDegree(), 50); // Clamped to source max
}

/* ----------------------------- Potential Tests ----------------------------- */

/** @test Central term dominates at large distance. */
TEST(GrailModelTest, PotentialReducesToCentralAtLargeDistance) {
  LunarSynthSource src(60);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 60));

  // At 100 Moon radii, perturbations are negligible
  const double FAR_R = 100.0 * lunar::R_REF;
  const double R[3] = {FAR_R, 0.0, 0.0};

  double V = 0.0;
  ASSERT_TRUE(model.potential(R, V));

  const double V_CENTRAL = lunar::GM / FAR_R;
  EXPECT_NEAR(V, V_CENTRAL, V_CENTRAL * 1e-6); // Within 1 ppm
}

/** @test Potential is positive (convention: V = GM/r for attraction). */
TEST(GrailModelTest, PotentialIsPositive) {
  LunarSynthSource src(60);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 60));

  const double R[3] = {lunar::R_REF + 100e3, 0.0, 0.0}; // 100 km altitude

  double V = 0.0;
  ASSERT_TRUE(model.potential(R, V));

  EXPECT_GT(V, 0.0);
}

/** @test Potential decreases with altitude. */
TEST(GrailModelTest, PotentialDecreasesWithAltitude) {
  LunarSynthSource src(60);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 60));

  const double R_LOW[3] = {lunar::R_REF + 50e3, 0.0, 0.0};   // 50 km
  const double R_HIGH[3] = {lunar::R_REF + 200e3, 0.0, 0.0}; // 200 km

  double vLow = 0.0, vHigh = 0.0;
  ASSERT_TRUE(model.potential(R_LOW, vLow));
  ASSERT_TRUE(model.potential(R_HIGH, vHigh));

  EXPECT_GT(vLow, vHigh);
}

/* ----------------------------- Acceleration Tests ----------------------------- */

/** @test Acceleration points toward center (negative radial). */
TEST(GrailModelTest, AccelerationPointsTowardCenter) {
  LunarSynthSource src(60);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 60));

  const double R[3] = {lunar::R_REF + 100e3, 0.0, 0.0}; // On +X axis

  double a[3] = {};
  ASSERT_TRUE(model.acceleration(R, a));

  // Acceleration should point toward center (negative x)
  EXPECT_LT(a[0], 0.0);

  // y and z should be near zero (central + zonal)
  EXPECT_NEAR(a[1], 0.0, std::abs(a[0]) * 0.01);
  EXPECT_NEAR(a[2], 0.0, std::abs(a[0]) * 0.01);
}

/** @test Acceleration magnitude is reasonable for Moon surface. */
TEST(GrailModelTest, SurfaceGravityMagnitude) {
  LunarSynthSource src(60);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 60));

  const double R[3] = {lunar::R_REF, 0.0, 0.0};

  double a[3] = {};
  ASSERT_TRUE(model.acceleration(R, a));

  const double MAG = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

  // Moon surface gravity ~1.62 m/s^2
  EXPECT_GT(MAG, 1.5);
  EXPECT_LT(MAG, 1.8);
}

/** @test Acceleration consistent with potential gradient. */
TEST(GrailModelTest, AccelerationConsistentWithPotentialGradient) {
  LunarSynthSource src(20);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 20));

  const double R[3] = {lunar::R_REF + 100e3, 50e3, 30e3}; // Off-axis point
  const double H = 10.0;                                  // 10m step for finite difference

  double aModel[3] = {};
  ASSERT_TRUE(model.acceleration(R, aModel));

  // Compute numerical gradient: a = grad(V)
  double aNumeric[3] = {};
  for (int i = 0; i < 3; ++i) {
    double rPlus[3] = {R[0], R[1], R[2]};
    double rMinus[3] = {R[0], R[1], R[2]};
    rPlus[i] += H;
    rMinus[i] -= H;

    double vPlus = 0.0, vMinus = 0.0;
    model.potential(rPlus, vPlus);
    model.potential(rMinus, vMinus);
    aNumeric[i] = (vPlus - vMinus) / (2.0 * H);
  }

  // Should match within finite difference accuracy (~H^2)
  const double TOL = 1e-4;
  EXPECT_NEAR(aModel[0], aNumeric[0], std::abs(aModel[0]) * TOL);
  EXPECT_NEAR(aModel[1], aNumeric[1], std::abs(aModel[1]) * TOL);
  EXPECT_NEAR(aModel[2], aNumeric[2], std::abs(aModel[2]) * TOL);
}

/* ----------------------------- AccelMode Tests ----------------------------- */

/** @test Analytic mode produces same result as numeric. */
TEST(GrailModelTest, AnalyticMatchesNumeric) {
  LunarSynthSource src(20);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 20));

  const double R[3] = {lunar::R_REF + 100e3, 50e3, 30e3};

  // Numeric mode (default)
  double aNumeric[3] = {};
  model.setAccelMode(GrailModel::AccelMode::Numeric);
  ASSERT_TRUE(model.acceleration(R, aNumeric));

  // Analytic mode
  double aAnalytic[3] = {};
  model.setAccelMode(GrailModel::AccelMode::Analytic);
  ASSERT_TRUE(model.acceleration(R, aAnalytic));

  // Should match closely (numeric uses finite differencing with H=10m,
  // so error is O(H^2/R) ~ 1e-4 relative, but can be higher at poles)
  const double TOL = 2e-3;
  EXPECT_NEAR(aAnalytic[0], aNumeric[0], std::abs(aNumeric[0]) * TOL);
  EXPECT_NEAR(aAnalytic[1], aNumeric[1], std::abs(aNumeric[1]) * TOL);
  EXPECT_NEAR(aAnalytic[2], aNumeric[2], std::abs(aNumeric[2]) * TOL);
}

/* ----------------------------- Evaluate Tests ----------------------------- */

/** @test Evaluate returns both potential and acceleration. */
TEST(GrailModelTest, EvaluateReturnsBoth) {
  LunarSynthSource src(20);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 20));

  const double R[3] = {lunar::R_REF + 100e3, 0.0, 0.0};

  double V = 0.0;
  double a[3] = {};
  ASSERT_TRUE(model.evaluate(R, V, a));

  // Compare with individual calls
  double vSingle = 0.0;
  double aSingle[3] = {};
  model.potential(R, vSingle);
  model.acceleration(R, aSingle);

  EXPECT_DOUBLE_EQ(V, vSingle);
  EXPECT_DOUBLE_EQ(a[0], aSingle[0]);
  EXPECT_DOUBLE_EQ(a[1], aSingle[1]);
  EXPECT_DOUBLE_EQ(a[2], aSingle[2]);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test Returns false for zero position. */
TEST(GrailModelTest, RejectZeroPosition) {
  LunarSynthSource src(20);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 20));

  const double R[3] = {0.0, 0.0, 0.0};

  double V = 0.0;
  EXPECT_FALSE(model.potential(R, V));

  double a[3] = {};
  EXPECT_FALSE(model.acceleration(R, a));
}

/** @test Handles very close to surface position. */
TEST(GrailModelTest, NearSurfaceStable) {
  LunarSynthSource src(20);
  GrailModel model;
  ASSERT_TRUE(model.init(src, 20));

  // Just 1 meter above reference radius
  const double R[3] = {lunar::R_REF + 1.0, 0.0, 0.0};

  double V = 0.0;
  double a[3] = {};

  EXPECT_TRUE(model.potential(R, V));
  EXPECT_TRUE(model.acceleration(R, a));
  EXPECT_TRUE(std::isfinite(V));
  EXPECT_TRUE(std::isfinite(a[0]));
}
