/**
 * @file Mount_uTest.cpp
 * @brief Tests for the mount sugar and the ticket's acceptance chain: the
 *        CG-relative sensed target as a single resolve, with a live CG.
 */

#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Mount.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace fr = apex::math::frames;
using fr::FrameGraph;
using fr::FrameId;
using fr::Mount;
using fr::Transform;

namespace {

constexpr double K_PI = 3.14159265358979323846;

/// The mass stack's live CG: a body-aligned frame whose origin tracks cg[3].
template <typename T> struct CgCtx {
  T cg[3] = {T(0), T(0), T(0)};
};
template <typename T> uint8_t cgEdge(void* ctx, T /*t*/, Transform<T>* out) noexcept {
  const auto* c = static_cast<const CgCtx<T>*>(ctx);
  *out = Transform<T>{};
  out->t[0] = c->cg[0];
  out->t[1] = c->cg[1];
  out->t[2] = c->cg[2];
  return 0;
}

} // namespace

template <typename T> class MountTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(MountTestT, ValueTypes);

/* --------------------------------- Mount ---------------------------------- */

/** @test Mount is flat/streamable; its edge reproduces lever arm + attitude. */
TYPED_TEST(MountTestT, MountPodAndEdge) {
  using T = TypeParam;
  static_assert(std::is_trivially_copyable<Mount<T>>::value, "must stream");
  Mount<T> m;
  m.leverArmM[0] = T(2);
  m.leverArmM[2] = T(-0.5);
  // Sensor yawed +90 in the body: sensor +X looks along body +Y.
  T qd[4];
  apex::math::quaternion::Quaternion<T> q(qd);
  q.setFromAngleAxis(T(K_PI / 2), T(0), T(0), T(1));
  for (int i = 0; i < 4; ++i) {
    m.q[i] = qd[i];
  }

  FrameGraph<T> g;
  FrameId body = 0, sensor = 0;
  ASSERT_EQ(g.addRoot("body", body), 0);
  ASSERT_EQ(fr::addMount(g, body, m, "pod", sensor), 0);
  EXPECT_EQ(g.kindOf(sensor), fr::EdgeKind::STATIC);

  // The sensor boresight (+X in sensor coords) points along body +Y...
  const T BORE[3] = {T(1), T(0), T(0)};
  T d[3];
  ASSERT_EQ(g.in(body).from(sensor).vector(BORE, d, T(0)), 0);
  EXPECT_NEAR(d[1], T(1), T(1e-5));
  // ...and a target 10 m down the boresight sits at the lever arm + 10 body-Y.
  const T TGT[3] = {T(10), T(0), T(0)};
  T p[3];
  ASSERT_EQ(g.in(body).from(sensor).point(TGT, p, T(0)), 0);
  EXPECT_NEAR(p[0], T(2), T(1e-4));
  EXPECT_NEAR(p[1], T(10), T(1e-4));
  EXPECT_NEAR(p[2], T(-0.5), T(1e-4));
}

/* --------------------------- The acceptance chain -------------------------- */

/** @test TICKET ACCEPTANCE: r_target/CG is one resolve call, and it tracks a
 *        CG moved by the mass stack between queries (prop burn). */
TYPED_TEST(MountTestT, CgRelativeSensedTargetTracksLiveCg) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId body = 0, cg = 0, sensor = 0;
  ASSERT_EQ(g.addRoot("body", body), 0);

  CgCtx<T> mass;
  mass.cg[0] = T(0.20); // CG forward of the body origin
  ASSERT_EQ(g.addStateDriven(body, {&cgEdge<T>, &mass}, "cg", cg), 0);

  Mount<T> m;
  m.leverArmM[0] = T(3.0);
  m.leverArmM[1] = T(0.4);
  ASSERT_EQ(fr::addMount(g, body, m, "rangefinder", sensor), 0);

  // The sensor reports a target at d = 25 m along its boresight (+X).
  const T MEAS[3] = {T(25), T(0), T(0)};
  T rTargetCg[3];
  ASSERT_EQ(g.in(cg).from(sensor).point(MEAS, rTargetCg, T(0)), 0);

  // Hand formula: (p_mount - p_cg) + R_mount * (d * a_hat), R = identity.
  EXPECT_NEAR(rTargetCg[0], T(3.0) - T(0.20) + T(25), T(1e-4));
  EXPECT_NEAR(rTargetCg[1], T(0.4), T(1e-5));

  // Prop burn shifts the CG aft; the same query tracks it with no rewiring.
  mass.cg[0] = T(-0.10);
  ASSERT_EQ(g.in(cg).from(sensor).point(MEAS, rTargetCg, T(0)), 0);
  EXPECT_NEAR(rTargetCg[0], T(3.0) + T(0.10) + T(25), T(1e-4));

  // The ray DIRECTION is CG-invariant (vector path: no lever arms anywhere).
  const T BORE[3] = {T(1), T(0), T(0)};
  T d0[3], d1[3];
  mass.cg[0] = T(0.20);
  ASSERT_EQ(g.in(cg).from(sensor).vector(BORE, d0, T(0)), 0);
  mass.cg[0] = T(-0.10);
  ASSERT_EQ(g.in(cg).from(sensor).vector(BORE, d1, T(0)), 0);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(d0[i], d1[i], T(1e-6));
  }
}

/** @test Per-arm arrays are just several mounts: two pods, distinct results. */
TYPED_TEST(MountTestT, TwoMountsCoexist) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId body = 0, podA = 0, podB = 0;
  ASSERT_EQ(g.addRoot("body", body), 0);

  Mount<T> a, b;
  a.leverArmM[1] = T(1.0);  // port
  b.leverArmM[1] = T(-1.0); // starboard
  ASSERT_EQ(fr::addMount(g, body, a, "pod_port", podA), 0);
  ASSERT_EQ(fr::addMount(g, body, b, "pod_stbd", podB), 0);

  const T ORIGIN[3] = {T(0), T(0), T(0)};
  T pa[3], pb[3], ab[3];
  ASSERT_EQ(g.in(body).from(podA).point(ORIGIN, pa, T(0)), 0);
  ASSERT_EQ(g.in(body).from(podB).point(ORIGIN, pb, T(0)), 0);
  EXPECT_NEAR(pa[1], T(1.0), T(1e-6));
  EXPECT_NEAR(pb[1], T(-1.0), T(1e-6));
  // And pod-to-pod resolves directly (2 m of separation).
  ASSERT_EQ(g.in(podA).from(podB).point(ORIGIN, ab, T(0)), 0);
  EXPECT_NEAR(ab[1], T(-2.0), T(1e-6));
}
