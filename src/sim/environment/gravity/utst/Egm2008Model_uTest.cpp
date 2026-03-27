/**
 * @file Egm2008Model_uTest.cpp
 * @brief Unit tests for sim::environment::gravity::Egm2008Model.
 *
 * Notes:
 *  - Uses tiny coefficient sources to test specific physical behaviors.
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008Params;

/* ----------------------------- Test Fixtures ----------------------------- */

/// N=0 only: Cbar_00 = 1, others 0.
class TinySourceN0 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 0; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    if (n == 0 && m == 0) {
      C = 1.0;
      S = 0.0;
      return true;
    }
    C = 0.0;
    S = 0.0;
    return (n == 0 && m == 0);
  }
};

/// N=2 zonal with J2 only: Cbar_00 = 1; Cbar_20 = -0.484165143790815e-3.
class TinySourceJ2 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 2; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    S = 0.0;
    if (n == 0 && m == 0) {
      C = 1.0;
      return true;
    }
    if (n == 2 && m == 0) {
      C = -0.484165143790815e-3;
      return true;
    }
    C = 0.0;
    return true;
  }
};

/* ----------------------------- API Tests ----------------------------- */

/** @test N=0 model reduces to central potential V = GM/r. */
TEST(Egm2008ModelTest, N0ReducesToCentralPotential) {
  TinySourceN0 src;
  Egm2008Model model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 0;

  ASSERT_TRUE(model.init(src, p));

  const double R[3] = {7000e3, 0.0, 0.0};
  const double RMAG = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);

  double v = 0.0;
  ASSERT_TRUE(model.potential(R, v));
  EXPECT_NEAR(v, p.GM / RMAG, 1e-6);

  double a[3] = {};
  ASSERT_TRUE(model.acceleration(R, a));
  const double AMAG = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  EXPECT_NEAR(AMAG, p.GM / (RMAG * RMAG), 1e-4);
  EXPECT_NEAR(a[0] * (R[0] / RMAG), -AMAG, 1e-5);
  EXPECT_NEAR(a[1], 0.0, 1e-8);
  EXPECT_NEAR(a[2], 0.0, 1e-8);
}

/** @test J2 model shows equator vs pole potential difference and longitude invariance. */
TEST(Egm2008ModelTest, J2SanityEquatorVsPoleAndLongitudeInvariance) {
  TinySourceJ2 src;
  Egm2008Model model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  ASSERT_TRUE(model.init(src, p));

  const double REQ[3] = {p.a, 0.0, 0.0};
  const double REQ90[3] = {0.0, p.a, 0.0};
  const double RPOL[3] = {0.0, 0.0, p.a};
  const double RMAG = p.a;

  const double V0 = p.GM / RMAG;

  double veq = 0.0, veq90 = 0.0, vp = 0.0;
  ASSERT_TRUE(model.potential(REQ, veq));
  ASSERT_TRUE(model.potential(REQ90, veq90));
  ASSERT_TRUE(model.potential(RPOL, vp));

  // J2 < 0: equator potential > central, pole potential < central
  EXPECT_GT(veq, V0);
  EXPECT_LT(vp, V0);

  // Zonal-only means longitude invariance
  EXPECT_NEAR(veq, veq90, 1e-10);

  // Equator > pole for J2 < 0
  EXPECT_GT(veq, vp);
}
