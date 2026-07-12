/**
 * @file FrameGraph_uTest.cpp
 * @brief Tests for the frame forest: registry limits, resolve against hand
 *        composition, time/state-driven edges, multi-root NO_PATH, and the
 *        fluent point/vector queries.
 */

#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace fr = apex::math::frames;
using fr::FrameGraph;
using fr::FrameId;
using fr::Transform;

namespace {

constexpr double K_PI = 3.14159265358979323846;

/// Time-driven edge: yaw about +Z at a fixed rate (earth-rotation shaped).
template <typename T> struct SpinCtx {
  T rate;
};
template <typename T> uint8_t spinEdge(void* ctx, T t, Transform<T>* out) noexcept {
  auto* c = static_cast<SpinCtx<T>*>(ctx);
  *out = Transform<T>{};
  out->rotation().setFromAngleAxis(c->rate * t, T(0), T(0), T(1));
  return 0;
}

/// State-driven edge: a live pose block (6DOF-shaped).
template <typename T> struct PoseCtx {
  Transform<T> pose;
};
template <typename T> uint8_t poseEdge(void* ctx, T /*t*/, Transform<T>* out) noexcept {
  *out = static_cast<PoseCtx<T>*>(ctx)->pose;
  return 0;
}

} // namespace

template <typename T> class FrameGraphTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(FrameGraphTestT, ValueTypes);

/* ------------------------------- Registry --------------------------------- */

/** @test Capacity, bad-parent, and null-provider are refused with reasons. */
TYPED_TEST(FrameGraphTestT, RegistryLimits) {
  using T = TypeParam;
  FrameGraph<T, 3> g;
  FrameId root = 0, a = 0, b = 0, overflow = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  ASSERT_EQ(g.addStatic(root, Transform<T>{}, "a", a), 0);
  ASSERT_EQ(g.addStatic(root, Transform<T>{}, "b", b), 0);
  EXPECT_EQ(g.addStatic(root, Transform<T>{}, "over", overflow),
            static_cast<uint8_t>(fr::Status::ERROR_CAPACITY));

  FrameGraph<T, 8> h;
  FrameId bad = 0;
  EXPECT_EQ(h.addStatic(FrameId{5}, Transform<T>{}, "orphan", bad),
            static_cast<uint8_t>(fr::Status::ERROR_BAD_FRAME));
  EXPECT_EQ(h.addTimeDriven(FrameId{0}, typename FrameGraph<T, 8>::EdgeProvider{}, "null", bad),
            static_cast<uint8_t>(fr::Status::ERROR_NO_PROVIDER));
}

/** @test Introspection reports parents, kinds, and logging names. */
TYPED_TEST(FrameGraphTestT, Introspection) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, body = 0;
  ASSERT_EQ(g.addRoot("world", root), 0);
  PoseCtx<T> ctx;
  ASSERT_EQ(g.addStateDriven(root, {&poseEdge<T>, &ctx}, "body", body), 0);

  EXPECT_EQ(g.size(), 2u);
  EXPECT_TRUE(g.valid(body));
  EXPECT_FALSE(g.valid(FrameId{7}));
  EXPECT_EQ(g.parentOf(body), root);
  EXPECT_EQ(g.parentOf(root), fr::K_NO_FRAME);
  EXPECT_EQ(g.kindOf(root), fr::EdgeKind::ROOT);
  EXPECT_EQ(g.kindOf(body), fr::EdgeKind::STATE_DRIVEN);
  EXPECT_STREQ(g.nameOf(body), "body");
}

/* -------------------------------- Resolve --------------------------------- */

/** @test A three-hop static chain resolves identically to hand composition. */
TYPED_TEST(FrameGraphTestT, ChainMatchesHandComposition) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, body = 0, payload = 0, sensor = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);

  Transform<T> eBody, ePayload, eSensor;
  eBody.rotation().setFromEuler321(T(0.2), T(-0.1), T(0.8));
  eBody.t[0] = T(100);
  ePayload.rotation().setFromAngleAxis(T(0.5), T(0), T(1), T(0));
  ePayload.t[2] = T(-0.4);
  eSensor.t[1] = T(1.5);

  ASSERT_EQ(g.addStatic(root, eBody, "body", body), 0);
  ASSERT_EQ(g.addStatic(body, ePayload, "payload", payload), 0);
  ASSERT_EQ(g.addStatic(payload, eSensor, "sensor", sensor), 0);

  Transform<T> viaGraph, ab, hand;
  ASSERT_EQ(g.resolve(sensor, root, T(0), viaGraph), 0);
  fr::composeInto(eBody, ePayload, ab);
  fr::composeInto(ab, eSensor, hand);

  const T P[3] = {T(0.3), T(-2), T(1)};
  T pg[3], ph[3];
  ASSERT_EQ(fr::transformPointInto(viaGraph, P, pg), 0);
  ASSERT_EQ(fr::transformPointInto(hand, P, ph), 0);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-3);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(pg[i], ph[i], TOL);
  }
}

