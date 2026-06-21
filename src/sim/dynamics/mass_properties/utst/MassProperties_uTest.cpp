/**
 * @file MassProperties_uTest.cpp
 * @brief PROOF tests for the full InertiaTensor and the compositional
 *        mass-properties aggregator.
 *
 * Every expected value is HAND-COMPUTED. Tolerance is 1e-9. These verify
 * the tensor multiply/solve round-trip and the parallel-axis aggregation.
 */

#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <initializer_list>
#include <memory>

#include <gtest/gtest.h>

using sim::dynamics::mass_properties::AggregateMassProperties;
using sim::dynamics::mass_properties::MassAccumulator;
using sim::dynamics::mass_properties::MassContributor;
using sim::dynamics::mass_properties::StaticMassSource;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::Vec3;

namespace {
constexpr double kTol = 1e-9;

MassContributor point(double m, Vec3 cg, InertiaTensor own = InertiaTensor{0, 0, 0, 0, 0, 0}) {
  return MassContributor{m, cg, own};
}

/** Aggregate a list of fixed contributors through the public accumulator. */
AggregateMassProperties aggregate(std::initializer_list<MassContributor> parts) {
  MassAccumulator acc;
  for (const auto& p : parts) {
    acc.add(p);
  }
  return acc.result();
}
} // namespace

/* ----------------------------- 1. Diagonal tensor ----------------------------- */

TEST(InertiaTensorProof, DiagonalMultiplyAndSolve) {
  const InertiaTensor I{2, 4, 8, 0, 0, 0};

  const Vec3 mv = I.multiply(Vec3{1, 1, 1});
  EXPECT_NEAR(mv.x, 2.0, kTol);
  EXPECT_NEAR(mv.y, 4.0, kTol);
  EXPECT_NEAR(mv.z, 8.0, kTol);

  const Vec3 sv = I.solve(Vec3{2, 4, 8});
  EXPECT_NEAR(sv.x, 1.0, kTol);
  EXPECT_NEAR(sv.y, 1.0, kTol);
  EXPECT_NEAR(sv.z, 1.0, kTol);
}

/* ----------------------------- 2. Full round-trip ----------------------------- */

TEST(InertiaTensorProof, FullTensorRoundTrip) {
  const InertiaTensor I{12, 8, 16, 1.5, 2.0, 1.0};
  const Vec3 w{0.3, -0.7, 1.1};
  const Vec3 back = I.solve(I.multiply(w));
  EXPECT_NEAR(back.x, w.x, kTol);
  EXPECT_NEAR(back.y, w.y, kTol);
  EXPECT_NEAR(back.z, w.z, kTol);
}

TEST(InertiaTensorProof, XzSymmetricRoundTrip) {
  const InertiaTensor I{12, 8, 16, 1.5, 0, 0};
  const Vec3 w{0.3, -0.7, 1.1};
  const Vec3 back = I.solve(I.multiply(w));
  EXPECT_NEAR(back.x, w.x, kTol);
  EXPECT_NEAR(back.y, w.y, kTol);
  EXPECT_NEAR(back.z, w.z, kTol);
}

/* ----------------------------- 3. Two unit point masses at x = +/-1 -----------------------------
 */

TEST(MassPropertiesProof, TwoUnitMassesOnXAxis) {
  const auto agg = aggregate({point(1.0, Vec3{1, 0, 0}), point(1.0, Vec3{-1, 0, 0})});
  EXPECT_NEAR(agg.mass_kg, 2.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixx, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyy, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Izz, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixz, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixy, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyz, 0.0, kTol);
}

/* ----------------------------- 4. Asymmetric products of inertia ----------------------------- */

TEST(MassPropertiesProof, AsymmetricProductsOfInertia) {
  const auto agg = aggregate({point(1.0, Vec3{1, 1, 0}), point(1.0, Vec3{-1, -1, 0})});
  EXPECT_NEAR(agg.mass_kg, 2.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixx, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyy, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Izz, 4.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixz, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixy, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyz, 0.0, kTol);
}

/* ----------------------------- 5. Mass-weighted CG ----------------------------- */

TEST(MassPropertiesProof, MassWeightedCG) {
  const auto agg = aggregate({point(1.0, Vec3{0, 0, 0}), point(3.0, Vec3{4, 0, 0})});
  EXPECT_NEAR(agg.mass_kg, 4.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, 3.0, kTol); // (1*0 + 3*4)/4 = 3
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);
}

/* ----------------------------- 6. Own inertia + parallel-axis ----------------------------- */

