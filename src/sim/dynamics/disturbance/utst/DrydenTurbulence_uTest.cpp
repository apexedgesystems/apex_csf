/**
 * @file DrydenTurbulence_uTest.cpp
 * @brief Unit tests for Dryden turbulence (3-axis PSD-shaped white noise).
 *
 * Coverage:
 *   1. Zero airspeed (V < 1) -> output frozen (no division by V)
 *   2. Zero sigma on all axes -> output stays at zero state
 *   3. RNG reproducibility: same seed gives same trajectory
 *   4. Different seeds give different trajectories
 *   5. Long-term RMS of u_g approximates sigma_u (within 30%, finite-sample)
 *   6. Long-term RMS of w_g approximates sigma_w (within 30%)
 *   7. Mean of long sample approaches zero (zero-mean by construction)
 *   8. Output magnitude bounded (no NaN / unbounded)
 *
 * The 30% RMS tolerance is generous because:
 *   - Finite-sample variance of sigma_hat is O(1/sqrt(N)) for N samples
 *   - The discrete-time backward-Euler approximation introduces small
 *     bias in the spectral magnitude
 *   - Filter "warm-up" (first tau seconds) has lower RMS than steady state
 */

#include "src/sim/dynamics/disturbance/inc/DrydenTurbulence.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::disturbance::DrydenRng;
using sim::dynamics::disturbance::DrydenTurbulenceParams;
using sim::dynamics::disturbance::DrydenTurbulenceState;
using sim::dynamics::disturbance::stepDryden;

/* ----------------------------- Edge cases ----------------------------- */

TEST(DrydenTest, ZeroAirspeedFreezesOutput) {
  DrydenTurbulenceParams p;
  DrydenTurbulenceState s;
  DrydenRng rng;
  s.u_g = 0.5;
  s.v_x2 = -0.3;
  s.w_x2 = 0.8;
  const auto r = stepDryden(s, p, rng, /*V*/ 0.5, /*dt*/ 0.02);
  EXPECT_DOUBLE_EQ(r.u_g_m_s, 0.5);
  EXPECT_DOUBLE_EQ(r.v_g_m_s, -0.3);
  EXPECT_DOUBLE_EQ(r.w_g_m_s, 0.8);
}

TEST(DrydenTest, NonPositiveDtFreezesOutput) {
  // Exercises the dt_s <= 0 half of the guard (the V<1 case covers the other).
  DrydenTurbulenceParams p;
  DrydenTurbulenceState s;
  DrydenRng rng;
  s.u_g = 0.7;
  s.v_x2 = 0.2;
  s.w_x2 = -0.4;
  const auto r = stepDryden(s, p, rng, /*V*/ 235.0, /*dt*/ 0.0);
  EXPECT_DOUBLE_EQ(r.u_g_m_s, 0.7);
  EXPECT_DOUBLE_EQ(r.v_g_m_s, 0.2);
  EXPECT_DOUBLE_EQ(r.w_g_m_s, -0.4);
}

TEST(DrydenTest, ZeroIntensityProducesZeroOutput) {
  DrydenTurbulenceParams p;
  p.sigma_u_m_s = 0.0;
  p.sigma_v_m_s = 0.0;
  p.sigma_w_m_s = 0.0;
  DrydenTurbulenceState s;
  DrydenRng rng;

  for (int i = 0; i < 1000; ++i) {
    stepDryden(s, p, rng, /*V*/ 235.0, /*dt*/ 0.02);
  }
  // After 20 sec all states should still be at 0 (filter K = 0).
  EXPECT_NEAR(s.u_g, 0.0, 1e-12);
  EXPECT_NEAR(s.v_x2, 0.0, 1e-12);
  EXPECT_NEAR(s.w_x2, 0.0, 1e-12);
}

/* ----------------------------- RNG determinism ----------------------------- */

TEST(DrydenTest, SameSeedGivesSameTrajectory) {
  DrydenTurbulenceParams p;
  DrydenTurbulenceState s1, s2;
  DrydenRng rng1(/*seed*/ 12345);
  DrydenRng rng2(/*seed*/ 12345);

  for (int i = 0; i < 200; ++i) {
    auto r1 = stepDryden(s1, p, rng1, 235.0, 0.02);
    auto r2 = stepDryden(s2, p, rng2, 235.0, 0.02);
    EXPECT_DOUBLE_EQ(r1.u_g_m_s, r2.u_g_m_s);
    EXPECT_DOUBLE_EQ(r1.v_g_m_s, r2.v_g_m_s);
    EXPECT_DOUBLE_EQ(r1.w_g_m_s, r2.w_g_m_s);
  }
}

