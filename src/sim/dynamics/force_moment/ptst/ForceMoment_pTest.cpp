/**
 * @file ForceMoment_pTest.cpp
 * @brief Performance test for the force/moment RT path.
 *
 * RT-path: a vehicle re-stacks its applied loads each tick. Throughput sets
 * the per-tick cost of the net force + moment combine (one op per lambda;
 * per-op time derived from callsPerSecond).
 *
 * Usage:
 *   ./SimDynamicsForceMoment_PTEST --gtest_list_tests
 *   ./SimDynamicsForceMoment_PTEST --profile perf --gtest_filter="*ForceMoment*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/force_moment/inc/ForceMoment.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp" // Vec3

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::force_moment::AppliedForce;
using sim::dynamics::force_moment::ForceMoment;
using sim::dynamics::force_moment::ForceMomentAccumulator;
using sim::dynamics::rigid_body::Vec3;

// The force side of a tick: net force + moment about the CG from five applied
// loads (gravity, aero at the aerodynamic center, two engine thrusts at their
// mount points, a control-surface couple). Each `resultAbout()` runs the
// force analogue of parallel-axis -- moment transfer of every offset force.
PERF_TEST(ForceMomentAccumulatorResult, Throughput) {
  UB_PERF_GUARD(perf);

  const Vec3 cg{0.3, 0.0, 0.1};
  const AppliedForce gravity{Vec3{0.0, 0.0, 1.3e5}, cg, Vec3{}};
  const AppliedForce aero{Vec3{-4.0e4, 0.0, -2.0e5}, Vec3{0.6, 0.0, 0.0}, Vec3{}};
  const AppliedForce thrustL{Vec3{6.0e4, 0.0, 0.0}, Vec3{-3.0, -4.0, 1.0}, Vec3{}};
  const AppliedForce thrustR{Vec3{6.0e4, 0.0, 0.0}, Vec3{-3.0, 4.0, 1.0}, Vec3{}};
  const AppliedForce surface{Vec3{}, Vec3{}, Vec3{0.0, 1.5e4, 0.0}}; // pure couple

  auto build = [&] {
    ForceMomentAccumulator acc;
    acc.add(gravity);
    acc.add(aero);
    acc.add(thrustL);
    acc.add(thrustR);
    acc.add(surface);
    return acc;
  };

  perf.warmup([&] {
    auto acc = build();
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto fm = acc.resultAbout(cg);
      (void)fm;
    }
  });

  volatile double sink = 0.0;
  auto acc = build();
  auto result = perf.throughputLoop(
      [&] {
        const ForceMoment fm = acc.resultAbout(cg);
        sink += fm.force.x + fm.moment.y;
      },
      "force_moment_accumulate");

  std::printf("\n[ForceMomentAccumulator] resultAbout (5 loads): %.0f stacks/s (%.4f us/stack)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