TEST(MassPropertiesProof, OwnInertiaPlusParallelAxis) {
  const InertiaTensor own{1, 1, 1, 0, 0, 0};
  const auto agg = aggregate({point(1.0, Vec3{0, 0, 5}, own), point(1.0, Vec3{0, 0, -5}, own)});
  EXPECT_NEAR(agg.cg_m.x, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);
  // Ixx = own(1+1) + m*d.z^2 x2 = 2 + 25 + 25 = 52
  EXPECT_NEAR(agg.inertia_about_cg.Ixx, 52.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyy, 52.0, kTol);
  // Izz = own(1+1) + 0 (offset along z) = 2
  EXPECT_NEAR(agg.inertia_about_cg.Izz, 2.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixz, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixy, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyz, 0.0, kTol);
}

/* ----------------------------- 7. Order independence ----------------------------- */

TEST(MassPropertiesProof, OrderIndependence) {
  const auto a = point(2.0, Vec3{1.0, -2.0, 0.5}, InertiaTensor{3, 4, 5, 0.2, 0.1, 0.3});
  const auto b = point(5.0, Vec3{-0.5, 1.5, 2.0}, InertiaTensor{7, 8, 9, 0.4, 0.6, 0.5});
  const auto c = point(1.5, Vec3{3.0, 0.0, -1.0}, InertiaTensor{2, 1, 6, 0.1, 0.2, 0.7});

  const auto abc = aggregate({a, b, c});
  const auto cba = aggregate({c, b, a});

  EXPECT_NEAR(abc.mass_kg, cba.mass_kg, kTol);
  EXPECT_NEAR(abc.cg_m.x, cba.cg_m.x, kTol);
  EXPECT_NEAR(abc.cg_m.y, cba.cg_m.y, kTol);
  EXPECT_NEAR(abc.cg_m.z, cba.cg_m.z, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Ixx, cba.inertia_about_cg.Ixx, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Iyy, cba.inertia_about_cg.Iyy, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Izz, cba.inertia_about_cg.Izz, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Ixz, cba.inertia_about_cg.Ixz, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Ixy, cba.inertia_about_cg.Ixy, kTol);
  EXPECT_NEAR(abc.inertia_about_cg.Iyz, cba.inertia_about_cg.Iyz, kTol);
}

/* ----------------------------- 8. Single contributor ----------------------------- */

TEST(MassPropertiesProof, SingleContributorUnchanged) {
  const InertiaTensor own{3.5, 4.5, 5.5, 0.2, 0.3, 0.4};
  const Vec3 cg{1.0, 2.0, 3.0};
  const auto agg = aggregate({point(7.0, cg, own)});

  EXPECT_NEAR(agg.mass_kg, 7.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, cg.x, kTol);
  EXPECT_NEAR(agg.cg_m.y, cg.y, kTol);
  EXPECT_NEAR(agg.cg_m.z, cg.z, kTol);
  // d = cg - cg = 0, so net inertia == own inertia exactly.
  EXPECT_NEAR(agg.inertia_about_cg.Ixx, own.Ixx, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyy, own.Iyy, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Izz, own.Izz, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixz, own.Ixz, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixy, own.Ixy, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyz, own.Iyz, kTol);
}

/* ----------------------------- 9. Symmetric positive-definite ----------------------------- */

TEST(MassPropertiesProof, RealisticAggregateIsPositiveDefinite) {
  // A realistic vehicle: dry structure + offset fuel + payload.
  MassContributor dry{1000.0, Vec3{0.0, 0.0, 0.0}, InertiaTensor{2000, 4000, 6000, 50, 0, 0}};
  MassContributor fuel{600.0, Vec3{1.5, 0.0, 0.2}, InertiaTensor{80, 160, 200, 0, 0, 0}};
  MassContributor payload{200.0, Vec3{-0.5, 0.3, -0.4}, InertiaTensor{30, 40, 50, 0, 0, 0}};

  const auto agg = aggregate({dry, fuel, payload});
  const auto& I = agg.inertia_about_cg;

  EXPECT_GT(I.Ixx, 0.0);
  EXPECT_GT(I.Iyy, 0.0);
  EXPECT_GT(I.Izz, 0.0);

  // Positive-definiteness via Sylvester's criterion on the matrix
  //   [ Ixx -Ixy -Ixz; -Ixy Iyy -Iyz; -Ixz -Iyz Izz ].
  const double a11 = I.Ixx, a12 = -I.Ixy, a13 = -I.Ixz, a22 = I.Iyy, a23 = -I.Iyz, a33 = I.Izz;
  const double det2 = a11 * a22 - a12 * a12; // leading 2x2 minor
  const double det3 =
      a11 * (a22 * a33 - a23 * a23) - a12 * (a12 * a33 - a23 * a13) + a13 * (a12 * a23 - a22 * a13);
  EXPECT_GT(a11, 0.0);
  EXPECT_GT(det2, 0.0);
  EXPECT_GT(det3, 0.0);
}

