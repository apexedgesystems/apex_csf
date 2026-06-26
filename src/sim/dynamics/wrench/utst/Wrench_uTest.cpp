/**
 * @file Wrench_uTest.cpp
 * @brief PROOF tests for the compositional force/moment accumulator.
 *
 * Every expected value is HAND-COMPUTED. Tolerance is 1e-9. These verify
 * the force sum, the induced-moment shift r x F, pure-couple passthrough,
 * order-independence, and source-vs-value equivalence.
 *
 *   force  = sum( part.force )
 *   moment = sum( part.moment + (part.point_m - about) x part.force )
 */

#include "src/sim/dynamics/wrench/inc/Wrench.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"

#include <memory>

#include <gtest/gtest.h>

using sim::dynamics::rigid_body::Vec3;
using sim::dynamics::wrench::AppliedWrench;
using sim::dynamics::wrench::DynamicWrenchSource;
using sim::dynamics::wrench::StaticWrenchSource;
using sim::dynamics::wrench::Wrench;
using sim::dynamics::wrench::WrenchAccumulator;

namespace {
constexpr double kTol = 1e-9;

/** Combine a list of applied forces about `about` via the accumulator. */
Wrench combine(std::initializer_list<AppliedWrench> parts, const Vec3& about) {
  WrenchAccumulator acc;
  for (const auto& p : parts) {
    acc.add(p);
  }
  return acc.resultAbout(about);
}
} // namespace

/* ----------------------------- 1. Force offset -> moment ----------------------------- */

/**
 * @test Proof 1. A downward force (0,0,-10) at x=2, about origin.
 *   r = (2,0,0), F = (0,0,-10)
 *   r x F = (0*(-10) - 0*0, 0*0 - 2*(-10), 2*0 - 0*0) = (0, 20, 0)
 */
TEST(WrenchProof, ForceOffsetInducesMoment) {
  const AppliedWrench p{Vec3{0, 0, -10}, Vec3{2, 0, 0}, Vec3{0, 0, 0}};
  const Wrench w = combine({p}, Vec3{0, 0, 0});

  EXPECT_NEAR(w.force.x, 0.0, kTol);
  EXPECT_NEAR(w.force.y, 0.0, kTol);
  EXPECT_NEAR(w.force.z, -10.0, kTol);
  EXPECT_NEAR(w.moment.x, 0.0, kTol);
  EXPECT_NEAR(w.moment.y, 20.0, kTol);
  EXPECT_NEAR(w.moment.z, 0.0, kTol);
}

/* ----------------------------- 2. Pure couple ----------------------------- */

/**
 * @test Proof 2. Equal-and-opposite forces form a pure couple, net force 0.
 *   +y force (0,10,0) at x=+1:  r x F = (1,0,0) x (0,10,0) = (0,0,10)
 *   -y force (0,-10,0) at x=-1: r x F = (-1,0,0) x (0,-10,0) = (0,0,10)
 *   net force = 0, net moment = (0,0,20)
 */
TEST(WrenchProof, PureCoupleFromOpposedForces) {
  const AppliedWrench a{Vec3{0, 10, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 0}};
  const AppliedWrench b{Vec3{0, -10, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 0}};
  const Wrench w = combine({a, b}, Vec3{0, 0, 0});

  EXPECT_NEAR(w.force.x, 0.0, kTol);
  EXPECT_NEAR(w.force.y, 0.0, kTol);
  EXPECT_NEAR(w.force.z, 0.0, kTol);
  EXPECT_NEAR(w.moment.x, 0.0, kTol);
  EXPECT_NEAR(w.moment.y, 0.0, kTol);
  EXPECT_NEAR(w.moment.z, 20.0, kTol);
}

/* ----------------------------- 3. Own moment passthrough ----------------------------- */

/**
 * @test Proof 3. A pure couple is independent of its application point and
 *   of the reference point: force 0, moment (5,0,0) at (7,7,7), about origin
 *   -> force 0, moment (5,0,0) (no induced term since force is 0).
 */
TEST(WrenchProof, OwnMomentPassthrough) {
  const AppliedWrench p{Vec3{0, 0, 0}, Vec3{7, 7, 7}, Vec3{5, 0, 0}};
  const Wrench w = combine({p}, Vec3{0, 0, 0});

  EXPECT_NEAR(w.force.x, 0.0, kTol);
  EXPECT_NEAR(w.force.y, 0.0, kTol);
  EXPECT_NEAR(w.force.z, 0.0, kTol);
  EXPECT_NEAR(w.moment.x, 5.0, kTol);
  EXPECT_NEAR(w.moment.y, 0.0, kTol);
  EXPECT_NEAR(w.moment.z, 0.0, kTol);
}

/* ----------------------------- 4. Force at reference point ----------------------------- */

/**
 * @test Proof 4. A force applied exactly at the reference point induces no
 *   moment: force (1,2,3) at origin, about origin -> r = 0, r x F = 0.
 */
TEST(WrenchProof, ForceAtReferencePointNoMoment) {
  const AppliedWrench p{Vec3{1, 2, 3}, Vec3{0, 0, 0}, Vec3{0, 0, 0}};
  const Wrench w = combine({p}, Vec3{0, 0, 0});

  EXPECT_NEAR(w.force.x, 1.0, kTol);
  EXPECT_NEAR(w.force.y, 2.0, kTol);
  EXPECT_NEAR(w.force.z, 3.0, kTol);
  EXPECT_NEAR(w.moment.x, 0.0, kTol);
  EXPECT_NEAR(w.moment.y, 0.0, kTol);
  EXPECT_NEAR(w.moment.z, 0.0, kTol);
}