/** @test Sibling frames resolve across their common parent (hand-derived). */
TYPED_TEST(FrameGraphTestT, SiblingResolveHandDerived) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, a = 0, b = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);

  Transform<T> ea; // a: +5 on root X
  ea.t[0] = T(5);
  Transform<T> eb; // b: rotated +90 about Z, at origin
  eb.rotation().setFromAngleAxis(T(K_PI / 2), T(0), T(0), T(1));

  ASSERT_EQ(g.addStatic(root, ea, "a", a), 0);
  ASSERT_EQ(g.addStatic(root, eb, "b", b), 0);

  // A point at a's origin is (5,0,0) in root; b is root rotated +90 about Z,
  // so root (5,0,0) reads as (0,-5,0)... in b coords: p_b = R^-1 p_root.
  const T ORIGIN[3] = {T(0), T(0), T(0)};
  T out[3];
  ASSERT_EQ(g.in(b).from(a).point(ORIGIN, out, T(0)), 0);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-4);
  EXPECT_NEAR(out[0], T(0), TOL);
  EXPECT_NEAR(out[1], T(-5), TOL);
  EXPECT_NEAR(out[2], T(0), TOL);
}

/** @test from == to yields the identity transform. */
TYPED_TEST(FrameGraphTestT, SelfResolveIsIdentity) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, a = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  Transform<T> ea;
  ea.rotation().setFromEuler321(T(0.4), T(0.5), T(0.6));
  ea.t[1] = T(9);
  ASSERT_EQ(g.addStatic(root, ea, "a", a), 0);

  Transform<T> x;
  ASSERT_EQ(g.resolve(a, a, T(0), x), 0);
  EXPECT_NEAR(std::abs(static_cast<double>(x.q[0])), 1.0, 1e-6);
  const T T_TOL = std::is_same<T, double>::value ? T(1e-15) : T(1e-6);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(x.t[i], T(0), T_TOL);
  }
}

/** @test Frames in disconnected trees report ERROR_NO_PATH. */
TYPED_TEST(FrameGraphTestT, DisconnectedTreesNoPath) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId earth = 0, moon = 0, ecef = 0, mcmf = 0;
  ASSERT_EQ(g.addRoot("eci", earth), 0);
  ASSERT_EQ(g.addRoot("mci", moon), 0);
  ASSERT_EQ(g.addStatic(earth, Transform<T>{}, "ecef", ecef), 0);
  ASSERT_EQ(g.addStatic(moon, Transform<T>{}, "mcmf", mcmf), 0);

  Transform<T> x;
  EXPECT_EQ(g.resolve(ecef, mcmf, T(0), x), static_cast<uint8_t>(fr::Status::ERROR_NO_PATH));
  EXPECT_EQ(g.resolve(ecef, earth, T(0), x), 0); // same tree still fine
}

/* --------------------------- Driven edges ---------------------------------- */

/** @test A time-driven edge evaluates at the explicit t (never wall clock). */
TYPED_TEST(FrameGraphTestT, TimeDrivenEdgeTracksT) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId inertial = 0, fixed = 0;
  ASSERT_EQ(g.addRoot("inertial", inertial), 0);
  SpinCtx<T> spin{T(0.1)}; // rad/s about +Z
  ASSERT_EQ(g.addTimeDriven(inertial, {&spinEdge<T>, &spin}, "fixed", fixed), 0);

  // A point on the fixed frame's +X axis, expressed in inertial at t:
  // rotated by rate*t about +Z.
  const T P[3] = {T(1), T(0), T(0)};
  const T TIMES[] = {T(0), T(5), T(10)};
  for (T t : TIMES) {
    T out[3];
    ASSERT_EQ(g.in(inertial).from(fixed).point(P, out, t), 0);
    const T ANG = T(0.1) * t;
    const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
    EXPECT_NEAR(out[0], std::cos(ANG), TOL);
    EXPECT_NEAR(out[1], std::sin(ANG), TOL);
  }
}

/** @test A state-driven edge reflects live context changes between resolves. */
TYPED_TEST(FrameGraphTestT, StateDrivenEdgeSeesLiveState) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId world = 0, body = 0;
  ASSERT_EQ(g.addRoot("world", world), 0);
  PoseCtx<T> pose;
  ASSERT_EQ(g.addStateDriven(world, {&poseEdge<T>, &pose}, "body", body), 0);

  const T ORIGIN[3] = {T(0), T(0), T(0)};
  T out[3];
  pose.pose.t[0] = T(10);
  ASSERT_EQ(g.in(world).from(body).point(ORIGIN, out, T(0)), 0);
  EXPECT_NEAR(out[0], T(10), T(1e-6));

  pose.pose.t[0] = T(-4); // the 6DOF stepped
  ASSERT_EQ(g.in(world).from(body).point(ORIGIN, out, T(0)), 0);
  EXPECT_NEAR(out[0], T(-4), T(1e-6));
}