/** @test reseed() restarts a stream so two RNGs reseeded alike agree again. */
TEST(DrydenTest, ReseedRestartsStream) {
  DrydenRng a(/*seed*/ 7);
  DrydenRng b(/*seed*/ 999);
  // Advance b so its state differs from a fresh seed-7 stream.
  for (int i = 0; i < 50; ++i)
    (void)b.normal01();

  a.reseed(123);
  b.reseed(123); // both now seeded identically and Box-Muller carry cleared
  for (int i = 0; i < 100; ++i) {
    EXPECT_DOUBLE_EQ(a.normal01(), b.normal01());
  }
}

TEST(DrydenTest, DifferentSeedsDiverge) {
  DrydenTurbulenceParams p;
  DrydenTurbulenceState s1, s2;
  DrydenRng rng1(/*seed*/ 1);
  DrydenRng rng2(/*seed*/ 2);

  // Run a while; verify final outputs differ.
  for (int i = 0; i < 200; ++i) {
    stepDryden(s1, p, rng1, 235.0, 0.02);
    stepDryden(s2, p, rng2, 235.0, 0.02);
  }
  // At least one axis must differ -- otherwise the seeds aren't behaving.
  const bool any_differ = (s1.u_g != s2.u_g) || (s1.v_x2 != s2.v_x2) || (s1.w_x2 != s2.w_x2);
  EXPECT_TRUE(any_differ);
}

/* ----------------------------- Statistical properties ----------------------------- */

namespace {

/** Run the model for N samples; return (mean, RMS) of the u_g time series. */
struct AxisStats {
  double mean;
  double rms;
};
AxisStats statsForU(const DrydenTurbulenceParams& p, double V_m_s, double dt, int n_warmup,
                    int n_samples) {
  DrydenTurbulenceState s;
  DrydenRng rng(p.seed);
  for (int i = 0; i < n_warmup; ++i) {
    stepDryden(s, p, rng, V_m_s, dt);
  }
  double sum = 0.0, sum_sq = 0.0;
  for (int i = 0; i < n_samples; ++i) {
    const auto r = stepDryden(s, p, rng, V_m_s, dt);
    sum += r.u_g_m_s;
    sum_sq += r.u_g_m_s * r.u_g_m_s;
  }
  const double mean = sum / n_samples;
  const double rms = std::sqrt(sum_sq / n_samples);
  return {mean, rms};
}

AxisStats statsForW(const DrydenTurbulenceParams& p, double V_m_s, double dt, int n_warmup,
                    int n_samples) {
  DrydenTurbulenceState s;
  DrydenRng rng(p.seed);
  for (int i = 0; i < n_warmup; ++i) {
    stepDryden(s, p, rng, V_m_s, dt);
  }
  double sum = 0.0, sum_sq = 0.0;
  for (int i = 0; i < n_samples; ++i) {
    const auto r = stepDryden(s, p, rng, V_m_s, dt);
    sum += r.w_g_m_s;
    sum_sq += r.w_g_m_s * r.w_g_m_s;
  }
  return {sum / n_samples, std::sqrt(sum_sq / n_samples)};
}

} // namespace

TEST(DrydenTest, LongRunULogitudinalRmsApproximatesSigmaU) {
  DrydenTurbulenceParams p;
  p.sigma_u_m_s = 1.0;
  // Long enough to settle and get good statistics.
  const auto stats = statsForU(p, /*V*/ 235.0, /*dt*/ 0.02,
                               /*warmup*/ 1000, /*samples*/ 50000);
  // First-order Dryden filter should produce RMS ~ sigma_u within finite-
  // sample noise (~30%).
  EXPECT_NEAR(stats.rms, p.sigma_u_m_s, 0.3 * p.sigma_u_m_s)
      << "u_g RMS = " << stats.rms << " vs sigma_u = " << p.sigma_u_m_s;
  // Mean should be close to zero (zero-mean white noise).
  EXPECT_NEAR(stats.mean, 0.0, 0.2 * p.sigma_u_m_s);
}