/* ----------------------------- 5. Superposition / order independence -----------------------------
 */

/**
 * @test Proof 5. Aggregation is order-independent: the result from a set
 *   equals the result from the reversed set (sums commute).
 */
TEST(WrenchProof, OrderIndependence) {
  const AppliedWrench a{Vec3{3, -1, 2}, Vec3{1.0, 2.0, -0.5}, Vec3{0.2, 0.1, 0.3}};
  const AppliedWrench b{Vec3{-2, 4, 1}, Vec3{-0.5, 1.5, 2.0}, Vec3{0.4, 0.6, 0.5}};
  const AppliedWrench c{Vec3{0.5, 0.0, -3}, Vec3{3.0, 0.0, -1.0}, Vec3{0.1, 0.2, 0.7}};
  const Vec3 about{0.3, -0.4, 0.7};

  const Wrench abc = combine({a, b, c}, about);
  const Wrench cba = combine({c, b, a}, about);

  EXPECT_NEAR(abc.force.x, cba.force.x, kTol);
  EXPECT_NEAR(abc.force.y, cba.force.y, kTol);
  EXPECT_NEAR(abc.force.z, cba.force.z, kTol);
  EXPECT_NEAR(abc.moment.x, cba.moment.x, kTol);
  EXPECT_NEAR(abc.moment.y, cba.moment.y, kTol);
  EXPECT_NEAR(abc.moment.z, cba.moment.z, kTol);
}

/* ----------------------------- 6. Source aggregation == value aggregation
 * ----------------------------- */

namespace {
/** A trivial dynamic source that reports a stored applied force. */
struct StubDynamicWrenchSource : DynamicWrenchSource {
  AppliedWrench f;
  [[nodiscard]] AppliedWrench current() const noexcept override { return f; }
};
} // namespace

/**
 * @test Proof 6. An accumulator sampling Static/Dynamic sources equals an
 *   accumulator fed the same AppliedWrench values.
 */
TEST(WrenchProof, SourceAggregationMatchesValueAggregation) {
  const AppliedWrench wa{Vec3{0, 0, -10}, Vec3{2, 0, 0}, Vec3{0, 0, 0}};
  const AppliedWrench wb{Vec3{0, 10, 0}, Vec3{1, 0, 0}, Vec3{1, 0, 0}};

  StaticWrenchSource src_a;
  src_a.f = wa;
  StubDynamicWrenchSource src_b;
  src_b.f = wb;

  const Vec3 about{0.0, 0.0, 0.0};

  WrenchAccumulator from_sources;
  from_sources.add(src_a);
  from_sources.add(src_b);
  const Wrench src_result = from_sources.resultAbout(about);

  const Wrench val_result = combine({wa, wb}, about);

  EXPECT_NEAR(src_result.force.x, val_result.force.x, kTol);
  EXPECT_NEAR(src_result.force.y, val_result.force.y, kTol);
  EXPECT_NEAR(src_result.force.z, val_result.force.z, kTol);
  EXPECT_NEAR(src_result.moment.x, val_result.moment.x, kTol);
  EXPECT_NEAR(src_result.moment.y, val_result.moment.y, kTol);
  EXPECT_NEAR(src_result.moment.z, val_result.moment.z, kTol);

  // And the value itself, hand-computed:
  //   force  = (0,0,-10) + (0,10,0) = (0,10,-10)
  //   moment: a -> (2,0,0)x(0,0,-10) = (0,20,0)
  //           b -> own (1,0,0) + (1,0,0)x(0,10,0) = (1,0,0)+(0,0,10) = (1,0,10)
  //           sum = (1,20,10)
  EXPECT_NEAR(src_result.force.x, 0.0, kTol);
  EXPECT_NEAR(src_result.force.y, 10.0, kTol);
  EXPECT_NEAR(src_result.force.z, -10.0, kTol);
  EXPECT_NEAR(src_result.moment.x, 1.0, kTol);
  EXPECT_NEAR(src_result.moment.y, 20.0, kTol);
  EXPECT_NEAR(src_result.moment.z, 10.0, kTol);
}

/* ----------------------------- Defensive / lifecycle ----------------------------- */

/**
 * @test clear() drops every fixed load and source, so an accumulator reused
 * across a rebuild starts with zero net force and moment.
 */
TEST(WrenchProof, ClearResetsAccumulator) {
  WrenchAccumulator acc;
  acc.add(AppliedWrench{Vec3{10.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{}});
  acc.add(AppliedWrench{Vec3{0.0, 5.0, 0.0}, Vec3{}, Vec3{}});
  ASSERT_NEAR(acc.resultAbout(Vec3{}).force.x, 10.0, kTol);

  acc.clear();
  const Wrench empty = acc.resultAbout(Vec3{});
  EXPECT_NEAR(empty.force.x, 0.0, kTol);
  EXPECT_NEAR(empty.force.y, 0.0, kTol);
  EXPECT_NEAR(empty.moment.z, 0.0, kTol);
}

/**
 * @test A source owned through the base interface destructs cleanly -- the
 * virtual destructor is what lets callers hold heterogeneous loads
 * polymorphically.
 */
TEST(WrenchProof, SourceDestructsThroughBasePointer) {
  std::unique_ptr<sim::dynamics::wrench::WrenchSource> src = std::make_unique<StaticWrenchSource>();
  src.reset(); // exercises ~WrenchSource through the base pointer
  EXPECT_EQ(src, nullptr);
}