/* ----------------------------- PROOF 7: StaticMassSource composition -----------------------------
 */

/**
 * @test Proof 7 (hand-computed, tol 1e-9).
 *
 * StaticMassSource.current() returns its stored contributor verbatim, and
 * a MassAccumulator fed the two sources equals one fed the same values.
 *
 * Two equal 1 kg masses at x = +/-1: CG at origin, Iyy = Izz = 2, Ixx = 0.
 */
TEST(MassPropertiesProof, StaticSourceCurrentAndAccumulator) {
  StaticMassSource a;
  a.c = point(1.0, Vec3{1, 0, 0});
  StaticMassSource b;
  b.c = point(1.0, Vec3{-1, 0, 0});

  // current() returns the stored contributor verbatim.
  EXPECT_NEAR(a.current().mass_kg, 1.0, kTol);
  EXPECT_NEAR(a.current().cg_m.x, 1.0, kTol);
  EXPECT_NEAR(b.current().cg_m.x, -1.0, kTol);

  MassAccumulator from_sources;
  from_sources.add(a);
  from_sources.add(b);
  const auto agg_src = from_sources.result();

  const auto agg_val = aggregate({a.c, b.c});

  EXPECT_NEAR(agg_src.mass_kg, 2.0, kTol);
  EXPECT_NEAR(agg_src.cg_m.x, 0.0, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Ixx, 0.0, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Iyy, 2.0, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Izz, 2.0, kTol);

  // Source path == value path, component by component.
  EXPECT_NEAR(agg_src.mass_kg, agg_val.mass_kg, kTol);
  EXPECT_NEAR(agg_src.cg_m.x, agg_val.cg_m.x, kTol);
  EXPECT_NEAR(agg_src.cg_m.y, agg_val.cg_m.y, kTol);
  EXPECT_NEAR(agg_src.cg_m.z, agg_val.cg_m.z, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Ixx, agg_val.inertia_about_cg.Ixx, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Iyy, agg_val.inertia_about_cg.Iyy, kTol);
  EXPECT_NEAR(agg_src.inertia_about_cg.Izz, agg_val.inertia_about_cg.Izz, kTol);
}

/* ----------------------------- Defensive / lifecycle ----------------------------- */

// A body with no mass (empty, or only massless markers) returns the default
// instead of dividing by the zero total: mass 0, CG at the origin, and the
// identity inertia tensor (the InertiaTensor default, which keeps a downstream
// solve() invertible rather than singular). This guard keeps an unconfigured
// vehicle well-defined rather than producing NaNs.
TEST(MassPropertiesProof, ZeroMassReturnsDefault) {
  MassAccumulator acc;
  acc.add(point(0.0, Vec3{5.0, -2.0, 1.0})); // a massless reference marker
  const auto agg = acc.result();

  EXPECT_NEAR(agg.mass_kg, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixx, 1.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Iyy, 1.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Izz, 1.0, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Ixz, 0.0, kTol);
}

// clear() drops every fixed contributor and source, so an accumulator reused
// across a rebuild starts empty again.
TEST(MassPropertiesProof, ClearResetsAccumulator) {
  MassAccumulator acc;
  acc.add(point(3.0, Vec3{1.0, 0.0, 0.0}));
  acc.add(point(2.0, Vec3{-1.0, 0.0, 0.0}));
  ASSERT_NEAR(acc.result().mass_kg, 5.0, kTol);

  acc.clear();
  EXPECT_NEAR(acc.result().mass_kg, 0.0, kTol);
}

// A source owned through the base interface destructs cleanly -- the virtual
// destructor is what lets callers hold heterogeneous sources polymorphically.
TEST(MassPropertiesProof, SourceDestructsThroughBasePointer) {
  std::unique_ptr<sim::dynamics::mass_properties::MassPropsSource> src =
      std::make_unique<StaticMassSource>();
  src.reset(); // exercises ~MassPropsSource through the base pointer
  EXPECT_EQ(src, nullptr);
}