TEST(DrydenTest, LongRunWVerticalRmsScalesWithSigmaW) {
  DrydenTurbulenceParams p_low;
  p_low.sigma_w_m_s = 0.5;
  DrydenTurbulenceParams p_high;
  p_high.sigma_w_m_s = 2.0;

  const auto rms_low = statsForW(p_low, 235.0, 0.02, 1000, 30000).rms;
  const auto rms_high = statsForW(p_high, 235.0, 0.02, 1000, 30000).rms;

  // Doubling sigma_w should approximately quadruple RMS (4x sigma_w increase = 4x RMS).
  EXPECT_NEAR(rms_high / rms_low, 4.0, 1.0);
}

AxisStats statsForV(const DrydenTurbulenceParams& p, double V_m_s, double dt, int n_warmup,
                    int n_samples) {
  DrydenTurbulenceState s;
  DrydenRng rng(p.seed);
  for (int i = 0; i < n_warmup; ++i) {
    stepDryden(s, p, rng, V_m_s, dt);
  }
  double sum = 0.0, sum_sq = 0.0;
  for (int i = 0; i < n_samples; ++i) {
    const auto r = stepDryden(s, p, rng, V_m_s, dt);
    sum += r.v_g_m_s;
    sum_sq += r.v_g_m_s * r.v_g_m_s;
  }
  return {sum / n_samples, std::sqrt(sum_sq / n_samples)};
}

/**
 * @test The 2nd-order (v_g, w_g) filters must produce RMS ~ sigma in absolute
 * terms, not merely scale linearly with sigma. This is the regression guard
 * for the gain-calibration bug: the original K = sigma*sqrt(8 L / V) gain
 * overstated the RMS by a factor sqrt(2) (~1.41 sigma, ~41% too strong); the
 * corrected K = sigma*sqrt(4 L / V) lands at sigma. A +-12% band passes the
 * fixed gain and fails the old one. (The scaling-only test above passes
 * either way, which is why it never caught this.)
 */
TEST(DrydenTest, LateralAndVerticalRmsMatchSigmaInAbsoluteTerms) {
  DrydenTurbulenceParams p; // sigma_v = sigma_w = ... set both to 1 for a clean target
  p.sigma_v_m_s = 1.0;
  p.sigma_w_m_s = 1.0;

  const double TOL = 0.12; // tight enough to reject the sqrt(2) overshoot
  const auto v = statsForV(p, /*V*/ 235.0, /*dt*/ 0.02, /*warmup*/ 2000, /*samples*/ 200000);
  const auto w = statsForW(p, /*V*/ 235.0, /*dt*/ 0.02, /*warmup*/ 2000, /*samples*/ 200000);

  EXPECT_NEAR(v.rms, p.sigma_v_m_s, TOL * p.sigma_v_m_s)
      << "v_g RMS = " << v.rms << " vs sigma_v = " << p.sigma_v_m_s;
  EXPECT_NEAR(w.rms, p.sigma_w_m_s, TOL * p.sigma_w_m_s)
      << "w_g RMS = " << w.rms << " vs sigma_w = " << p.sigma_w_m_s;
  // Zero-mean by construction.
  EXPECT_NEAR(v.mean, 0.0, 0.2 * p.sigma_v_m_s);
  EXPECT_NEAR(w.mean, 0.0, 0.2 * p.sigma_w_m_s);
}

/* ----------------------------- Boundedness ----------------------------- */

TEST(DrydenTest, OutputRemainsFiniteOverLongRun) {
  DrydenTurbulenceParams p;
  p.sigma_u_m_s = 6.0; // severe-turbulence intensity
  p.sigma_v_m_s = 6.0;
  p.sigma_w_m_s = 4.5;
  DrydenTurbulenceState s;
  DrydenRng rng(p.seed);

  for (int i = 0; i < 100000; ++i) {
    const auto r = stepDryden(s, p, rng, 235.0, 0.02);
    ASSERT_TRUE(std::isfinite(r.u_g_m_s));
    ASSERT_TRUE(std::isfinite(r.v_g_m_s));
    ASSERT_TRUE(std::isfinite(r.w_g_m_s));
    // Bounds: gust shouldn't exceed ~6 sigma even for severe turb (Gaussian tail).
    ASSERT_LT(std::fabs(r.u_g_m_s), 6.0 * p.sigma_u_m_s);
    ASSERT_LT(std::fabs(r.v_g_m_s), 6.0 * p.sigma_v_m_s);
    ASSERT_LT(std::fabs(r.w_g_m_s), 6.0 * p.sigma_w_m_s);
  }
}
