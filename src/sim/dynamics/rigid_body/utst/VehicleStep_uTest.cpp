/**
 * @file VehicleStep_uTest.cpp
 * @brief Consistency proof for the aggregate-consuming stepRigidBody6DOF
 *        overload (VehicleStep.hpp).
 *
 * The overload that takes (AggregateMassProperties, ForceMoment) must
 * produce exactly the same trajectory as the callback-based step fed the
 * unpacked force / moment / mass / inertia. We verify this for a known
 * load: a body force plus an off-CG force whose induced moment we
 * aggregate via the force/moment layer. Tolerance 1e-12 (bit-for-bit same
 * code path).
 */

#include "src/sim/dynamics/force_moment/inc/ForceMoment.hpp"
#include "src/sim/dynamics/inc/VehicleStep.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <gtest/gtest.h>

using sim::dynamics::force_moment::AppliedForce;
using sim::dynamics::force_moment::ForceMoment;
using sim::dynamics::force_moment::ForceMomentAccumulator;
using sim::dynamics::mass_properties::AggregateMassProperties;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::stepRigidBody6DOF;
using sim::dynamics::rigid_body::Vec3;

namespace {
constexpr double kTol = 1e-12;

RigidBody6DOFState makeState() {
  RigidBody6DOFState s;
  s.velocity_body = Vec3{10.0, 1.0, -2.0};
  s.angular_velocity_body = Vec3{0.1, -0.05, 0.2};
  return s;
}
} // namespace

/** @test The aggregate overload equals the callback path step-for-step. */
TEST(VehicleStepProof, AggregateOverloadMatchesCallbackPath) {
  // Mass properties: 1500 kg, full symmetric inertia about CG.
  AggregateMassProperties mp;
  mp.mass_kg = 1500.0;
  mp.cg_m = Vec3{0.0, 0.0, 0.0};
  mp.inertia_about_cg = InertiaTensor{2000.0, 4000.0, 6000.0, 50.0, 10.0, 5.0};

  // Net loads: a body force at the CG plus an off-CG force inducing a
  // moment. Aggregate about the CG so the moment is consistent with I.
  const AppliedForce a{Vec3{500.0, 0.0, -200.0}, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0}};
  const AppliedForce b{Vec3{0.0, 0.0, -1000.0}, Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0}};
  ForceMomentAccumulator loads;
  loads.add(a);
  loads.add(b);
  const ForceMoment fm = loads.resultAbout(mp.cg_m);

  const double t = 0.0;
  const double dt = 0.02;

  // Path 1: aggregate-consuming overload.
  RigidBody6DOFState s1 = makeState();
  stepRigidBody6DOF(s1, mp, fm, t, dt);

  // Path 2: callback overload with the unpacked force / moment / mass / I.
  RigidBody6DOFState s2 = makeState();
  const Vec3 force = fm.force;
  const Vec3 moment = fm.moment;
  stepRigidBody6DOF(
      s2, [&force](double, const RigidBody6DOFState&) { return force; },
      [&moment](double, const RigidBody6DOFState&) { return moment; }, mp.mass_kg,
      mp.inertia_about_cg, t, dt);

  // Both paths must yield an identical state.
  EXPECT_NEAR(s1.velocity_body.x, s2.velocity_body.x, kTol);
  EXPECT_NEAR(s1.velocity_body.y, s2.velocity_body.y, kTol);
  EXPECT_NEAR(s1.velocity_body.z, s2.velocity_body.z, kTol);
  EXPECT_NEAR(s1.angular_velocity_body.x, s2.angular_velocity_body.x, kTol);
  EXPECT_NEAR(s1.angular_velocity_body.y, s2.angular_velocity_body.y, kTol);
  EXPECT_NEAR(s1.angular_velocity_body.z, s2.angular_velocity_body.z, kTol);
  EXPECT_NEAR(s1.position_inertial.x, s2.position_inertial.x, kTol);
  EXPECT_NEAR(s1.position_inertial.y, s2.position_inertial.y, kTol);
  EXPECT_NEAR(s1.position_inertial.z, s2.position_inertial.z, kTol);
  EXPECT_NEAR(s1.attitude.w, s2.attitude.w, kTol);
  EXPECT_NEAR(s1.attitude.x, s2.attitude.x, kTol);
  EXPECT_NEAR(s1.attitude.y, s2.attitude.y, kTol);
  EXPECT_NEAR(s1.attitude.z, s2.attitude.z, kTol);
}