/* ------------------------- Fluent API + updates ---------------------------- */

/** @test vector() ignores every translation on the path; point() does not. */
TYPED_TEST(FrameGraphTestT, FluentVectorSkipsLeverArms) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, body = 0, mount = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  Transform<T> eBody, eMount;
  eBody.t[0] = T(1000);
  eMount.t[1] = T(2);
  ASSERT_EQ(g.addStatic(root, eBody, "body", body), 0);
  ASSERT_EQ(g.addStatic(body, eMount, "mount", mount), 0);

  const T RAY[3] = {T(0), T(0), T(1)};
  T v[3], p[3];
  ASSERT_EQ(g.in(root).from(mount).vector(RAY, v, T(0)), 0);
  ASSERT_EQ(g.in(root).from(mount).point(RAY, p, T(0)), 0);
  EXPECT_NEAR(v[0], T(0), T(1e-6)); // direction untouched by offsets
  EXPECT_NEAR(v[2], T(1), T(1e-6));
  EXPECT_NEAR(p[0], T(1000), T(1e-3)); // position picks up both arms
  EXPECT_NEAR(p[1], T(2), T(1e-3));
}

/** @test updateStatic moves a frame; refused for provider-driven frames. */
TYPED_TEST(FrameGraphTestT, UpdateStatic) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, site = 0, spin = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  Transform<T> e;
  e.t[0] = T(1);
  ASSERT_EQ(g.addStatic(root, e, "site", site), 0);
  SpinCtx<T> ctx{T(1)};
  ASSERT_EQ(g.addTimeDriven(root, {&spinEdge<T>, &ctx}, "spin", spin), 0);

  const T ORIGIN[3] = {T(0), T(0), T(0)};
  T out[3];
  ASSERT_EQ(g.in(root).from(site).point(ORIGIN, out, T(0)), 0);
  EXPECT_NEAR(out[0], T(1), T(1e-6));

  e.t[0] = T(7);
  ASSERT_EQ(g.updateStatic(site, e), 0);
  ASSERT_EQ(g.in(root).from(site).point(ORIGIN, out, T(0)), 0);
  EXPECT_NEAR(out[0], T(7), T(1e-6));

  EXPECT_EQ(g.updateStatic(spin, e), static_cast<uint8_t>(fr::Status::ERROR_BAD_FRAME));
}

/** @test A failing edge provider propagates its status out of resolve. */
TYPED_TEST(FrameGraphTestT, ProviderFailurePropagates) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0, flaky = 0, leaf = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  ASSERT_EQ(g.addTimeDriven(root,
                            {+[](void*, T, Transform<T>*) noexcept -> uint8_t {
                               return static_cast<uint8_t>(fr::Status::ERROR_INVALID_VALUE);
                             },
                             nullptr},
                            "flaky", flaky),
            0);
  ASSERT_EQ(g.addStatic(flaky, Transform<T>{}, "leaf", leaf), 0);

  Transform<T> x;
  // Failing edge on the up path...
  EXPECT_EQ(g.resolve(leaf, root, T(0), x), static_cast<uint8_t>(fr::Status::ERROR_INVALID_VALUE));
  // ...and on the down path.
  EXPECT_EQ(g.resolve(root, leaf, T(0), x), static_cast<uint8_t>(fr::Status::ERROR_INVALID_VALUE));
}

/** @test Out-of-range ids get safe introspection answers and BAD_FRAME resolves. */
TYPED_TEST(FrameGraphTestT, InvalidIdSafety) {
  using T = TypeParam;
  FrameGraph<T> g;
  FrameId root = 0;
  ASSERT_EQ(g.addRoot("root", root), 0);
  const FrameId BOGUS{40};

  EXPECT_EQ(g.parentOf(BOGUS), fr::K_NO_FRAME);
  EXPECT_EQ(g.kindOf(BOGUS), fr::EdgeKind::ROOT);
  EXPECT_STREQ(g.nameOf(BOGUS), "?");
  Transform<T> x;
  EXPECT_EQ(g.resolve(root, BOGUS, T(0), x), static_cast<uint8_t>(fr::Status::ERROR_BAD_FRAME));
  EXPECT_EQ(g.resolve(BOGUS, root, T(0), x), static_cast<uint8_t>(fr::Status::ERROR_BAD_FRAME));
}
